#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <fstream>
#include <limits>
#include <regex>
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
            const unsigned delay = preset == Preset::modern ? 30u : 18u;
            for (unsigned tick = 1; tick < delay; ++tick) {
                REQUIRE(step(s, 0, &e));
                if (tick + 1 < delay)
                    REQUIRE_FALSE(e.locked);
            }
            REQUIRE(e.locked);
            REQUIRE(e.spin);
            REQUIRE(e.cleared == rows);
            REQUIRE(e.attack == 2 * rows);
            REQUIRE(s.score == 400 + 400 * rows);
            REQUIRE_FALSE(e.perfect_clear);
        }
}
TEST_CASE("Hold drop ghost and Fusion restrictions") {
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
    auto before = s.active;
    REQUIRE(step(s, hold | hard_drop, &e));
    REQUIRE_FALSE(e.held);
    REQUIRE_FALSE(e.locked);
    REQUIRE(s.active == before);
}
TEST_CASE("DAS preset timing and opposite directional ties") {
    for (auto p : {Preset::fusion, Preset::modern}) {
        auto s = initial_state(2, p);
        s.active = {Piece::O, 0, 3, 0};
        REQUIRE(step(s, left));
        REQUIRE(s.active.x == 2);
        const unsigned delay = p == Preset::fusion ? 16u : 6u;
        for (unsigned t = 1; t < delay; ++t) {
            REQUIRE(step(s, left));
            REQUIRE(s.active.x == 2);
        }
        REQUIRE(step(s, left));
        REQUIRE(s.active.x == 1);
        REQUIRE(step(s, left | right));
        REQUIRE(s.active.x == 2);
        REQUIRE(step(s, 0));
        REQUIRE(step(s, left | right));
        REQUIRE(s.active.x == 2);
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
    REQUIRE(s.score == 100);
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
TEST_CASE("Grounded manipulation reset caps persist and force a lock") {
    for (auto preset : {Preset::fusion, Preset::modern}) {
        auto s = initial_state(1, preset);
        s.active = {Piece::O, 0, 3, 18};
        Events e;
        const unsigned cap = preset == Preset::fusion ? 8u : 15u,
                       delay = preset == Preset::fusion ? 18u : 30u;
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
