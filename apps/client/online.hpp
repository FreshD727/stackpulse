#pragma once
#include "client_logic.hpp"
#include <algorithm>
#include <deque>
#include <stackpulse/network.hpp>

namespace stackpulse::client {
// Only server counters produce online lock/clear/garbage cues. Reconciliation
// never touches this cursor; entering/recovering/switching a board seeds a baseline.
class OnlineSoundObserver {
  public:
    void reset() {
        *this = OnlineSoundObserver{};
    }
    std::vector<Cue> observe(const net::View& view) {
        if (!view.has_snapshot ||
            (view.phase != net::Phase::playing && view.phase != net::Phase::results)) {
            reset();
            return {};
        }
        const auto player = view.role == net::Role::spectator ? view.featured_player : view.player;
        const auto it = std::find_if(view.sounds.begin(), view.sounds.end(), [&](const auto& row) {
            return row.player == player;
        });
        if (it == view.sounds.end()) {
            reset();
            return {};
        }
        const bool baseline =
            !initialized_ || room_ != view.room_id || match_ != view.match_id || player_ != player;
        if (!baseline && view.snapshot_revision <= revision_)
            return {};
        std::vector<Cue> cues;
        constexpr std::array mapping{Cue::locked,
                                     Cue::clear,
                                     Cue::clear,
                                     Cue::clear,
                                     Cue::four_line_clear,
                                     Cue::spin_single,
                                     Cue::spin_double,
                                     Cue::spin_triple,
                                     Cue::garbage_warning,
                                     Cue::garbage_hit};
        if (!baseline)
            for (std::size_t i = 0; i < mapping.size(); ++i) {
                // Coalesce catch-up occurrences of each cue rather than burst old audio.
                if (it->counts[i] > counts_[i])
                    cues.push_back(mapping[i]);
            }
        counts_ = it->counts;
        room_ = view.room_id;
        match_ = view.match_id;
        player_ = player;
        revision_ = view.snapshot_revision;
        initialized_ = true;
        return cues;
    }

  private:
    std::uint64_t room_{}, match_{}, revision_{};
    std::uint16_t player_{};
    bool initialized_{};
    decltype(net::SoundCounters::counts) counts_{};
};
enum class RoomAction { ready, return_to_lobby, leave };
inline std::vector<RoomAction> room_actions(const net::View& view) {
    std::vector<RoomAction> actions;
    if (view.phase == net::Phase::waiting && view.role == net::Role::player)
        actions.push_back(RoomAction::ready);
    else if (view.phase == net::Phase::results && view.member_id == view.manager_id)
        actions.push_back(RoomAction::return_to_lobby);
    if (view.phase == net::Phase::waiting || view.phase == net::Phase::countdown ||
        view.phase == net::Phase::results)
        actions.push_back(RoomAction::leave);
    return actions;
}
enum class PlayerStatus { playing, out, disconnected };
struct LeaderboardEntry {
    std::uint16_t id{};
    std::uint64_t score{};
    std::string name;
    PlayerStatus status{};
};
inline std::vector<LeaderboardEntry> leaderboard(const net::View& view) {
    std::vector<LeaderboardEntry> rows;
    for (const auto& player : view.authoritative.players) {
        auto member = std::find_if(view.members.begin(), view.members.end(), [&](const auto& m) {
            return m.player == player.id && m.role == net::Role::player;
        });
        const bool disconnected =
            player.disconnected || (member != view.members.end() && !member->connected);
        rows.push_back(
            {player.id,
             player.board.score,
             member == view.members.end() ? "P" + std::to_string(player.id) : member->name,
             disconnected             ? PlayerStatus::disconnected
             : player.board.game_over ? PlayerStatus::out
                                      : PlayerStatus::playing});
    }
    std::sort(rows.begin(), rows.end(), [](const auto& a, const auto& b) {
        return a.score != b.score ? a.score > b.score : a.id < b.id;
    });
    return rows;
}
struct RoomSelection {
    std::string code;
    net::Role role{net::Role::player};
    bool password_required{};
    void choose(const net::RoomInfo& room, net::Role requested_role) {
        code = room.code;
        role = requested_role;
        password_required = room.password_required;
    }
    // Player selection keeps its explicit role even for a full or running room;
    // the server returns the operation error rather than silently watching.
    void confirm(net::Client& client, const std::string& password) const {
        client.join_room(code, password, role);
    }
};
// Only the local board is speculative. The full match remains immutable authority.
class OnlinePrediction {
  public:
    static constexpr std::size_t history_limit = 240;
    void reset() {
        *this = OnlinePrediction{};
    }
    bool apply(const net::View& view) {
        if (view.role == net::Role::spectator) {
            reset();
            return false;
        }
        if (view.room_id != room_id_ || view.match_id != match_id_ || view.player != player_ ||
            view.room.preset != preset_ || view.room.start_level != start_level_) {
            reset();
            room_id_ = view.room_id;
            match_id_ = view.match_id;
            player_ = view.player;
            preset_ = view.room.preset;
            start_level_ = view.room.start_level;
        }
        if (!view.has_snapshot || view.snapshot_revision <= revision_)
            return false;
        const auto player = std::find_if(view.authoritative.players.begin(),
                                         view.authoritative.players.end(),
                                         [&](const auto& p) { return p.id == view.player; });
        if (player == view.authoritative.players.end() || !match::valid_state(view.authoritative))
            return false;
        if (initialized_ && view.authoritative.tick < authoritative_tick_)
            return false;
        const auto previous_tick = board_.tick;
        const bool expired =
            initialized_ && ((player->board.tick < previous_tick &&
                              (history_.empty() || history_.front().tick > player->board.tick)) ||
                             (player->board.tick > previous_tick &&
                              player->board.tick - previous_tick > history_limit));
        board_ = player->board;
        authoritative_tick_ = view.authoritative.tick;
        revision_ = view.snapshot_revision;
        initialized_ = true;
        last_reset_ = expired;
        // A gap cannot be filled by inventing inputs: accept the complete authority.
        if (expired) {
            history_.clear();
            ++hard_resets_;
        } else {
            for (const auto& frame : history_) {
                if (frame.tick < board_.tick)
                    continue;
                if (frame.tick != board_.tick || board_.game_over)
                    break;
                modern::step(board_, frame.held);
            }
            while (!history_.empty() && history_.front().tick < player->board.tick)
                history_.pop_front();
        }
        return true;
    }
    // Returns events only for a newly sampled tick; reconciliation emits no sounds.
    bool sample(std::uint32_t held, modern::Events& events) {
        events = {};
        if (!initialized_ || board_.game_over || board_.tick > authoritative_tick_ + 120 ||
            (sampled_ && board_.tick <= sampled_through_))
            return false;
        const net::Input frame{board_.tick, ++sequence_, held & modern::action_mask};
        if (!history_.empty() && frame.tick <= history_.back().tick)
            return false;
        const auto tick = board_.tick;
        if (!modern::step(board_, frame.held, &events))
            return false;
        history_.push_back(frame);
        sampled_through_ = tick;
        sampled_ = true;
        if (history_.size() > history_limit)
            history_.pop_front();
        if (tick <= sounded_through_ && sounded_)
            events = {};
        else {
            sounded_through_ = tick;
            sounded_ = true;
        }
        return true;
    }
    std::vector<net::Input> rolling() const {
        const auto count = std::min<std::size_t>(8, history_.size());
        return {history_.end() - static_cast<std::ptrdiff_t>(count), history_.end()};
    }
    const modern::State& board() const {
        return board_;
    }
    bool initialized() const {
        return initialized_;
    }
    bool hard_reset() const {
        return last_reset_;
    }
    std::uint64_t hard_resets() const {
        return hard_resets_;
    }
    std::size_t history_size() const {
        return history_.size();
    }

  private:
    std::uint64_t room_id_{}, match_id_{};
    std::uint16_t player_{};
    modern::Preset preset_{};
    std::uint8_t start_level_{};
    modern::State board_;
    std::deque<net::Input> history_;
    std::uint64_t revision_{}, authoritative_tick_{}, sounded_through_{}, hard_resets_{},
        sampled_through_{};
    std::uint32_t sequence_{};
    bool initialized_{}, last_reset_{}, sounded_{}, sampled_{};
};
} // namespace stackpulse::client
