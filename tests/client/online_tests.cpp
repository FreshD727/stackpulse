#include "../../apps/client/online.hpp"
#include <catch2/catch_test_macros.hpp>

namespace {
stackpulse::net::View snapshot(std::uint64_t revision = 1) {
    stackpulse::net::View v;
    v.player = 1;
    v.has_snapshot = true;
    v.snapshot_revision = revision;
    v.authoritative = stackpulse::match::initial_state({1, 2}, 727);
    return v;
}
} // namespace

TEST_CASE("Online sound counters survive skipped snapshots without replaying correction cues",
          "[online][audio]") {
    namespace net = stackpulse::net;
    namespace client = stackpulse::client;
    auto view = snapshot();
    view.phase = net::Phase::playing;
    view.room_id = 1;
    view.match_id = 5;
    view.sounds = {{1, {}}, {2, {}}};
    client::OnlineSoundObserver sounds;
    REQUIRE(sounds.observe(view).empty());
    auto increment = [&](std::size_t player, net::SoundEvent event, std::uint64_t count = 1) {
        view.sounds[player].counts[static_cast<std::size_t>(event)] += count;
    };
    increment(0, net::SoundEvent::locked, 3);
    increment(0, net::SoundEvent::spin_double);
    increment(0, net::SoundEvent::garbage_warning);
    increment(0, net::SoundEvent::garbage_hit);
    ++view.snapshot_revision;
    const std::vector expected{client::Cue::locked,
                               client::Cue::spin_double,
                               client::Cue::garbage_warning,
                               client::Cue::garbage_hit};
    REQUIRE(sounds.observe(view) == expected);
    REQUIRE(sounds.observe(view).empty());
    ++view.snapshot_revision; // Reconciliation with identical counters is silent.
    REQUIRE(sounds.observe(view).empty());
    increment(1, net::SoundEvent::clear_four);
    ++view.snapshot_revision;
    REQUIRE(sounds.observe(view).empty()); // Other board is not the local audio focus.
    view.role = net::Role::spectator;
    view.featured_player = 2;
    REQUIRE(sounds.observe(view).empty()); // Switching focus suppresses history.
    increment(1, net::SoundEvent::clear_four);
    ++view.snapshot_revision;
    REQUIRE(sounds.observe(view) == std::vector{client::Cue::four_line_clear});
    view.phase = net::Phase::connecting;
    REQUIRE(sounds.observe(view).empty());
    increment(1, net::SoundEvent::clear_four, 10);
    view.phase = net::Phase::playing;
    ++view.snapshot_revision;
    REQUIRE(sounds.observe(view).empty()); // Reconnect establishes a fresh baseline.
    ++view.match_id;
    view.sounds = {{1, {}}, {2, {}}};
    REQUIRE(sounds.observe(view).empty());
    increment(1, net::SoundEvent::clear_single);
    ++view.snapshot_revision;
    REQUIRE(sounds.observe(view) == std::vector{client::Cue::clear});
}

TEST_CASE("Lobby directory keeps room and footer selections stable across refreshes",
          "[online][ui]") {
    namespace net = stackpulse::net;
    namespace client = stackpulse::client;
    net::RoomInfo first, second, third;
    first.code = "FIRST";
    second.code = "SECOND";
    third.code = "THIRD";
    const std::vector<net::RoomInfo> rooms{first, second};
    const std::vector<net::RoomInfo> expanded{third, second, first};
    REQUIRE(client::directory_selection(0, rooms, expanded) == 2);
    REQUIRE(client::directory_selection(1, rooms, expanded) == 1);
    REQUIRE(client::directory_selection(1, rooms, {first}) == 0);
    REQUIRE(client::directory_selection(1, rooms, {}) == 0);
    for (const auto& directory : {std::vector<net::RoomInfo>{}, rooms, expanded}) {
        const auto count = static_cast<int>(directory.size());
        REQUIRE(client::directory_action(count, directory.size()) ==
                client::DirectoryAction::refresh);
        REQUIRE(client::directory_action(count + 1, directory.size()) ==
                client::DirectoryAction::back);
        for (const auto& refreshed : {std::vector<net::RoomInfo>{}, rooms, expanded}) {
            const auto refreshed_count = static_cast<int>(refreshed.size());
            REQUIRE(client::directory_selection(count, directory, refreshed) == refreshed_count);
            REQUIRE(client::directory_selection(count + 1, directory, refreshed) ==
                    refreshed_count + 1);
            REQUIRE(client::directory_selection(999, directory, refreshed) == refreshed_count + 1);
            const auto selected = client::directory_selection(-1, directory, refreshed);
            REQUIRE(selected >= 0);
            REQUIRE(selected <= refreshed_count + 1);
        }
    }
    REQUIRE(client::directory_action(0, rooms.size()) == client::DirectoryAction::join);
}

TEST_CASE("Lobby actions keep Leave reachable for every member and phase", "[online][ui]") {
    namespace net = stackpulse::net;
    namespace client = stackpulse::client;
    for (const auto phase : {net::Phase::waiting, net::Phase::countdown, net::Phase::results})
        for (const auto role : {net::Role::player, net::Role::spectator})
            for (const bool manager : {false, true}) {
                net::View view;
                view.phase = phase;
                view.role = role;
                view.member_id = 1;
                view.manager_id = manager ? 1 : 2;
                const auto actions = client::room_actions(view);
                REQUIRE_FALSE(actions.empty());
                REQUIRE(actions.back() == client::RoomAction::leave);
                const bool can_ready = phase == net::Phase::waiting && role == net::Role::player;
                const bool can_return = phase == net::Phase::results && manager;
                REQUIRE(actions.size() == (can_ready || can_return ? 2U : 1U));
                if (can_ready)
                    REQUIRE(actions.front() == client::RoomAction::ready);
                if (can_return)
                    REQUIRE(actions.front() == client::RoomAction::return_to_lobby);
            }
    for (const auto phase : {net::Phase::connecting,
                             net::Phase::playing,
                             net::Phase::browsing,
                             net::Phase::disconnected,
                             net::Phase::error}) {
        net::View view;
        view.phase = phase;
        REQUIRE(client::room_actions(view).empty());
    }
}

TEST_CASE("Online prediction restores authority and replays only stable local inputs", "[online]") {
    namespace sp = stackpulse;
    auto v = snapshot();
    sp::client::OnlinePrediction p;
    REQUIRE(p.apply(v));
    const auto opponent = v.authoritative.players[1].board;
    sp::modern::Events e;
    REQUIRE(p.sample(sp::modern::right, e));
    REQUIRE(p.sample(sp::modern::down, e));
    REQUIRE(p.sample(0, e));
    const auto stable = p.rolling();
    auto corrected = v.authoritative.players[0].board;
    corrected.score = 400;
    v.authoritative.players[0].board = corrected;
    ++v.snapshot_revision;
    REQUIRE(p.apply(v));
    for (const auto& f : stable)
        REQUIRE(sp::modern::step(corrected, f.held));
    REQUIRE(p.board() == corrected);
    REQUIRE(v.authoritative.players[1].board == opponent);
    REQUIRE(p.rolling().size() == stable.size());
    for (std::size_t i = 0; i < stable.size(); ++i) {
        REQUIRE(p.rolling()[i].tick == stable[i].tick);
        REQUIRE(p.rolling()[i].sequence == stable[i].sequence);
        REQUIRE(p.rolling()[i].held == stable[i].held);
    }
    REQUIRE_FALSE(p.apply(v));
    v.snapshot_revision = 1;
    REQUIRE_FALSE(p.apply(v));
}

TEST_CASE("Online history expiry restores complete state without rewriting future ticks",
          "[online]") {
    namespace sp = stackpulse;
    auto v = snapshot();
    sp::client::OnlinePrediction p;
    REQUIRE(p.apply(v));
    sp::modern::Events e;
    // Keep receiving usable authority while accumulating more than history_limit
    // frames; a later older-but-new-revision snapshot cannot regress authority.
    for (unsigned i = 0; i < 270; ++i) {
        REQUIRE(p.sample(0, e));
        if (i % 60 == 59) {
            std::string error;
            for (unsigned j = 0; j < 60; ++j)
                REQUIRE(sp::match::advance(v.authoritative, {0, 0}, nullptr, error));
            ++v.snapshot_revision;
            REQUIRE(p.apply(v));
        }
    }
    REQUIRE(p.history_size() <= sp::client::OnlinePrediction::history_limit);
    auto stale = snapshot(v.snapshot_revision + 1);
    REQUIRE_FALSE(p.apply(stale));
    std::string error;
    for (unsigned i = 0; i < 400; ++i)
        REQUIRE(sp::match::advance(v.authoritative, {0, 0}, nullptr, error));
    ++v.snapshot_revision;
    REQUIRE(p.apply(v));
    REQUIRE(p.hard_reset());
    REQUIRE(p.hard_resets() == 1);
    REQUIRE(p.board() == v.authoritative.players[0].board);
    // Lost complete history is represented by a fresh predictor seeded with a
    // complete later snapshot; all future-relevant queues and timers survive.
    sp::client::OnlinePrediction fresh;
    REQUIRE(fresh.apply(v));
    REQUIRE(fresh.board() == v.authoritative.players[0].board);
}

TEST_CASE("Invalid online authority does not mutate prediction", "[online]") {
    auto v = snapshot();
    stackpulse::client::OnlinePrediction p;
    REQUIRE(p.apply(v));
    const auto before = p.board();
    ++v.snapshot_revision;
    v.authoritative.players[0].board.board[0] = 255;
    REQUIRE_FALSE(p.apply(v));
    REQUIRE(p.board() == before);
}

TEST_CASE("A new room or match epoch resets speculative input and permits tick zero", "[online]") {
    auto v = snapshot();
    v.room_id = 3;
    v.match_id = 4;
    stackpulse::client::OnlinePrediction p;
    REQUIRE(p.apply(v));
    stackpulse::modern::Events events;
    REQUIRE(p.sample(stackpulse::modern::right, events));
    REQUIRE(p.history_size() == 1);
    ++v.match_id;
    ++v.snapshot_revision;
    REQUIRE(p.apply(v));
    REQUIRE(p.history_size() == 0);
    REQUIRE(p.board() == v.authoritative.players.front().board);
    REQUIRE(p.sample(stackpulse::modern::right, events));
    REQUIRE(events.shifted);
    ++v.room_id;
    ++v.snapshot_revision;
    REQUIRE(p.apply(v));
    REQUIRE(p.history_size() == 0);
}

TEST_CASE("Spectator views discard local prediction and cannot sample or upload frames",
          "[online]") {
    auto v = snapshot();
    stackpulse::client::OnlinePrediction p;
    REQUIRE(p.apply(v));
    stackpulse::modern::Events events;
    REQUIRE(p.sample(stackpulse::modern::down, events));
    v.role = stackpulse::net::Role::spectator;
    REQUIRE_FALSE(p.apply(v));
    REQUIRE_FALSE(p.initialized());
    REQUIRE_FALSE(p.sample(stackpulse::modern::down, events));
    REQUIRE(p.rolling().empty());
}

TEST_CASE("Leaderboard ranks score then id and distinguishes out from disconnected",
          "[online][ui]") {
    auto view = snapshot();
    view.authoritative.players[0].board.score = 90;
    view.authoritative.players[1].board.score = 120;
    view.authoritative.players[0].board.game_over = true;
    view.authoritative.players[1].board.game_over = true;
    view.authoritative.players[1].disconnected = true;
    auto rows = stackpulse::client::leaderboard(view);
    REQUIRE(rows[0].id == 2);
    REQUIRE(rows[0].status == stackpulse::client::PlayerStatus::disconnected);
    REQUIRE(rows[1].status == stackpulse::client::PlayerStatus::out);
    view.authoritative.players[0].board.score = 120;
    rows = stackpulse::client::leaderboard(view);
    REQUIRE(rows[0].id == 1);
    REQUIRE(rows[1].id == 2);
}
TEST_CASE("Room selection preserves explicit player or watch action across room states",
          "[online][ui]") {
    stackpulse::net::RoomInfo room;
    room.code = "ABCD";
    room.state = stackpulse::net::Phase::playing;
    room.players = room.capacity = 8;
    room.password_required = true;
    stackpulse::client::RoomSelection selected;
    selected.choose(room, stackpulse::net::Role::player);
    REQUIRE(selected.role == stackpulse::net::Role::player);
    REQUIRE(selected.password_required);
    REQUIRE(selected.code == room.code);
    selected.choose(room, stackpulse::net::Role::spectator);
    REQUIRE(selected.role == stackpulse::net::Role::spectator);
    room.password_required = false;
    selected.choose(room, stackpulse::net::Role::player);
    REQUIRE_FALSE(selected.password_required);
    REQUIRE(selected.role == stackpulse::net::Role::player);
}

TEST_CASE("Online prediction starts a new input epoch when lobby rules change", "[online][ui]") {
    auto view = snapshot();
    stackpulse::client::OnlinePrediction prediction;
    REQUIRE(prediction.apply(view));
    stackpulse::modern::Events events;
    REQUIRE(prediction.sample(stackpulse::modern::left, events));
    REQUIRE_FALSE(prediction.rolling().empty());
    view.room.start_level = 4;
    REQUIRE(prediction.apply(view));
    REQUIRE(prediction.rolling().empty());
    REQUIRE(prediction.board() == view.authoritative.players[0].board);
}
