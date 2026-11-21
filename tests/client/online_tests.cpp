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
