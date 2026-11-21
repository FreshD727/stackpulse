#include <catch2/catch_test_macros.hpp>
#include <stackpulse/match.hpp>
#include <stdexcept>
using namespace stackpulse;
namespace {
void attack_board(match::Player& p) {
    auto s = modern::spin_fixture(17, modern::Preset::modern, 2);
    s.tick = p.board.tick;
    s.garbage_rng = p.board.garbage_rng;
    s.target_rng = p.board.target_rng;
    p.board = s;
}
} // namespace
TEST_CASE("Match simultaneous attacks arrive next tick without global cancellation") {
    auto s = match::initial_state({1, 2}, 17);
    for (auto& p : s.players)
        attack_board(p);
    const auto a = s.players[0].board.garbage_rng, b = s.players[1].board.garbage_rng;
    std::string error;
    match::Events e;
    REQUIRE(match::advance(s, {modern::clockwise, modern::clockwise}, &e, error));
    REQUIRE(match::advance(s, {modern::hard_drop, modern::hard_drop}, &e, error));
    REQUIRE(e.dispatched.size() == 2);
    REQUIRE(s.scheduled.size() == 2);
    REQUIRE(s.players[0].board.incoming.empty());
    REQUIRE(s.players[1].board.incoming.empty());
    REQUIRE(s.scheduled[0].packet.creation == 1);
    REQUIRE(s.scheduled[0].packet.eligible == 31);
    REQUIRE(s.scheduled[0].delivery == 2);
    REQUIRE(s.players[0].board.garbage_rng != a);
    REQUIRE(s.players[1].board.garbage_rng != b);
    REQUIRE(match::advance(s, {0, 0}, &e, error));
    REQUIRE(s.scheduled.empty());
    REQUIRE(s.players[0].board.incoming.size() == 1);
    REQUIRE(s.players[1].board.incoming.size() == 1);
}
TEST_CASE("Match complete snapshots resume deterministic ticks and reject malformed bytes") {
    auto s = match::initial_state({2, 7, 19}, 43);
    std::string error;
    match::Events e;
    REQUIRE(match::advance(s, {modern::left, modern::target_next, 0}, &e, error));
    REQUIRE(match::disconnect(s, 7));
    auto bytes = match::canonical_bytes(s);
    match::State restored;
    REQUIRE(match::decode_state(bytes, restored));
    REQUIRE(restored == s);
    for (unsigned i = 0; i < 200; ++i) {
        std::vector<std::uint32_t> input{0, 0, i % 17 ? 0u : modern::clockwise};
        REQUIRE(match::advance(s, input, nullptr, error));
        REQUIRE(match::advance(restored, input, nullptr, error));
        REQUIRE(match::state_hash(s) == match::state_hash(restored));
    }
    auto sentinel = restored;
    for (std::size_t n = 0; n < bytes.size(); ++n) {
        auto truncated = bytes;
        truncated.resize(n);
        REQUIRE_FALSE(match::decode_state(truncated, restored));
        REQUIRE(restored == sentinel);
    }
    bytes.push_back(0);
    REQUIRE_FALSE(match::decode_state(bytes, restored));
}
TEST_CASE("Match simultaneous forfeits draw and result occurs exactly once") {
    auto s = match::initial_state({1, 2}, 3);
    REQUIRE(match::disconnect(s, 1));
    REQUIRE(match::disconnect(s, 2));
    s.players[0].deadline = 0;
    s.players[1].deadline = 0;
    std::string error;
    match::Events e;
    REQUIRE(match::advance(s, {0, 0}, &e, error));
    REQUIRE(s.result == match::Result::draw);
    REQUIRE(e.result_generated);
    REQUIRE(e.eliminated == std::vector<std::uint16_t>{1, 2});
    REQUIRE(s.players[0].placement == 1);
    REQUIRE(s.players[1].placement == 1);
    auto before = s;
    REQUIRE_FALSE(match::advance(s, {0, 0}, &e, error));
    REQUIRE(s == before);
}
TEST_CASE("Match forfeit survivor wins and dead attacks are suppressed") {
    auto s = match::initial_state({1, 2}, 9);
    for (auto& p : s.players)
        attack_board(p);
    std::string error;
    match::Events e;
    REQUIRE(match::advance(s, {modern::clockwise, modern::clockwise}, &e, error));
    REQUIRE(match::disconnect(s, 1));
    s.players[0].deadline = s.tick;
    REQUIRE(match::advance(s, {modern::hard_drop, modern::hard_drop}, &e, error));
    REQUIRE(s.result == match::Result::winner);
    REQUIRE(s.players[0].placement == 2);
    REQUIRE(e.dispatched.empty());
}
TEST_CASE("Match rejects inputs and capacity errors atomically and duel target consumes no RNG") {
    auto s = match::initial_state({1, 2}, 9);
    auto before = s;
    std::string error;
    match::Events e;
    e.result_generated = true;
    REQUIRE_FALSE(match::advance(s, {256, 0}, &e, error));
    REQUIRE(s == before);
    REQUIRE(e.result_generated);
    auto rng = s.players[0].board.target_rng;
    REQUIRE(match::advance(s, {modern::target_next, 0}, &e, error));
    REQUIRE(s.players[0].target == 2);
    REQUIRE(s.players[0].board.target_rng == rng);
    REQUIRE_FALSE(match::advance(s, {0}, &e, error));
    REQUIRE_FALSE(match::disconnect(s, 99));
    REQUIRE(match::disconnect(s, 1));
    auto deadline = s.players[0].deadline;
    REQUIRE(match::disconnect(s, 1));
    REQUIRE(s.players[0].deadline == deadline);
    REQUIRE(match::reconnect(s, 1));
    REQUIRE(s.players[0].deadline == 0);
}

TEST_CASE("Match retargets after simultaneous elimination without drawing for sole opponent") {
    auto s = match::initial_state({1, 2, 3}, 17);
    attack_board(s.players[0]);
    s.players[0].target = 2;
    std::string error;
    match::Events e;
    REQUIRE(match::advance(s, {modern::clockwise, 0, 0}, &e, error));
    REQUIRE(match::disconnect(s, 2));
    s.players[1].deadline = s.tick;
    const auto rng = s.players[0].board.target_rng;
    REQUIRE(match::advance(s, {modern::hard_drop, 0, 0}, &e, error));
    REQUIRE(e.dispatched.size() == 1);
    REQUIRE(e.dispatched[0].recipient == 3);
    REQUIRE(s.players[0].board.target_rng == rng);
    REQUIRE(s.players[1].placement == 3);
}
TEST_CASE("Match dispatch capacity exhaustion rolls back boards RNG and events") {
    auto s = match::initial_state({1, 2}, 17);
    attack_board(s.players[0]);
    std::string error;
    match::Events e;
    REQUIRE(match::advance(s, {modern::clockwise, 0}, &e, error));
    for (unsigned i = 1; i <= 256; ++i)
        s.scheduled.push_back({s.tick + 1, 2, {i, s.tick, s.tick + 30, 1, 1, 0}});
    s.next_packet_id = 257;
    REQUIRE(match::valid_state(s));
    const auto before = s;
    e.result_generated = true;
    REQUIRE_FALSE(match::advance(s, {modern::hard_drop, 0}, &e, error));
    REQUIRE(s == before);
    REQUIRE(e.result_generated);
}

TEST_CASE("Match supports all eight stable player IDs and canonical target validation") {
    auto s = match::initial_state({1, 4, 9, 12, 100, 500, 800, 65535}, 1);
    REQUIRE(s.players.size() == 8);
    for (const auto& p : s.players) {
        REQUIRE(p.target != 0);
        REQUIRE(p.target != p.id);
    }
    std::string error;
    REQUIRE(match::advance(s, std::vector<std::uint32_t>(8, 0), nullptr, error));
    match::State restored;
    REQUIRE(match::decode_state(match::canonical_bytes(s), restored));
    REQUIRE(restored == s);
    auto before = s;
    s.players[0].target = 2;
    REQUIRE_FALSE(match::valid_state(s));
    REQUIRE_FALSE(match::decode_state(match::canonical_bytes(s), restored));
    REQUIRE(restored == before);
    REQUIRE_THROWS_AS(match::initial_state({2, 1}, 1), std::invalid_argument);
    REQUIRE_THROWS_AS(match::initial_state({1, 1}, 1), std::invalid_argument);
}

TEST_CASE("Match full eight-player dedup capacity fits complete snapshot codec") {
    auto s = match::initial_state({1, 2, 3, 4, 5, 6, 7, 8}, 47);
    for (auto& p : s.players)
        for (std::uint64_t id = 1; id <= 4096; ++id)
            p.board.received_ids.push_back(id);
    s.next_packet_id = 4097;
    REQUIRE(match::valid_state(s));
    const auto bytes = match::canonical_bytes(s);
    REQUIRE(bytes.size() > 262144);
    REQUIRE(bytes.size() < 1048576);
    match::State restored;
    REQUIRE(match::decode_state(bytes, restored));
    REQUIRE(restored == s);
    std::string error;
    REQUIRE(match::advance(s, std::vector<std::uint32_t>(8, 0), nullptr, error));
    REQUIRE(match::advance(restored, std::vector<std::uint32_t>(8, 0), nullptr, error));
    REQUIRE(restored == s);
}

TEST_CASE("Classic match start level disconnect forfeit and codec preserve authoritative state") {
    auto s = match::initial_state({1, 2}, 17, modern::Preset::classic, 19);
    REQUIRE(s.players[0].board.start_level == 19);
    std::string error;
    match::Events e;
    REQUIRE(match::advance(s, {modern::down, modern::down}, &e, error));
    REQUIRE(match::disconnect(s, 1));
    REQUIRE(match::valid_state(s));
    match::State restored;
    REQUIRE(match::decode_state(match::canonical_bytes(s), restored));
    REQUIRE(restored == s);
    REQUIRE(match::reconnect(s, 1));
    REQUIRE(match::advance(s, {0, 0}, &e, error));
    REQUIRE(match::disconnect(s, 1));
    s.players[0].deadline = s.tick;
    REQUIRE(match::advance(s, {0, 0}, &e, error));
    REQUIRE(s.result == match::Result::winner);
    REQUIRE(s.players[0].board.classic_state->game_over);
    REQUIRE(match::decode_state(match::canonical_bytes(s), restored));
    REQUIRE(restored == s);
    REQUIRE(s.scheduled.empty());
    auto fusion = match::initial_state({1, 2}, 17, modern::Preset::fusion, 12);
    REQUIRE(fusion.players[1].board.start_level == 12);
    auto modern = match::initial_state({1, 2}, 17, modern::Preset::modern, 12);
    REQUIRE(modern.players[1].board.start_level == 0);
}

TEST_CASE("Immediate reconnect treats disconnect input reset as a soft drop release") {
    for (auto preset : {modern::Preset::classic, modern::Preset::fusion, modern::Preset::modern}) {
        auto s = match::initial_state({1, 2}, 17, preset);
        auto& board = s.players[0].board;
        board.held = modern::down;
        board.soft_drop_armed = false;
        if (board.classic_state) {
            board.classic_state->held = modern::down;
            board.classic_state->soft_drop_armed = false;
        }
        REQUIRE(match::valid_state(s));
        REQUIRE(match::disconnect(s, 1));
        REQUIRE(board.held == 0);
        REQUIRE(board.soft_drop_armed);
        REQUIRE(match::valid_state(s));
        match::State restored;
        REQUIRE(match::decode_state(match::canonical_bytes(s), restored));
        REQUIRE(restored == s);
        REQUIRE(match::reconnect(s, 1));
        std::string error;
        match::Events e;
        REQUIRE(match::advance(s, {modern::down, 0}, &e, error));
        REQUIRE(match::advance(s, {modern::down, 0}, &e, error));
        REQUIRE(e.boards[0].soft_dropped);
        REQUIRE(s.players[0].board.score == 1);
    }
}
