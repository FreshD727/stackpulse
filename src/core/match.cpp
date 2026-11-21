#include <algorithm>
#include <limits>
#include <stackpulse/match.hpp>
#include <stdexcept>
#include <tuple>
namespace stackpulse::match {
namespace {
constexpr auto maximum = std::numeric_limits<std::uint64_t>::max();
std::uint32_t bounded(std::uint32_t& x, std::uint32_t n) {
    const auto threshold = (0u - n) % n;
    do {
        x ^= x << 13;
        x ^= x >> 17;
        x ^= x << 5;
    } while (x < threshold);
    return x % n;
}
Player* player(State& s, std::uint16_t id) {
    auto it = std::find_if(
        s.players.begin(), s.players.end(), [id](const auto& p) { return p.id == id; });
    return it == s.players.end() ? nullptr : &*it;
}
std::vector<std::uint16_t> opponents(const State& s, std::uint16_t id) {
    std::vector<std::uint16_t> out;
    for (const auto& p : s.players)
        if (p.id != id && !p.placement)
            out.push_back(p.id);
    return out;
}
void choose(Player& p, const std::vector<std::uint16_t>& ids) {
    p.target = ids.empty() ? 0
               : ids.size() == 1
                   ? ids[0]
                   : ids[bounded(p.board.target_rng, static_cast<std::uint32_t>(ids.size()))];
}
bool fail(std::string& e, const char* text) {
    e = text;
    return false;
}
void put(std::vector<std::uint8_t>& b, std::uint64_t v, unsigned n) {
    for (unsigned i = 0; i < n; ++i)
        b.push_back(static_cast<std::uint8_t>(v >> (i * 8)));
}
struct Reader {
    const std::vector<std::uint8_t>& b;
    std::size_t p{};
    bool ok{true};
    std::uint64_t get(unsigned n) {
        if (n > b.size() - p) {
            ok = false;
            return 0;
        }
        std::uint64_t v = 0;
        for (unsigned i = 0; i < n; ++i)
            v |= std::uint64_t(b[p++]) << (i * 8);
        return v;
    }
};
} // namespace
State initial_state(const std::vector<std::uint16_t>& ids,
                    std::uint32_t seed,
                    modern::Preset preset,
                    std::uint32_t start_level) {
    State s;
    for (auto id : ids) {
        Player p;
        p.id = id;
        p.board = modern::initial_state(seed ^ id, preset, start_level);
        s.players.push_back(p);
    }
    if (!valid_state(s))
        throw std::invalid_argument("invalid match roster");
    for (auto& p : s.players)
        choose(p, opponents(s, p.id));
    return s;
}
bool valid_state(const State& s) noexcept {
    if (s.players.size() < 2 || s.players.size() > 8 || s.scheduled.size() > 256 ||
        !s.next_packet_id || static_cast<unsigned>(s.result) > 2 || s.result_tick > s.tick)
        return false;
    unsigned alive = 0;
    std::uint16_t previous = 0;
    for (const auto& p : s.players) {
        if (p.board.preset != s.players.front().board.preset ||
            p.board.start_level != s.players.front().board.start_level || !p.id ||
            p.id <= previous || !modern::valid_state(p.board) ||
            (!p.placement && p.board.tick != s.tick) || p.board.tick > s.tick ||
            p.placement > s.players.size() || p.target == p.id ||
            (p.placement && !p.board.game_over) || (!p.placement && p.board.game_over) ||
            (!p.disconnected && p.deadline))
            return false;
        if (p.target && std::none_of(s.players.begin(), s.players.end(), [&](const auto& x) {
                return x.id == p.target;
            }))
            return false;
        previous = p.id;
        alive += !p.placement;
    }
    if (s.result == Result::playing && (alive < 2 || s.result_tick))
        return false;
    if (s.result == Result::winner && alive != 1)
        return false;
    if (s.result == Result::draw && alive != 0)
        return false;
    if (s.players.front().board.preset == modern::Preset::classic && !s.scheduled.empty())
        return false;
    std::uint64_t prior = 0;
    for (const auto& a : s.scheduled) {
        if (!a.packet.id || a.packet.id <= prior || a.packet.id >= s.next_packet_id ||
            a.packet.creation > maximum - 30 || a.delivery != a.packet.creation + 1 ||
            a.delivery < s.tick || a.packet.eligible != a.packet.creation + 30 || !a.packet.rows ||
            a.packet.rows > 512 || a.packet.hole > 9 || a.recipient == a.packet.source ||
            std::none_of(s.players.begin(),
                         s.players.end(),
                         [&](const auto& p) { return p.id == a.recipient; }) ||
            std::none_of(s.players.begin(), s.players.end(), [&](const auto& p) {
                return p.id == a.packet.source;
            }))
            return false;
        prior = a.packet.id;
    }
    return true;
}
bool advance(State& state,
             const std::vector<std::uint32_t>& held,
             Events* events,
             std::string& error) {
    error.clear();
    if (!valid_state(state) || held.size() != state.players.size())
        return fail(error, "invalid match state or roster inputs");
    for (auto mask : held)
        if (mask & ~modern::action_mask)
            return fail(error, "invalid match input mask");
    if (state.result != Result::playing)
        return fail(error, "match already ended");
    if (state.tick == maximum)
        return fail(error, "match tick exhausted");
    State s = state;
    Events out;
    out.boards.resize(s.players.size());
    for (const auto& a : s.scheduled)
        if (a.delivery == s.tick) {
            auto* p = player(s, a.recipient);
            if (!p->placement && !modern::enqueue(p->board, a.packet))
                return fail(error, "packet arrival capacity or state rejected");
        }
    std::erase_if(s.scheduled, [&](const auto& a) { return a.delivery == s.tick; });
    std::vector<std::size_t> deaths;
    for (std::size_t i = 0; i < s.players.size(); ++i) {
        auto& p = s.players[i];
        if (p.placement)
            continue;
        auto mask = p.disconnected ? 0 : held[i];
        if ((mask & modern::target_next) && !(p.board.held & modern::target_next)) {
            auto ids = opponents(s, p.id);
            auto it = std::upper_bound(ids.begin(), ids.end(), p.target);
            p.target = ids.empty() ? 0 : it == ids.end() ? ids.front() : *it;
        }
        if (!modern::step(p.board, mask, &out.boards[i]))
            return fail(error, "board step rejected");
        if (p.board.game_over || (p.disconnected && p.deadline <= s.tick))
            deaths.push_back(i);
    }
    const auto live_before = static_cast<unsigned>(std::count_if(
        s.players.begin(), s.players.end(), [](const auto& p) { return !p.placement; }));
    const auto survivors = live_before - static_cast<unsigned>(deaths.size());
    for (auto i : deaths) {
        auto& p = s.players[i];
        p.placement = static_cast<std::uint8_t>(survivors ? survivors + 1 : 1);
        p.board.game_over = true;
        if (p.board.classic_state)
            p.board.classic_state->game_over = true;
        out.eliminated.push_back(p.id);
    }
    for (std::size_t i = 0; i < s.players.size(); ++i) {
        auto& p = s.players[i];
        auto attack = out.boards[i].attack;
        if (p.placement || !attack)
            continue;
        auto ids = opponents(s, p.id);
        if (ids.empty())
            continue;
        if (!std::binary_search(ids.begin(), ids.end(), p.target))
            choose(p, ids);
        if (s.tick > maximum - 30 || s.next_packet_id == maximum || attack > 512 ||
            s.scheduled.size() == 256)
            return fail(error, "dispatch capacity or counter exhausted");
        Arrival a{s.tick + 1,
                  p.target,
                  {s.next_packet_id++,
                   s.tick,
                   s.tick + 30,
                   p.id,
                   static_cast<std::uint16_t>(attack),
                   static_cast<std::uint8_t>(bounded(p.board.garbage_rng, 10))}};
        s.scheduled.push_back(a);
        out.dispatched.push_back(a);
    }
    if (survivors <= 1) {
        s.result = survivors ? Result::winner : Result::draw;
        s.result_tick = s.tick + 1;
        out.result_generated = true;
    }
    ++s.tick;
    if (!valid_state(s))
        return fail(error, "resulting match state invalid");
    state = std::move(s);
    if (events)
        *events = std::move(out);
    return true;
}
bool disconnect(State& s, std::uint16_t id) {
    if (!valid_state(s) || s.result != Result::playing || s.tick > maximum - 600)
        return false;
    auto* p = player(s, id);
    if (!p || p->placement)
        return false;
    if (!p->disconnected) {
        p->disconnected = true;
        p->deadline = s.tick + 600;
        p->board.held = 0;
        p->board.soft_drop_armed = true;
        if (p->board.classic_state) {
            p->board.classic_state->held = 0;
            p->board.classic_state->soft_drop_armed = true;
        }
    }
    return true;
}
bool reconnect(State& s, std::uint16_t id) {
    if (!valid_state(s) || s.result != Result::playing)
        return false;
    auto* p = player(s, id);
    if (!p || p->placement)
        return false;
    p->disconnected = false;
    p->deadline = 0;
    return true;
}
std::vector<std::uint8_t> canonical_bytes(const State& s) {
    std::vector<std::uint8_t> b;
    put(b, 0x314d5053, 4);
    put(b, s.tick, 8);
    put(b, s.next_packet_id, 8);
    put(b, s.players.size(), 1);
    for (const auto& p : s.players) {
        put(b, p.id, 2);
        put(b, p.target, 2);
        put(b, p.placement, 1);
        put(b, p.disconnected, 1);
        put(b, p.deadline, 8);
        auto board = modern::canonical_bytes(p.board);
        put(b, board.size(), 4);
        b.insert(b.end(), board.begin(), board.end());
    }
    put(b, s.scheduled.size(), 2);
    for (const auto& a : s.scheduled) {
        put(b, a.delivery, 8);
        put(b, a.recipient, 2);
        put(b, a.packet.id, 8);
        put(b, a.packet.creation, 8);
        put(b, a.packet.eligible, 8);
        put(b, a.packet.source, 2);
        put(b, a.packet.rows, 2);
        put(b, a.packet.hole, 1);
    }
    put(b, static_cast<unsigned>(s.result), 1);
    put(b, s.result_tick, 8);
    return b;
}
bool decode_state(const std::vector<std::uint8_t>& b, State& out) {
    if (b.size() > 1048576)
        return false;
    Reader r{b};
    State s;
    if (r.get(4) != 0x314d5053)
        return false;
    s.tick = r.get(8);
    s.next_packet_id = r.get(8);
    auto count = r.get(1);
    if (count < 2 || count > 8)
        return false;
    for (unsigned i = 0; i < count; ++i) {
        Player p;
        p.id = static_cast<std::uint16_t>(r.get(2));
        p.target = static_cast<std::uint16_t>(r.get(2));
        p.placement = static_cast<std::uint8_t>(r.get(1));
        auto flag = r.get(1);
        if (flag > 1)
            return false;
        p.disconnected = flag != 0;
        p.deadline = r.get(8);
        auto size = r.get(4);
        if (size > b.size() - r.p)
            return false;
        std::vector<std::uint8_t> board(b.begin() + static_cast<std::ptrdiff_t>(r.p),
                                        b.begin() + static_cast<std::ptrdiff_t>(r.p + size));
        r.p += static_cast<std::size_t>(size);
        if (!modern::decode_state(board, p.board))
            return false;
        s.players.push_back(std::move(p));
    }
    auto n = r.get(2);
    if (n > 256)
        return false;
    for (unsigned i = 0; i < n; ++i) {
        Arrival a;
        a.delivery = r.get(8);
        a.recipient = static_cast<std::uint16_t>(r.get(2));
        a.packet.id = r.get(8);
        a.packet.creation = r.get(8);
        a.packet.eligible = r.get(8);
        a.packet.source = static_cast<std::uint16_t>(r.get(2));
        a.packet.rows = static_cast<std::uint16_t>(r.get(2));
        a.packet.hole = static_cast<std::uint8_t>(r.get(1));
        s.scheduled.push_back(a);
    }
    s.result = static_cast<Result>(r.get(1));
    s.result_tick = r.get(8);
    if (!r.ok || r.p != b.size() || !valid_state(s) || canonical_bytes(s) != b)
        return false;
    out = std::move(s);
    return true;
}
std::uint64_t state_hash(const State& s) {
    auto h = std::uint64_t{14695981039346656037ull};
    for (auto v : canonical_bytes(s)) {
        h ^= v;
        h *= 1099511628211ull;
    }
    return h;
}
} // namespace stackpulse::match
