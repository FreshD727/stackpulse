#pragma once
#include <algorithm>
#include <deque>
#include <stackpulse/network.hpp>

namespace stackpulse::client {
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
        if (view.room_id != room_id_ || view.match_id != match_id_ || view.player != player_) {
            reset();
            room_id_ = view.room_id;
            match_id_ = view.match_id;
            player_ = view.player;
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
    modern::State board_;
    std::deque<net::Input> history_;
    std::uint64_t revision_{}, authoritative_tick_{}, sounded_through_{}, hard_resets_{},
        sampled_through_{};
    std::uint32_t sequence_{};
    bool initialized_{}, last_reset_{}, sounded_{}, sampled_{};
};
} // namespace stackpulse::client
