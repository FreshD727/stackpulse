#include <algorithm>
#include <bit>
#include <limits>
#include <stackpulse/modern.hpp>
namespace stackpulse::modern {
bool valid_state(const State& s) noexcept {
    if (s.preset == Preset::classic) {
        if (!s.classic_state || !classic::valid_state(*s.classic_state) ||
            (s.held & ~action_mask) || !s.target_rng || !s.garbage_rng || !s.incoming.empty() ||
            !s.received_ids.empty())
            return false;
        auto expected = s;
        project_classic(expected);
        return expected == s;
    }
    if (s.classic_state)
        return false;
    if (s.start_level > 29 || (s.preset == Preset::modern && s.start_level != 0) ||
        static_cast<unsigned>(s.preset) > 1 || static_cast<unsigned>(s.active.piece) > 6 ||
        s.active.rotation > 3 || s.active.x < -4 || s.active.x > 10 || s.active.y < -20 ||
        s.active.y > 19 || s.held & ~action_mask || !s.rng || !s.garbage_rng || !s.target_rng ||
        (s.preset == Preset::fusion && s.das_timer > 16) || s.bag_cursor > 7 ||
        static_cast<unsigned>(s.held_piece) > 6 || s.direction < -1 || s.direction > 1 ||
        s.combo < -1 || s.combo > 1000000 || s.lock_resets > 15 || s.last_kick > 4 ||
        s.gravity_timer > 60 || s.lock_elapsed > 30 || s.incoming.size() > 256 ||
        s.received_ids.size() > 4096)
        return false;
    if (!s.game_over)
        for (unsigned row = 0; row < 40; ++row)
            if (std::all_of(s.board.begin() + row * 10,
                            s.board.begin() + (row + 1) * 10,
                            [](auto c) { return c != 0; }))
                return false;
    for (auto c : s.board)
        if (c > 8)
            return false;
    for (auto p : s.next)
        if (static_cast<unsigned>(p) > 6)
            return false;
    std::array<bool, 7> seen{};
    for (auto p : s.bag) {
        const auto i = static_cast<unsigned>(p);
        if (i > 6 || seen[i])
            return false;
        seen[i] = true;
    }
    for (std::size_t i = 0; i < s.received_ids.size(); ++i) {
        if (!s.received_ids[i])
            return false;
        if (i && s.received_ids[i - 1] >= s.received_ids[i])
            return false;
    }
    for (std::size_t i = 0; i < s.incoming.size(); ++i) {
        const auto& p = s.incoming[i];
        for (std::size_t j = 0; j < i; ++j)
            if (s.incoming[j].id == p.id)
                return false;
        if (!p.id || !p.rows || p.rows > 1024 || p.hole > 9 || p.eligible < p.creation ||
            !std::binary_search(s.received_ids.begin(), s.received_ids.end(), p.id))
            return false;
        if (i) {
            const auto& prev = s.incoming[i - 1];
            if (prev.creation > p.creation || (prev.creation == p.creation && prev.id >= p.id))
                return false;
        }
    }
    if (!s.game_over && collides(s, s.active))
        return false;
    return true;
}
namespace {
struct Writer {
    std::vector<std::uint8_t> b;
    void u(std::uint64_t v, unsigned n) {
        for (unsigned i = 0; i < n; ++i)
            b.push_back(static_cast<std::uint8_t>(v >> (i * 8)));
    }
};
struct Reader {
    const std::vector<std::uint8_t>& b;
    std::size_t pos{};
    bool ok{true};
    std::uint64_t u(unsigned n) {
        if (n > b.size() - std::min(pos, b.size())) {
            ok = false;
            return 0;
        }
        std::uint64_t v = 0;
        for (unsigned i = 0; i < n; ++i)
            v |= static_cast<std::uint64_t>(b[pos++]) << (i * 8);
        return v;
    }
    bool flag() {
        auto v = u(1);
        if (v > 1)
            ok = false;
        return v == 1;
    }
};
} // namespace
std::vector<std::uint8_t> canonical_bytes(const State& s) {
    Writer w;
    w.u(rules_version, 4);
    w.u(static_cast<unsigned>(s.preset), 1);
    w.u(s.tick, 8);
    w.u(s.score, 8);
    w.u(s.lines, 8);
    w.u(s.start_level, 4);
    for (auto c : s.board)
        w.u(c, 1);
    w.u(static_cast<unsigned>(s.active.piece), 1);
    w.u(s.active.rotation, 1);
    w.u(static_cast<std::uint32_t>(s.active.x), 4);
    w.u(static_cast<std::uint32_t>(s.active.y), 4);
    w.u(s.rng, 4);
    w.u(s.garbage_rng, 4);
    w.u(s.target_rng, 4);
    w.u(s.held, 4);
    for (auto p : s.bag)
        w.u(static_cast<unsigned>(p), 1);
    w.u(s.bag_cursor, 1);
    for (auto p : s.next)
        w.u(static_cast<unsigned>(p), 1);
    w.u(static_cast<unsigned>(s.held_piece), 1);
    w.u(s.has_hold, 1);
    w.u(s.hold_used, 1);
    w.u(s.gravity_timer, 8);
    w.u(s.das_timer, 8);
    w.u(s.lock_elapsed, 8);
    w.u(static_cast<std::uint32_t>(s.direction), 4);
    w.u(static_cast<std::uint32_t>(s.combo), 4);
    w.u(s.lock_resets, 1);
    w.u(s.last_kick, 1);
    w.u(s.last_rotation, 1);
    w.u(s.back_to_back, 1);
    w.u(s.game_over, 1);
    w.u(s.soft_drop_armed, 1);
    w.u(s.classic_state.has_value(), 1);
    if (s.classic_state) {
        const auto bytes = classic::canonical_bytes(*s.classic_state);
        w.u(bytes.size(), 4);
        w.b.insert(w.b.end(), bytes.begin(), bytes.end());
    }
    w.u(s.incoming.size(), 4);
    for (auto p : s.incoming) {
        w.u(p.id, 8);
        w.u(p.creation, 8);
        w.u(p.eligible, 8);
        w.u(p.source, 2);
        w.u(p.rows, 2);
        w.u(p.hole, 1);
    }
    w.u(s.received_ids.size(), 4);
    for (auto id : s.received_ids)
        w.u(id, 8);
    return w.b;
}
bool decode_state(const std::vector<std::uint8_t>& b, State& out) {
    if (b.size() > 50000)
        return false;
    Reader r{b};
    if (r.u(4) != rules_version)
        return false;
    State s;
    s.preset = static_cast<Preset>(r.u(1));
    s.tick = r.u(8);
    s.score = r.u(8);
    s.lines = r.u(8);
    s.start_level = static_cast<std::uint32_t>(r.u(4));
    for (auto& c : s.board)
        c = static_cast<std::uint8_t>(r.u(1));
    s.active.piece = static_cast<Piece>(r.u(1));
    s.active.rotation = static_cast<std::uint8_t>(r.u(1));
    s.active.x = std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(r.u(4)));
    s.active.y = std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(r.u(4)));
    s.rng = static_cast<std::uint32_t>(r.u(4));
    s.garbage_rng = static_cast<std::uint32_t>(r.u(4));
    s.target_rng = static_cast<std::uint32_t>(r.u(4));
    s.held = static_cast<std::uint32_t>(r.u(4));
    for (auto& p : s.bag)
        p = static_cast<Piece>(r.u(1));
    s.bag_cursor = static_cast<std::uint8_t>(r.u(1));
    for (auto& p : s.next)
        p = static_cast<Piece>(r.u(1));
    s.held_piece = static_cast<Piece>(r.u(1));
    s.has_hold = r.flag();
    s.hold_used = r.flag();
    s.gravity_timer = r.u(8);
    s.das_timer = r.u(8);
    s.lock_elapsed = r.u(8);
    s.direction = std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(r.u(4)));
    s.combo = std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(r.u(4)));
    s.lock_resets = static_cast<std::uint8_t>(r.u(1));
    s.last_kick = static_cast<std::uint8_t>(r.u(1));
    s.last_rotation = r.flag();
    s.back_to_back = r.flag();
    s.game_over = r.flag();
    s.soft_drop_armed = r.flag();
    if (r.flag()) {
        const auto size = r.u(4);
        if (!r.ok || size > b.size() - r.pos)
            return false;
        std::vector<std::uint8_t> bytes(b.begin() + static_cast<std::ptrdiff_t>(r.pos),
                                        b.begin() + static_cast<std::ptrdiff_t>(r.pos + size));
        r.pos += static_cast<std::size_t>(size);
        classic::State c;
        if (!classic::decode_state(bytes, c))
            return false;
        s.classic_state = c;
    }
    const auto n = r.u(4);
    if (n > 256)
        return false;
    for (std::uint64_t i = 0; i < n; ++i) {
        Packet p;
        p.id = r.u(8);
        p.creation = r.u(8);
        p.eligible = r.u(8);
        p.source = static_cast<std::uint16_t>(r.u(2));
        p.rows = static_cast<std::uint16_t>(r.u(2));
        p.hole = static_cast<std::uint8_t>(r.u(1));
        s.incoming.push_back(p);
    }
    const auto ids = r.u(4);
    if (ids > 4096)
        return false;
    for (std::uint64_t i = 0; i < ids; ++i)
        s.received_ids.push_back(r.u(8));
    if (!r.ok || r.pos != b.size() || !valid_state(s))
        return false;
    out = std::move(s);
    return true;
}
std::uint64_t state_hash(const State& s) {
    std::uint64_t h = 14695981039346656037ull;
    for (auto b : canonical_bytes(s)) {
        h ^= b;
        h *= 1099511628211ull;
    }
    return h;
}
} // namespace stackpulse::modern
