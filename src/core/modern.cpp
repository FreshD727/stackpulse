#include <algorithm>
#include <limits>
#include <stackpulse/modern.hpp>
namespace stackpulse::modern {
namespace {
using Shape = std::array<Cell, 4>;
constexpr std::array<std::array<Shape, 4>, 7> shapes{{{{{{{0, 1}, {1, 1}, {2, 1}, {3, 1}}},
                                                        {{{2, 0}, {2, 1}, {2, 2}, {2, 3}}},
                                                        {{{0, 2}, {1, 2}, {2, 2}, {3, 2}}},
                                                        {{{1, 0}, {1, 1}, {1, 2}, {1, 3}}}}},
                                                      {{{{{0, 0}, {0, 1}, {1, 1}, {2, 1}}},
                                                        {{{1, 0}, {2, 0}, {1, 1}, {1, 2}}},
                                                        {{{0, 1}, {1, 1}, {2, 1}, {2, 2}}},
                                                        {{{1, 0}, {1, 1}, {0, 2}, {1, 2}}}}},
                                                      {{{{{2, 0}, {0, 1}, {1, 1}, {2, 1}}},
                                                        {{{1, 0}, {1, 1}, {1, 2}, {2, 2}}},
                                                        {{{0, 1}, {1, 1}, {2, 1}, {0, 2}}},
                                                        {{{0, 0}, {1, 0}, {1, 1}, {1, 2}}}}},
                                                      {{{{{1, 0}, {2, 0}, {1, 1}, {2, 1}}},
                                                        {{{1, 0}, {2, 0}, {1, 1}, {2, 1}}},
                                                        {{{1, 0}, {2, 0}, {1, 1}, {2, 1}}},
                                                        {{{1, 0}, {2, 0}, {1, 1}, {2, 1}}}}},
                                                      {{{{{1, 0}, {2, 0}, {0, 1}, {1, 1}}},
                                                        {{{1, 0}, {1, 1}, {2, 1}, {2, 2}}},
                                                        {{{1, 1}, {2, 1}, {0, 2}, {1, 2}}},
                                                        {{{0, 0}, {0, 1}, {1, 1}, {1, 2}}}}},
                                                      {{{{{1, 0}, {0, 1}, {1, 1}, {2, 1}}},
                                                        {{{1, 0}, {1, 1}, {2, 1}, {1, 2}}},
                                                        {{{0, 1}, {1, 1}, {2, 1}, {1, 2}}},
                                                        {{{1, 0}, {0, 1}, {1, 1}, {1, 2}}}}},
                                                      {{{{{0, 0}, {1, 0}, {1, 1}, {2, 1}}},
                                                        {{{2, 0}, {1, 1}, {2, 1}, {1, 2}}},
                                                        {{{0, 1}, {1, 1}, {1, 2}, {2, 2}}},
                                                        {{{1, 0}, {0, 1}, {1, 1}, {0, 2}}}}}}};
std::uint32_t random(std::uint32_t& x) {
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    return x;
}
std::uint32_t bounded(std::uint32_t& x, std::uint32_t n) {
    const auto threshold = (0u - n) % n;
    std::uint32_t v;
    do {
        v = random(x);
    } while (v < threshold);
    return v % n;
}
std::uint32_t normalize(std::uint32_t x) {
    return x ? x : 0x6d2b79f5u;
}
Piece draw(State& s) {
    if (s.bag_cursor == 7) {
        for (unsigned i = 0; i < 7; ++i)
            s.bag[i] = static_cast<Piece>(i);
        for (unsigned i = 6; i > 0; --i)
            std::swap(s.bag[i], s.bag[bounded(s.rng, i + 1)]);
        s.bag_cursor = 0;
    }
    return s.bag[s.bag_cursor++];
}
Piece take(State& s) {
    const auto p = s.next[0];
    std::move(s.next.begin() + 1, s.next.end(), s.next.begin());
    s.next.back() = draw(s);
    return p;
}
bool occupied(const State& s, int x, int y) {
    return x < 0 || x >= 10 || y < -20 || y >= 20 ||
           s.board[static_cast<std::size_t>((y + 20) * 10 + x)] != 0;
}
bool grounded(const State& s) {
    auto a = s.active;
    ++a.y;
    return collides(s, a);
}
void add(std::uint64_t& a, std::uint64_t b) {
    a = b > std::numeric_limits<std::uint64_t>::max() - a
            ? std::numeric_limits<std::uint64_t>::max()
            : a + b;
}
void spawn(State& s, Piece p) {
    s.active = {p, 0, 3, -2};
    s.gravity_timer = 0;
    s.lock_elapsed = 0;
    s.lock_resets = 0;
    s.last_rotation = false;
    s.last_kick = 0;
    if (collides(s, s.active))
        s.game_over = true;
}
void insert(State& s, Events& e) {
    unsigned count = 0;
    for (auto& p : s.incoming) {
        while (p.rows && p.eligible <= s.tick && count < 8 && !s.game_over) {
            if (std::any_of(s.board.begin(), s.board.begin() + 10, [](auto c) { return c != 0; })) {
                s.game_over = true;
                break;
            }
            std::move(s.board.begin() + 10, s.board.end(), s.board.begin());
            std::fill(s.board.end() - 10, s.board.end(), std::uint8_t{8});
            s.board[390 + p.hole] = 0;
            --p.rows;
            ++count;
        }
    }
    std::erase_if(s.incoming, [](const auto& p) { return p.rows == 0; });
    e.inserted = count;
}
void lock(State& s, Events& e) {
    e.locked = true;
    const auto placed = cells(s.active);
    if (s.active.piece == Piece::T && s.last_rotation) {
        unsigned corners = 0;
        for (int dy : {0, 2})
            for (int dx : {0, 2})
                corners += occupied(s, s.active.x + dx, s.active.y + dy) ? 1u : 0u;
        e.spin = corners >= 3;
    }
    for (auto c : placed)
        s.board[static_cast<std::size_t>((c.y + 20) * 10 + c.x)] =
            static_cast<std::uint8_t>(s.active.piece) + 1;
    std::array<bool, 40> full{};
    for (unsigned r = 0; r < 40; ++r) {
        full[r] = std::all_of(s.board.begin() + r * 10, s.board.begin() + (r + 1) * 10, [](auto c) {
            return c != 0;
        });
        e.cleared += full[r] ? 1u : 0u;
    }
    bool survives = false, visible = false;
    for (auto c : placed) {
        if (!full[static_cast<std::size_t>(c.y + 20)]) {
            survives = true;
            int shifted = c.y;
            for (int r = c.y + 21; r < 40; ++r)
                if (full[static_cast<std::size_t>(r)])
                    ++shifted;
            visible |= shifted >= 0;
        }
    }
    int dest = 39;
    for (int r = 39; r >= 0; --r)
        if (!full[static_cast<std::size_t>(r)]) {
            for (unsigned x = 0; x < 10; ++x)
                s.board[static_cast<std::size_t>(dest * 10) + x] =
                    s.board[static_cast<std::size_t>(r * 10) + x];
            --dest;
        }
    while (dest >= 0) {
        std::fill_n(s.board.begin() + dest * 10, 10, std::uint8_t{0});
        --dest;
    }
    constexpr std::array<unsigned, 5> normal_score{0, 100, 300, 500, 800},
        normal_attack{0, 0, 1, 2, 4};
    constexpr std::array<unsigned, 4> spin_score{400, 800, 1200, 1600}, spin_attack{0, 2, 4, 6};
    auto points =
        e.spin ? spin_score[std::min(e.cleared, 3u)] : normal_score[std::min(e.cleared, 4u)];
    e.attack =
        e.spin ? spin_attack[std::min(e.cleared, 3u)] : normal_attack[std::min(e.cleared, 4u)];
    if (e.cleared) {
        const bool qualifies = e.spin || e.cleared == 4;
        e.back_to_back = qualifies && s.back_to_back;
        if (e.back_to_back) {
            points = points * 3 / 2;
            ++e.attack;
        }
        s.back_to_back = qualifies;
        s.combo = std::min(s.combo + 1, 1000000);
        e.attack += static_cast<unsigned>(std::min(4, s.combo / 2));
        e.perfect_clear =
            std::all_of(s.board.begin(), s.board.end(), [](auto c) { return c == 0; });
        if (e.perfect_clear)
            e.attack += 6;
    } else
        s.combo = -1;
    add(s.score, points);
    e.score_delta += points;
    add(s.lines, e.cleared);
    for (auto& p : s.incoming) {
        const auto cancel = std::min<unsigned>(e.attack, p.rows);
        p.rows = static_cast<std::uint16_t>(p.rows - cancel);
        e.attack -= cancel;
        e.canceled += cancel;
    }
    std::erase_if(s.incoming, [](auto& p) { return p.rows == 0; });
    if (survives && !visible)
        s.game_over = true;
    if (!s.game_over) {
        insert(s, e);
        if (!s.game_over) {
            spawn(s, take(s));
            s.hold_used = false;
            e.spawned = true;
        }
    }
    e.topped_out = s.game_over;
}
} // namespace
std::array<Cell, 4> cells(const Active& a) noexcept {
    if (static_cast<unsigned>(a.piece) >= 7 || a.rotation >= 4)
        return {};
    auto out = shapes[static_cast<unsigned>(a.piece)][a.rotation];
    for (auto& c : out) {
        c.x += a.x;
        c.y += a.y;
    }
    return out;
}
bool collides(const State& s, const Active& a) noexcept {
    for (auto c : cells(a))
        if (occupied(s, c.x, c.y))
            return true;
    return false;
}
std::array<Cell, 5> kick_offsets(Piece p, std::uint8_t from, std::uint8_t to) noexcept {
    if (p == Piece::O)
        return {};
    using K = std::array<Cell, 5>;
    const bool cw = to == ((from + 1) % 4);
    if (p == Piece::I) {
        if (from == 0)
            return cw ? K{{{0, 0}, {-2, 0}, {1, 0}, {-2, 1}, {1, -2}}}
                      : K{{{0, 0}, {-1, 0}, {2, 0}, {-1, -2}, {2, 1}}};
        if (from == 1)
            return cw ? K{{{0, 0}, {-1, 0}, {2, 0}, {-1, -2}, {2, 1}}}
                      : K{{{0, 0}, {2, 0}, {-1, 0}, {2, -1}, {-1, 2}}};
        if (from == 2)
            return cw ? K{{{0, 0}, {2, 0}, {-1, 0}, {2, -1}, {-1, 2}}}
                      : K{{{0, 0}, {1, 0}, {-2, 0}, {1, 2}, {-2, -1}}};
        return cw ? K{{{0, 0}, {1, 0}, {-2, 0}, {1, 2}, {-2, -1}}}
                  : K{{{0, 0}, {-2, 0}, {1, 0}, {-2, 1}, {1, -2}}};
    }
    if (from == 1)
        return K{{{0, 0}, {1, 0}, {1, 1}, {0, -2}, {1, -2}}};
    if (from == 3)
        return K{{{0, 0}, {-1, 0}, {-1, 1}, {0, -2}, {-1, -2}}};
    const int d = (from == 0 ? cw : !cw) ? -1 : 1;
    return K{{{0, 0}, {d, 0}, {d, -1}, {0, 2}, {d, 2}}};
}
bool rotate(State& s, bool cw) noexcept {
    auto a = s.active;
    a.rotation = static_cast<std::uint8_t>((a.rotation + (cw ? 1 : 3)) % 4);
    const auto kicks = kick_offsets(a.piece, s.active.rotation, a.rotation);
    for (unsigned i = 0; i < (a.piece == Piece::O ? 1u : 5u); ++i) {
        auto candidate = a;
        candidate.x += kicks[i].x;
        candidate.y += kicks[i].y;
        if (!collides(s, candidate)) {
            s.active = candidate;
            s.last_rotation = true;
            s.last_kick = static_cast<std::uint8_t>(i);
            return true;
        }
    }
    return false;
}
State initial_state(std::uint32_t seed, Preset preset) {
    State s;
    s.preset = preset;
    s.rng = normalize(seed);
    s.garbage_rng = normalize(seed ^ 0xa5a5a5a5u);
    s.target_rng = normalize(seed ^ 0x3c6ef372u);
    const auto p = draw(s);
    for (auto& n : s.next)
        n = draw(s);
    spawn(s, p);
    return s;
}
Active ghost(const State& s) noexcept {
    auto a = s.active;
    if (collides(s, a))
        return a;
    for (;;) {
        auto b = a;
        ++b.y;
        if (collides(s, b))
            return a;
        a = b;
    }
}
std::uint32_t level(const State& s) noexcept {
    return static_cast<std::uint32_t>(std::min<std::uint64_t>(20, s.lines / 10));
}
bool enqueue(State& s, const Packet& p) {
    if (!p.id || !p.rows || p.rows > 1024 || p.hole >= 10 || p.eligible < p.creation)
        return false;
    if (std::binary_search(s.received_ids.begin(), s.received_ids.end(), p.id))
        return true;
    if (s.incoming.size() >= 256 || s.received_ids.size() >= 4096)
        return false;
    s.received_ids.insert(std::lower_bound(s.received_ids.begin(), s.received_ids.end(), p.id),
                          p.id);
    s.incoming.push_back(p);
    std::sort(s.incoming.begin(), s.incoming.end(), [](auto& a, auto& b) {
        return a.creation < b.creation || (a.creation == b.creation && a.id < b.id);
    });
    return true;
}
bool step(State& s, std::uint32_t held, Events* output) {
    if ((held & ~action_mask) || !valid_state(s) ||
        s.tick == std::numeric_limits<std::uint64_t>::max() ||
        s.gravity_timer == std::numeric_limits<std::uint64_t>::max() ||
        s.das_timer == std::numeric_limits<std::uint64_t>::max())
        return false;
    Events e;
    if (s.game_over) {
        s.held = held;
        ++s.tick;
        if (output)
            *output = e;
        return true;
    }
    const auto pressed = held & ~s.held;
    const auto old_score = s.score;
    s.held = held;
    if (s.preset == Preset::modern && (pressed & hold) && !s.hold_used) {
        const auto old = s.active.piece;
        const auto p = s.has_hold ? s.held_piece : take(s);
        s.held_piece = old;
        s.has_hold = true;
        s.hold_used = true;
        spawn(s, p);
        e.held = true;
        e.topped_out = s.game_over;
        ++s.tick;
        if (output)
            *output = e;
        return true;
    }
    const bool was_grounded = grounded(s);
    bool manipulation = false;
    if (((pressed & clockwise) != 0) != ((pressed & counterclockwise) != 0)) {
        e.rotated = rotate(s, (pressed & clockwise) != 0);
        manipulation = e.rotated;
    }
    int direction = 0;
    if ((held & (left | right)) == (left | right)) {
        if ((pressed & (left | right)) == (left | right))
            direction = 0;
        else if (pressed & left)
            direction = -1;
        else if (pressed & right)
            direction = 1;
        else
            direction = s.direction;
    } else if (held & left)
        direction = -1;
    else if (held & right)
        direction = 1;
    bool move = false;
    if (direction != s.direction) {
        s.direction = direction;
        s.das_timer = 0;
        move = direction != 0;
    } else if (direction) {
        ++s.das_timer;
        const unsigned delay = s.preset == Preset::fusion ? 16u : 6u,
                       repeat = s.preset == Preset::fusion ? 6u : 2u;
        move = s.das_timer >= delay && (s.das_timer - delay) % repeat == 0;
    } else
        s.das_timer = 0;
    if (move) {
        auto a = s.active;
        a.x += direction;
        if (!collides(s, a)) {
            s.active = a;
            s.last_rotation = false;
            e.shifted = true;
            e.moved = true;
            manipulation = true;
        }
    }
    if (s.preset == Preset::modern && (pressed & hard_drop)) {
        const auto a = ghost(s);
        add(s.score, static_cast<std::uint64_t>(a.y - s.active.y) * 2);
        s.active = a;
        lock(s, e);
    } else {
        constexpr std::array<unsigned, 21> gravity{60, 48, 40, 32, 26, 20, 16, 12, 10, 8, 6,
                                                   5,  4,  3,  2,  1,  1,  1,  1,  1,  1};
        const auto interval = (held & down) ? std::min(2u, gravity[level(s)]) : gravity[level(s)];
        ++s.gravity_timer;
        while (s.gravity_timer >= interval) {
            s.gravity_timer -= interval;
            auto a = s.active;
            ++a.y;
            if (collides(s, a)) {
                s.gravity_timer = 0;
                break;
            }
            s.active = a;
            e.moved = true;
            if (held & down) {
                add(s.score, 1);
                e.soft_dropped = true;
            }
        }
        if (was_grounded && manipulation && s.lock_resets < (s.preset == Preset::fusion ? 8 : 15)) {
            s.lock_elapsed = 0;
            ++s.lock_resets;
        }
        if (grounded(s)) {
            ++s.lock_elapsed;
            if (s.lock_elapsed >= static_cast<unsigned>(s.preset == Preset::fusion ? 18 : 30))
                lock(s, e);
        } else
            s.lock_elapsed = 0;
    }
    e.score_delta = s.score - old_score;
    ++s.tick;
    if (output)
        *output = e;
    return true;
}
State spin_fixture(std::uint32_t seed, Preset p, unsigned rows) {
    auto s = initial_state(seed, p);
    rows = std::clamp(rows, 1u, 3u);
    for (unsigned r = 0; r < rows; ++r) {
        const int y = 19 - static_cast<int>(r);
        for (int x = 0; x < 10; ++x)
            if (x != 4 && !(y == 18 && x == 5))
                s.board[static_cast<std::size_t>((y + 20) * 10 + x)] = 8;
    }
    if (rows < 3) {
        s.board[373] = 8;
        s.board[375] = 8;
    }
    s.board[354] = 8;
    s.active = {Piece::T, 0, 4, 15};
    return s;
}
} // namespace stackpulse::modern
