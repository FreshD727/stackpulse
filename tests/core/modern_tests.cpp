#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <fstream>
#include <limits>
#include <regex>
#include <stackpulse/classic.hpp>
#include <stackpulse/modern.hpp>
#include <string>
using namespace stackpulse::modern;
TEST_CASE("Modern seven bag and complete resumable codec") {
    auto s = initial_state(1);
    REQUIRE(s.active.piece == Piece::S);
    REQUIRE(s.next == std::array<Piece, 5>{Piece::I, Piece::L, Piece::O, Piece::T, Piece::Z});
    REQUIRE(enqueue(s, {1, 0, 30, 2, 6, 4}));
    REQUIRE(enqueue(s, {1, 0, 30, 2, 6, 4}));
    REQUIRE(s.incoming.size() == 1);
    for (unsigned i = 0; i < 11; ++i)
        REQUIRE(step(s, right));
    State restored;
    REQUIRE(decode_state(canonical_bytes(s), restored));
    REQUIRE(s == restored);
    for (unsigned i = 0; i < 100; ++i) {
        REQUIRE(step(s, i % 2 ? 0u : down));
        REQUIRE(step(restored, i % 2 ? 0u : down));
        REQUIRE(s == restored);
    }
    auto b = canonical_bytes(s);
    b.push_back(0);
    REQUIRE_FALSE(decode_state(b, restored));
    b = canonical_bytes(s);
    b[4] = 9;
    REQUIRE_FALSE(decode_state(b, restored));
}
TEST_CASE("Every piece and 90 degree transition uses first open kick") {
    for (unsigned p = 0; p < 7; ++p)
        for (unsigned r = 0; r < 4; ++r)
            for (bool cw : {false, true}) {
                auto s = initial_state(1);
                s.active = {static_cast<Piece>(p), static_cast<std::uint8_t>(r), 3, 0};
                REQUIRE(rotate(s, cw));
                REQUIRE(s.active.rotation == (r + (cw ? 1 : 3)) % 4);
                REQUIRE(s.active.x == 3);
                REQUIRE(s.active.y == 0);
                REQUIRE(s.last_kick == 0);
            }
    auto s = initial_state(1);
    s.active = {Piece::T, 0, 3, 18};
    REQUIRE(rotate(s, true));
    REQUIRE(s.last_kick == 2);
    REQUIRE(s.active.x == 2);
    REQUIRE(s.active.y == 17);
    s = initial_state(1);
    s.active = {Piece::T, 1, -1, 0};
    REQUIRE(rotate(s, false));
    REQUIRE(s.last_kick == 1);
    REQUIRE(s.active.x == 0);
}
TEST_CASE("Kick offsets match every ordered normative transition") {
    using K = std::array<Cell, 5>;
    const std::array<K, 8> j{{K{{{0, 0}, {-1, 0}, {-1, -1}, {0, 2}, {-1, 2}}},
                              K{{{0, 0}, {1, 0}, {1, 1}, {0, -2}, {1, -2}}},
                              K{{{0, 0}, {1, 0}, {1, 1}, {0, -2}, {1, -2}}},
                              K{{{0, 0}, {-1, 0}, {-1, -1}, {0, 2}, {-1, 2}}},
                              K{{{0, 0}, {1, 0}, {1, -1}, {0, 2}, {1, 2}}},
                              K{{{0, 0}, {-1, 0}, {-1, 1}, {0, -2}, {-1, -2}}},
                              K{{{0, 0}, {-1, 0}, {-1, 1}, {0, -2}, {-1, -2}}},
                              K{{{0, 0}, {1, 0}, {1, -1}, {0, 2}, {1, 2}}}}};
    const std::array<K, 8> i{{K{{{0, 0}, {-2, 0}, {1, 0}, {-2, 1}, {1, -2}}},
                              K{{{0, 0}, {2, 0}, {-1, 0}, {2, -1}, {-1, 2}}},
                              K{{{0, 0}, {-1, 0}, {2, 0}, {-1, -2}, {2, 1}}},
                              K{{{0, 0}, {1, 0}, {-2, 0}, {1, 2}, {-2, -1}}},
                              K{{{0, 0}, {2, 0}, {-1, 0}, {2, -1}, {-1, 2}}},
                              K{{{0, 0}, {-2, 0}, {1, 0}, {-2, 1}, {1, -2}}},
                              K{{{0, 0}, {1, 0}, {-2, 0}, {1, 2}, {-2, -1}}},
                              K{{{0, 0}, {-1, 0}, {2, 0}, {-1, -2}, {2, 1}}}}};
    const std::array<unsigned, 8> from{0, 1, 1, 2, 2, 3, 3, 0}, to{1, 0, 2, 1, 3, 2, 0, 3};
    for (unsigned k = 0; k < 8; ++k) {
        REQUIRE(kick_offsets(Piece::I,
                             static_cast<std::uint8_t>(from[k]),
                             static_cast<std::uint8_t>(to[k])) == i[k]);
        for (auto p : {Piece::J, Piece::L, Piece::S, Piece::T, Piece::Z})
            REQUIRE(kick_offsets(p,
                                 static_cast<std::uint8_t>(from[k]),
                                 static_cast<std::uint8_t>(to[k])) == j[k]);
    }
}
TEST_CASE("Authored single double triple spin routes run from actual input") {
    for (auto preset : {Preset::fusion, Preset::modern})
        for (unsigned rows = 1; rows <= 3; ++rows) {
            auto s = spin_fixture(7, preset, rows);
            REQUIRE(valid_state(s));
            Events e;
            REQUIRE(step(s, clockwise, &e));
            REQUIRE(e.rotated);
            REQUIRE(s.active.rotation == 1);
            REQUIRE(s.active.x == 3);
            REQUIRE(s.active.y == 17);
            REQUIRE(s.last_kick == 4);
            const unsigned delay = 30u;
            for (unsigned tick = 1; tick < delay; ++tick) {
                REQUIRE(step(s, 0, &e));
                if (tick + 1 < delay)
                    REQUIRE_FALSE(e.locked);
            }
            REQUIRE(e.locked);
            REQUIRE(e.spin);
            REQUIRE(e.cleared == rows);
            REQUIRE(e.attack == 2 * rows);
            REQUIRE(s.score == (preset == Preset::fusion ? 800 + 800 * rows : 400 + 400 * rows));
            REQUIRE_FALSE(e.perfect_clear);
        }
}
TEST_CASE("Hold drop and ghost work in both presets") {
    auto s = initial_state(1);
    const auto first = s.active.piece;
    const auto next = s.next[0];
    Events e;
    REQUIRE(step(s, hold | hard_drop, &e));
    REQUIRE(e.held);
    REQUIRE_FALSE(e.locked);
    REQUIRE(s.held_piece == first);
    REQUIRE(s.active.piece == next);
    REQUIRE(step(s, 0));
    REQUIRE(step(s, hold, &e));
    REQUIRE_FALSE(e.held);
    auto landing = ghost(s);
    REQUIRE(step(s, hard_drop, &e));
    REQUIRE(e.locked);
    REQUIRE_FALSE(s.hold_used);
    REQUIRE(landing.y >= 0);
    REQUIRE(s.score > 0);
    s = initial_state(1, Preset::fusion);
    const auto fusion_first = s.active.piece;
    const auto fusion_next = s.next[0];
    REQUIRE(step(s, hold | hard_drop, &e));
    REQUIRE(e.held);
    REQUIRE_FALSE(e.locked);
    REQUIRE(s.held_piece == fusion_first);
    REQUIRE(s.active.piece == fusion_next);
    REQUIRE(step(s, 0));
    REQUIRE(step(s, hard_drop, &e));
    REQUIRE_FALSE(e.hard_dropped);
    REQUIRE_FALSE(e.locked);
    REQUIRE(s.hold_used);
}
TEST_CASE("DAS preset timing and opposite directional ties") {
    for (auto p : {Preset::fusion, Preset::modern}) {
        auto s = initial_state(2, p);
        s.active = {Piece::O, 0, 3, 0};
        REQUIRE(step(s, left));
        REQUIRE(s.active.x == 2);
        const unsigned delay = p == Preset::fusion ? 16u : 10u;
        for (unsigned t = 1; t < delay; ++t) {
            REQUIRE(step(s, left));
            REQUIRE(s.active.x == 2);
        }
        REQUIRE(step(s, left));
        REQUIRE(s.active.x == 1);
        REQUIRE(step(s, left | right));
        REQUIRE(s.active.x == (p == Preset::fusion ? 1 : 2));
        REQUIRE(step(s, 0));
        REQUIRE(step(s, left | right));
        REQUIRE(s.active.x == (p == Preset::fusion ? 1 : 2));
    }
}
TEST_CASE("Garbage delay cap cancellation overflow and spawn collision") {
    auto s = initial_state(1);
    REQUIRE(enqueue(s, {1, 0, 30, 2, 12, 4}));
    s.active = {Piece::O, 0, 0, 18};
    Events e;
    REQUIRE(step(s, hard_drop, &e));
    REQUIRE(e.inserted == 0);
    s.tick = 30;
    REQUIRE(step(s, 0));
    REQUIRE(step(s, hard_drop, &e));
    REQUIRE(e.inserted == 8);
    REQUIRE(s.incoming[0].rows == 4);
    for (unsigned y = 32; y < 40; ++y)
        REQUIRE(s.board[y * 10 + 4] == 0);
    s = spin_fixture(1, Preset::modern, 3);
    REQUIRE(enqueue(s, {1, 0, 0, 2, 3, 0}));
    REQUIRE(enqueue(s, {2, 0, 100, 2, 5, 1}));
    REQUIRE(step(s, clockwise));
    REQUIRE(step(s, hard_drop, &e));
    REQUIRE(e.spin);
    REQUIRE(e.canceled == 6);
    REQUIRE(e.attack == 0);
    REQUIRE(e.inserted == 0);
    REQUIRE(s.incoming.size() == 1);
    REQUIRE(s.incoming[0].rows == 2);
    s = initial_state(1);
    s.board[0] = 8;
    s.active = {Piece::O, 0, 0, 18};
    REQUIRE(enqueue(s, {1, 0, 0, 2, 1, 3}));
    REQUIRE(step(s, hard_drop, &e));
    REQUIRE(s.game_over);
    REQUIRE(e.topped_out);
}
TEST_CASE("Back to back combos perfect clear and nonspin lateral invalidation") {
    auto s = spin_fixture(1, Preset::modern, 2);
    s.back_to_back = true;
    s.combo = 0;
    REQUIRE(step(s, clockwise));
    Events e;
    REQUIRE(step(s, hard_drop, &e));
    REQUIRE(e.back_to_back);
    REQUIRE(s.score == 1800);
    REQUIRE(e.attack == 5);
    REQUIRE(s.combo == 1);
    s = initial_state(1);
    s.board.fill(0);
    for (unsigned x = 0; x < 10; ++x)
        if (x < 3 || x > 6)
            s.board[390 + x] = 8;
    s.active = {Piece::I, 0, 3, 18};
    REQUIRE(step(s, hard_drop, &e));
    REQUIRE(e.cleared == 1);
    REQUIRE(e.perfect_clear);
    REQUIRE(e.attack == 6);
    REQUIRE(s.score == 2100);
    s = initial_state(1);
    s.last_rotation = true;
    REQUIRE(step(s, left));
    REQUIRE_FALSE(s.last_rotation);
    s.last_rotation = true;
    REQUIRE(step(s, down));
    REQUIRE(s.last_rotation);
}
TEST_CASE("Invalid modern inputs and counters reject unchanged") {
    auto s = initial_state(1);
    auto before = s;
    REQUIRE_FALSE(step(s, 256));
    REQUIRE(s == before);
    s.tick = std::numeric_limits<std::uint64_t>::max();
    before = s;
    REQUIRE_FALSE(step(s, 0));
    REQUIRE(s == before);
    auto bytes = canonical_bytes(s);
    bytes[4] = 255;
    State decoded;
    REQUIRE_FALSE(decode_state(bytes, decoded));
}
TEST_CASE("Every kick candidate can be forced in order by independently constructed obstacles") {
    for (auto piece : {Piece::I, Piece::J, Piece::L, Piece::S, Piece::T, Piece::Z})
        for (unsigned from = 0; from < 4; ++from)
            for (bool cw : {false, true})
                for (unsigned wanted = 0; wanted < 5; ++wanted) {
                    bool found = false;
                    const auto to = static_cast<std::uint8_t>((from + (cw ? 1 : 3)) % 4);
                    const auto offsets = kick_offsets(piece, static_cast<std::uint8_t>(from), to);
                    for (int y = -18; y <= 18 && !found; ++y)
                        for (int x = -3; x < 10 && !found; ++x) {
                            auto s = initial_state(9);
                            s.active = {piece, static_cast<std::uint8_t>(from), x, y};
                            if (collides(s, s.active))
                                continue;
                            auto target = s.active;
                            target.rotation = to;
                            target.x += offsets[wanted].x;
                            target.y += offsets[wanted].y;
                            if (collides(s, target))
                                continue;
                            const auto current_cells = cells(s.active),
                                       target_cells = cells(target);
                            bool possible = true;
                            for (unsigned earlier = 0; earlier < wanted; ++earlier) {
                                auto candidate = s.active;
                                candidate.rotation = to;
                                candidate.x += offsets[earlier].x;
                                candidate.y += offsets[earlier].y;
                                if (collides(s, candidate))
                                    continue;
                                bool blocked = false;
                                for (auto c : cells(candidate)) {
                                    if (std::find(current_cells.begin(), current_cells.end(), c) ==
                                            current_cells.end() &&
                                        std::find(target_cells.begin(), target_cells.end(), c) ==
                                            target_cells.end()) {
                                        s.board[static_cast<std::size_t>((c.y + 20) * 10 + c.x)] =
                                            8;
                                        blocked = true;
                                        break;
                                    }
                                }
                                if (!blocked) {
                                    possible = false;
                                    break;
                                }
                            }
                            if (!possible)
                                continue;
                            REQUIRE(rotate(s, cw));
                            REQUIRE(s.last_kick == wanted);
                            REQUIRE(s.active == target);
                            found = true;
                        }
                    INFO("piece " << static_cast<unsigned>(piece) << " from " << from << " CW "
                                  << cw << " index " << wanted);
                    if (!found) {
                        REQUIRE(piece == Piece::T);
                        REQUIRE(((from == 0 && wanted == 3) || (from == 2 && wanted == 2)));
                    }
                }
}
TEST_CASE("Simultaneous rotations cancel and blocked rotation preserves marker") {
    auto s = initial_state(1);
    const auto original = s.active;
    Events e;
    REQUIRE(step(s, clockwise | counterclockwise, &e));
    REQUIRE_FALSE(e.rotated);
    REQUIRE(s.active == original);
    s = initial_state(1);
    s.active = {Piece::T, 0, 3, 0};
    s.last_rotation = true;
    s.last_kick = 3;
    const auto active_cells = cells(s.active);
    s.board.fill(8);
    for (auto c : active_cells)
        s.board[static_cast<std::size_t>((c.y + 20) * 10 + c.x)] = 0;
    const auto before = s;
    REQUIRE_FALSE(rotate(s, true));
    REQUIRE(s == before);
}
TEST_CASE("Modern grounded manipulation reset caps persist and force a lock") {
    for (auto preset : {Preset::modern}) {
        auto s = initial_state(1, preset);
        s.active = {Piece::T, 0, 3, 18};
        Events e;
        const unsigned cap = 15u, delay = 30u;
        REQUIRE(step(s, 0));
        for (unsigned n = 0; n < cap; ++n) {
            REQUIRE(step(s, n % 2 ? left : right, &e));
            REQUIRE_FALSE(e.locked);
            REQUIRE(s.lock_elapsed == 1);
            REQUIRE(s.lock_resets == n + 1);
        }
        for (unsigned t = 1; t < delay; ++t) {
            REQUIRE(step(s, t % 2 ? left : right, &e));
            if (t + 1 < delay)
                REQUIRE_FALSE(e.locked);
        }
        REQUIRE(e.locked);
    }
}
TEST_CASE("Max valid state roundtrips and malformed timer geometry enums reject") {
    auto s = initial_state(1);
    for (std::uint64_t id = 1; id <= 4096; ++id)
        s.received_ids.push_back(id);
    for (std::uint64_t id = 1; id <= 256; ++id)
        s.incoming.push_back({id, 0, 30, 2, 1024, 9});
    REQUIRE(valid_state(s));
    auto bytes = canonical_bytes(s);
    REQUIRE(bytes.size() < 50000);
    State restored;
    REQUIRE(decode_state(bytes, restored));
    REQUIRE(s == restored);
    REQUIRE(enqueue(s, {1, 0, 30, 2, 1, 0}));
    REQUIRE_FALSE(enqueue(s, {4097, 0, 30, 2, 1, 0}));
    s = initial_state(1);
    s.gravity_timer = 61;
    REQUIRE_FALSE(valid_state(s));
    s = initial_state(1);
    s.active.x = std::numeric_limits<std::int32_t>::max();
    REQUIRE_FALSE(valid_state(s));
    s = initial_state(1);
    s.active.piece = static_cast<Piece>(9);
    REQUIRE_FALSE(valid_state(s));
}
TEST_CASE("Hidden lock loss and hidden row clear rescue are distinct") {
    auto s = initial_state(1);
    s.active = {Piece::I, 0, 3, -3};
    s.board[190 + 3] = 8;
    s.lock_elapsed = 29;
    Events e;
    REQUIRE(step(s, 0, &e));
    REQUIRE(e.locked);
    REQUIRE(s.game_over);
    s = initial_state(1);
    s.active = {Piece::I, 0, 3, -2};
    for (unsigned x = 0; x < 10; ++x)
        if (x < 3 || x > 6)
            s.board[190 + x] = 8;
    s.board[200 + 3] = 8;
    s.lock_elapsed = 29;
    REQUIRE(step(s, 0, &e));
    REQUIRE(e.cleared == 1);
    REQUIRE_FALSE(s.game_over);
}

TEST_CASE("Authored JSON triple fixture board and input ticks execute in real core") {
    std::ifstream file(std::string(STACKPULSE_SOURCE_DIR) +
                       "/tests/fixtures/rotation/t-spin-triple-v1.json");
    REQUIRE(file.good());
    std::string text((std::istreambuf_iterator<char>(file)), {});
    const auto board_start = text.find("\"board\"");
    const auto board_end = text.find("\"active\"", board_start);
    REQUIRE(board_start != std::string::npos);
    REQUIRE(board_end != std::string::npos);
    auto board_text = text.substr(board_start, board_end - board_start);
    std::regex integer("[0-9]+");
    std::vector<unsigned> values;
    for (auto it = std::sregex_iterator(board_text.begin(), board_text.end(), integer);
         it != std::sregex_iterator();
         ++it)
        values.push_back(static_cast<unsigned>(std::stoul(it->str())));
    REQUIRE(values.size() == 400);
    auto s = spin_fixture(7, Preset::modern, 3);
    for (unsigned i = 0; i < 400; ++i)
        REQUIRE(s.board[i] == values[i]);
    auto begin = text.find("\"inputs\"");
    auto end = text.find("\"kick_tests\"", begin);
    auto input = text.substr(begin, end - begin);
    std::regex record("\\\"tick\\\"\\s*:\\s*([0-9]+)\\s*,\\s*\\\"held\\\"\\s*:\\s*([0-9]+)");
    Events e;
    unsigned ticks = 0;
    for (auto it = std::sregex_iterator(input.begin(), input.end(), record);
         it != std::sregex_iterator();
         ++it) {
        REQUIRE(std::stoul((*it)[1].str()) == s.tick);
        REQUIRE(step(s, static_cast<std::uint32_t>(std::stoul((*it)[2].str())), &e));
        ++ticks;
    }
    REQUIRE(ticks == 30);
    REQUIRE(e.locked);
    REQUIRE(e.spin);
    REQUIRE(e.cleared == 3);
    REQUIRE(e.attack == 6);
    REQUIRE(s.score == 1600);
    REQUIRE(s.board[384] == 8);
}

TEST_CASE("Every battle reward clear zero chains breaks combo cap and score saturation") {
    constexpr std::array<unsigned, 5> scores{0, 100, 300, 500, 800}, attacks{0, 0, 1, 2, 4};
    for (unsigned rows = 1; rows <= 4; ++rows) {
        auto s = initial_state(1);
        for (unsigned r = 0; r < rows; ++r)
            for (unsigned x = 0; x < 10; ++x)
                if (x != 4)
                    s.board[(39 - r) * 10 + x] = 8;
        s.active = {Piece::I, 1, 2, 16};
        s.board[350] = 8;
        Events e;
        REQUIRE(step(s, hard_drop, &e));
        REQUIRE(e.cleared == rows);
        REQUIRE(s.score == scores[rows]);
        REQUIRE(e.attack == attacks[rows]);
        REQUIRE(s.combo == 0);
        REQUIRE(s.back_to_back == (rows == 4));
    }
    auto s = spin_fixture(1, Preset::modern, 1);
    s.board[390] = 0;
    s.back_to_back = true;
    s.combo = 4;
    REQUIRE(step(s, clockwise));
    Events e;
    REQUIRE(step(s, hard_drop, &e));
    REQUIRE(e.spin);
    REQUIRE(e.cleared == 0);
    REQUIRE(s.score == 400);
    REQUIRE(e.attack == 0);
    REQUIRE(s.back_to_back);
    REQUIRE(s.combo == -1);
    s = initial_state(1);
    for (unsigned x = 0; x < 10; ++x)
        if (x != 4)
            s.board[390 + x] = 8;
    s.active = {Piece::I, 1, 2, 16};
    s.back_to_back = true;
    s.combo = 99;
    s.score = std::numeric_limits<std::uint64_t>::max() - 50;
    REQUIRE(step(s, hard_drop, &e));
    REQUIRE(s.score == std::numeric_limits<std::uint64_t>::max());
    REQUIRE_FALSE(s.back_to_back);
    REQUIRE(e.attack == 4);
}
TEST_CASE("Ineligible oldest packets do not block eligible insert and spawn collision loses") {
    auto s = initial_state(1);
    s.tick = 1;
    s.active = {Piece::O, 0, 0, 18};
    REQUIRE(enqueue(s, {1, 0, 100, 2, 3, 1}));
    REQUIRE(enqueue(s, {2, 1, 1, 2, 2, 7}));
    Events e;
    REQUIRE(step(s, hard_drop, &e));
    REQUIRE(e.inserted == 2);
    REQUIRE(s.incoming.size() == 1);
    REQUIRE(s.incoming[0].id == 1);
    REQUIRE(s.board[397] == 0);
    s = initial_state(1);
    auto spawn = Active{s.next[0], 0, 3, -2};
    for (auto c : cells(spawn))
        s.board[static_cast<std::size_t>((c.y + 20) * 10 + c.x)] = 8;
    s.active = {Piece::O, 0, 0, 18};
    REQUIRE(step(s, hard_drop, &e));
    REQUIRE(s.game_over);
    REQUIRE(e.topped_out);
}
TEST_CASE("Modern live snapshot invariants reject full rows and duplicate packet IDs") {
    auto s = initial_state(1);
    std::fill_n(s.board.begin() + 390, 10, std::uint8_t{8});
    REQUIRE_FALSE(valid_state(s));
    s = initial_state(1);
    REQUIRE(enqueue(s, {1, 0, 30, 2, 1, 2}));
    s.incoming.push_back({1, 1, 31, 2, 1, 2});
    REQUIRE_FALSE(valid_state(s));
}
TEST_CASE("One hundred seeds resume identical modern and Fusion traces") {
    for (unsigned seed = 1; seed <= 100; ++seed) {
        auto a = initial_state(seed, seed % 2 ? Preset::fusion : Preset::modern);
        State b;
        REQUIRE(decode_state(canonical_bytes(a), b));
        for (unsigned t = 0; t < 1000; ++t) {
            const std::uint32_t held = t % 31 == 0   ? clockwise
                                       : t % 7 == 0  ? down
                                       : t % 43 == 0 ? left
                                                     : 0;
            REQUIRE(step(a, held));
            REQUIRE(step(b, held));
        }
        REQUIRE(a == b);
        REQUIRE(state_hash(a) == state_hash(b));
    }
}

TEST_CASE("Soft drop transitions never spend stored gravity as a burst") {
    for (auto preset : {Preset::fusion, Preset::modern})
        for (unsigned start = 0; start <= (preset == Preset::fusion ? 29u : 20u); ++start) {
            auto base = initial_state(1, preset, preset == Preset::fusion ? start : 0);
            if (preset == Preset::modern)
                base.lines = start * 10;
            base.active = {Piece::O, 0, 3, 0};
            const auto interval = gravity_interval(base);
            for (unsigned phase = 0; phase < interval; ++phase) {
                auto s = base;
                s.gravity_timer = phase;
                for (unsigned tick = 0; tick < 8; ++tick) {
                    const auto before = s.active.y;
                    const auto score = s.score;
                    Events e;
                    REQUIRE(step(s, tick % 3 == 1 ? 0u : down, &e));
                    REQUIRE(s.active.y >= before);
                    REQUIRE(s.active.y <= before + 1);
                    REQUIRE(s.score <= score + 1);
                    REQUIRE_FALSE(e.locked);
                    REQUIRE_FALSE(e.hard_dropped);
                }
            }
        }
}
TEST_CASE("Fusion draws the Classic stream with one next and visible spawns") {
    constexpr std::array<Piece, 7> mapping{
        Piece::I, Piece::O, Piece::T, Piece::S, Piece::Z, Piece::J, Piece::L};
    for (unsigned seed : {0u, 1u, 7u, 49u, 0xffffffffu}) {
        auto classic = stackpulse::classic::initial_state(seed);
        auto s = initial_state(seed, Preset::fusion);
        for (unsigned n = 0; n < 100; ++n) {
            REQUIRE(s.active.piece == mapping[static_cast<unsigned>(classic.active.piece)]);
            REQUIRE(s.next[0] == mapping[static_cast<unsigned>(classic.next)]);
            REQUIRE(s.rng == classic.rng);
            for (unsigned i = 1; i < s.next.size(); ++i)
                REQUIRE(s.next[i] == Piece::I);
            for (auto c : cells(s.active))
                REQUIRE(c.y >= 0);
            // Remove stack after each actual lock so stream comparison reaches 100 draws.
            s.board.fill(0);
            s.active = ghost(s);
            s.gravity_timer = gravity_interval(s) - 1;
            REQUIRE(step(s, down));
            s.board.fill(0);
            REQUIRE(step(s, 0));
            classic.board.fill(0);
            classic.active.y = 15;
            stackpulse::classic::Events e;
            do {
                REQUIRE(stackpulse::classic::step(classic, stackpulse::classic::down, &e));
            } while (!e.spawned);
            REQUIRE_FALSE(classic.game_over);
        }
    }
}
TEST_CASE("Fusion level thresholds and speed match Classic for every start level") {
    for (unsigned start = 0; start <= 29; ++start)
        for (unsigned lines = 0; lines <= 400; ++lines) {
            auto s = initial_state(1, Preset::fusion, start);
            s.lines = lines;
            const auto expected = stackpulse::classic::level_for_lines(start, lines);
            REQUIRE(level(s) == expected);
            REQUIRE(gravity_interval(s) == stackpulse::classic::gravity_frames(expected));
        }
}
TEST_CASE("Fusion every ordinary piece locks on the Classic downward attempt") {
    for (auto piece : {Piece::I, Piece::J, Piece::L, Piece::O, Piece::S, Piece::T, Piece::Z}) {
        auto s = initial_state(1, Preset::fusion);
        s.active = {piece, 0, 3, 0};
        REQUIRE_FALSE(spin_possible(s));
        s.active = ghost(s);
        REQUIRE_FALSE(spin_possible(s));
        s.last_rotation = true;
        s.gravity_timer = gravity_interval(s) - 1;
        Events e;
        REQUIRE(step(s, 0, &e));
        REQUIRE(e.locked);
        REQUIRE(e.spawned);
        REQUIRE_FALSE(e.hard_dropped);
    }
}
TEST_CASE("Fusion Classic clear values and all clear bonus scale with the level") {
    constexpr std::array<unsigned, 5> points{0, 40, 100, 300, 1200};
    for (unsigned start : {0u, 5u, 19u})
        for (unsigned rows = 1; rows <= 4; ++rows) {
            auto s = initial_state(1, Preset::fusion, start);
            for (unsigned r = 0; r < rows; ++r)
                for (unsigned x = 0; x < 10; ++x)
                    if (x != 4)
                        s.board[(39 - r) * 10 + x] = 8;
            s.board[350] = 8;
            s.active = {Piece::I, 1, 2, 16};
            Events e;
            s.active = ghost(s);
            s.gravity_timer = gravity_interval(s) - 1;
            REQUIRE(step(s, 0, &e));
            REQUIRE(e.cleared == rows);
            REQUIRE(s.score == points[rows] * (start + 1));
        }
    for (auto preset : {Preset::modern, Preset::fusion}) {
        auto s = initial_state(1, preset);
        for (unsigned x = 0; x < 10; ++x)
            if (x < 3 || x > 6)
                s.board[390 + x] = 8;
        s.active = {Piece::I, 0, 3, 18};
        Events e;
        s.gravity_timer = gravity_interval(s) - 1;
        REQUIRE(step(s, preset == Preset::fusion ? 0u : hard_drop, &e));
        REQUIRE(e.perfect_clear);
        REQUIRE(s.score == (preset == Preset::fusion ? 2040 : 2100));
    }
}
TEST_CASE("Version three snapshots retain Fusion start level hold and timers") {
    auto s = initial_state(49, Preset::fusion, 19);
    REQUIRE(step(s, hold));
    REQUIRE(step(s, left));
    REQUIRE(enqueue(s, {19, s.tick, s.tick + 30, 2, 5, 8}));
    auto bytes = canonical_bytes(s);
    REQUIRE(bytes[0] == 3);
    State restored;
    REQUIRE(decode_state(bytes, restored));
    REQUIRE(restored == s);
    REQUIRE(restored.start_level == 19);
    for (auto older : {1u, 2u}) {
        bytes[0] = static_cast<std::uint8_t>(older);
        REQUIRE_FALSE(decode_state(bytes, restored));
        REQUIRE(restored == s);
    }
    s.start_level = 30;
    REQUIRE_FALSE(valid_state(s));
}

TEST_CASE("Modern repeat cadence leaves three ticks between horizontal shifts") {
    auto s = initial_state(1);
    s.active = {Piece::O, 0, 4, 0};
    REQUIRE(step(s, left));
    for (unsigned tick = 1; tick <= 16; ++tick) {
        Events e;
        REQUIRE(step(s, left, &e));
        REQUIRE(e.shifted == (tick == 10 || tick == 13 || tick == 16));
    }
}
TEST_CASE("Modern hard drop is edge triggered and awards two points per descended row") {
    for (auto preset : {Preset::modern}) {
        auto s = initial_state(1, preset);
        s.active = {Piece::O, 0, 3, 0};
        const auto distance = ghost(s).y - s.active.y;
        Events e;
        REQUIRE(step(s, hard_drop, &e));
        REQUIRE(e.hard_dropped);
        REQUIRE(e.locked);
        REQUIRE(s.score == static_cast<std::uint64_t>(distance * 2));
        REQUIRE(e.score_delta == s.score);
        REQUIRE(step(s, hard_drop, &e));
        REQUIRE_FALSE(e.hard_dropped);
        REQUIRE_FALSE(e.locked);
        REQUIRE(step(s, 0));
        REQUIRE(step(s, hard_drop, &e));
        REQUIRE(e.hard_dropped);
        REQUIRE(e.locked);
    }
}

TEST_CASE("Fusion grounded non T waits a full Classic gravity interval in a valid state") {
    for (bool landing_first : {false, true}) {
        auto s = initial_state(1, Preset::fusion);
        s.active = {Piece::O, 0, 3, landing_first ? 17 : 18};
        Events e;
        const auto interval = gravity_interval(s);
        if (landing_first) {
            s.gravity_timer = interval - 1;
            REQUIRE(step(s, 0, &e));
            REQUIRE(s.active.y == 18);
            REQUIRE_FALSE(e.locked);
            REQUIRE(valid_state(s));
        }
        for (unsigned tick = 1; tick <= interval; ++tick) {
            REQUIRE(step(s, 0, &e));
            REQUIRE(valid_state(s));
            REQUIRE(e.locked == (tick == interval));
        }
    }
}
TEST_CASE("Fusion O gravity and lock trajectory matches Classic exactly") {
    for (unsigned start : {0u, 9u, 19u, 29u}) {
        auto s = initial_state(1, Preset::fusion, start);
        auto c = stackpulse::classic::initial_state(1, start);
        s.active = {Piece::O, 0, 3, 0};
        c.active = {stackpulse::classic::Piece::O, 0, 3, 0};
        for (unsigned tick = 0; tick < 1000; ++tick) {
            Events modern_event;
            stackpulse::classic::Events classic_event;
            REQUIRE(step(s, 0, &modern_event));
            REQUIRE(stackpulse::classic::step(c, 0, &classic_event));
            REQUIRE(modern_event.locked == classic_event.locked);
            if (modern_event.locked)
                break;
            REQUIRE(s.active.y == c.active.y);
            REQUIRE(s.gravity_timer == c.gravity_timer);
            REQUIRE(tick + 1 < 1000);
        }
    }
}

TEST_CASE("Fusion wall charge tuck retries and matches Classic every active tick") {
    auto s = initial_state(1, Preset::fusion, 15);
    auto c = stackpulse::classic::initial_state(1, 15);
    for (const Cell cell : std::array<Cell, 4>{{{1, 17}, {0, 18}, {1, 18}, {0, 19}}}) {
        s.board[(cell.y + 20) * 10 + cell.x] = 8;
        c.board[cell.y * 10 + cell.x] = 5;
    }
    s.active = {Piece::J, 3, 2, 16};
    c.active = {stackpulse::classic::Piece::J, 3, 2, 16};
    s.held = left;
    c.held = stackpulse::classic::left;
    s.direction = c.direction = -1;
    s.das_timer = c.das_timer = 16;
    s.gravity_timer = c.gravity_timer = 3;
    for (unsigned tick = 0; tick < 5; ++tick) {
        Events e;
        stackpulse::classic::Events ce;
        REQUIRE(step(s, left, &e));
        REQUIRE(stackpulse::classic::step(c, stackpulse::classic::left, &ce));
        REQUIRE(e.shifted == ce.shifted);
        REQUIRE(e.locked == ce.locked);
        REQUIRE(s.das_timer == c.das_timer);
        if (!e.locked) {
            REQUIRE(s.active.x == c.active.x);
            REQUIRE(s.active.y == c.active.y);
        }
        if (tick == 0)
            REQUIRE(s.das_timer == 16);
        if (tick == 1)
            REQUIRE(s.das_timer == 10);
    }
}
TEST_CASE("Fusion spawn and hold use Classic flipped TJL geometry") {
    for (auto piece : {Piece::T, Piece::J, Piece::L}) {
        auto s = initial_state(1, Preset::fusion);
        s.next[0] = piece;
        s.gravity_timer = 24;
        s.lock_elapsed = 12;
        s.lock_resets = 7;
        s.last_rotation = true;
        s.last_kick = 4;
        Events e;
        REQUIRE(step(s, hold, &e));
        REQUIRE(e.held);
        REQUIRE(s.active.rotation == 2);
        REQUIRE(s.active.piece == piece);
        REQUIRE(s.gravity_timer == 0);
        REQUIRE(s.lock_elapsed == 0);
        REQUIRE(s.lock_resets == 0);
        REQUIRE_FALSE(s.last_rotation);
        REQUIRE(s.last_kick == 0);
        REQUIRE(spawn_rotation(piece, Preset::modern) == 0);
        auto actual = cells(s.active);
        const auto cp = piece == Piece::T   ? stackpulse::classic::Piece::T
                        : piece == Piece::J ? stackpulse::classic::Piece::J
                                            : stackpulse::classic::Piece::L;
        auto expected =
            stackpulse::classic::cells({cp, stackpulse::classic::spawn_rotation(cp), 3, 0});
        // Classic flipped shapes occupy the same translated local geometry.
        auto sort = [](auto& cells) {
            std::sort(cells.begin(), cells.end(), [](auto a, auto b) {
                return a.y < b.y || (a.y == b.y && a.x < b.x);
            });
        };
        sort(actual);
        sort(expected);
        for (unsigned i = 0; i < 4; ++i) {
            REQUIRE(actual[i].x == expected[i].x);
            REQUIRE(actual[i].y == expected[i].y);
        }
    }
}
TEST_CASE("Fusion ignores hard drop through presses holds and release") {
    auto a = initial_state(1, Preset::fusion), b = a;
    for (unsigned tick = 0; tick < 100; ++tick) {
        Events e;
        REQUIRE(step(a, tick % 3 ? hard_drop : 0u, &e));
        REQUIRE(step(b, 0));
        REQUIRE_FALSE(e.hard_dropped);
        auto compare = a;
        compare.held = b.held;
        REQUIRE(compare == b);
    }
}
TEST_CASE("Fusion spin window follows actual CW CCW geometry and disappears outside cavity") {
    for (bool cw : {false, true}) {
        auto s = spin_fixture(7, Preset::fusion, 2);
        if (!cw) {
            auto board = s.board;
            s.board.fill(0);
            for (int y = 0; y < 40; ++y)
                for (int x = 0; x < 10; ++x)
                    s.board[y * 10 + 9 - x] = board[y * 10 + x];
            s.active.x = 3;
        }
        REQUIRE(spin_possible(s));
        Events e;
        REQUIRE(step(s, cw ? clockwise : counterclockwise, &e));
        REQUIRE(e.rotated);
        REQUIRE(s.last_kick == 4);
        REQUIRE(spin_possible(s));
        REQUIRE_FALSE(e.locked);
        s.active = {Piece::T, 0, 0, 0};
        REQUIRE_FALSE(spin_possible(s));
        REQUIRE(step(s, 0));
        REQUIRE(s.lock_elapsed == 0);
    }
}
TEST_CASE("Fusion blocked unrotated T wedge does not gain a spin window") {
    auto s = initial_state(1, Preset::fusion);
    s.active = {Piece::T, 0, 3, 18};
    // Fill every cell in the local rotation envelope except the active piece,
    // preserving holes elsewhere in each row so this is a valid live board.
    const auto active_cells = cells(s.active);
    for (int y = 15; y < 20; ++y)
        for (int x = 1; x <= 7; ++x) {
            Cell c{x, y};
            if (std::find(active_cells.begin(), active_cells.end(), c) == active_cells.end())
                s.board[(y + 20) * 10 + x] = 8;
        }
    REQUIRE(valid_state(s));
    REQUIRE_FALSE(spin_possible(s));
    auto copy = s;
    REQUIRE_FALSE(rotate(copy, true));
    REQUIRE_FALSE(rotate(copy, false));
    s.gravity_timer = gravity_interval(s) - 1;
    Events e;
    REQUIRE(step(s, clockwise, &e));
    REQUIRE_FALSE(e.rotated);
    REQUIRE(e.locked);
    REQUIRE_FALSE(e.spin);
}

TEST_CASE("Fusion moving out of a potential spin cavity restores immediate locking") {
    auto s = spin_fixture(7, Preset::fusion, 2);
    REQUIRE(spin_possible(s));
    s.lock_elapsed = 20;
    s.gravity_timer = gravity_interval(s) - 1;
    Events e;
    REQUIRE(step(s, right, &e));
    REQUIRE(e.shifted);
    REQUIRE(e.locked);
    REQUIRE_FALSE(e.spin);
}
TEST_CASE("Fusion completed spin respects the delay cap and failed actions cannot reset it") {
    auto s = spin_fixture(7, Preset::fusion, 3);
    REQUIRE(step(s, clockwise));
    REQUIRE(spin_possible(s));
    s.lock_resets = 15;
    s.lock_elapsed = 29;
    Events e;
    REQUIRE(step(s, left, &e));
    REQUIRE_FALSE(e.shifted);
    REQUIRE(e.locked);
    REQUIRE(e.spin);
    REQUIRE(e.cleared == 3);
}
