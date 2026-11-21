#include <fstream>
#include <limits>
#include <stackpulse/replay.hpp>
#include <string_view>
namespace stackpulse::replay {
namespace {
constexpr std::string_view rules =
    "classic-provisional-v4;tick=60/"
    "1;board=10x20;spawn=3,-1;spawn-rotation=I0,O0,T2,S0,Z0,J2,L2;matrix=I4,SZ3-two-state-"
    "horizontal-down1,O-fixed,other3;rotation=no-kicks;random=xorshift32-reject7;seed0=1;gravity="
    "48,43,38,33,28,23,18,13,8,6,5,5,5,4,4,4,3,3,3,2,2,2,2,2,2,2,2,2,2,1;das=16,6,blocked-charge16,"
    "entry-saturates16;opposites=neutral-reset;entry=10;clear=20;lock=failed-down;soft=2,1;score=0,"
    "40,100,300,1200;lines=saturate-u32;level=initial-min(start*10+10,max(100,start*10-50))-then10;"
    "clear-score=post-transition;topout=above0-or-blocked-spawn;held=left1,right2,down4,cw8,ccw16;"
    "order=edges,das,entry,rotate,horizontal,gravity;random-reject=0..3;start-clamp=29;rotate="
    "press-edge-opposites-cancel;softdrop=per-piece-release-repress-including-entry";
constexpr std::size_t max_bytes = 8 * 1024 * 1024;
void put(std::vector<std::uint8_t>& b, std::uint64_t v, unsigned n) {
    for (unsigned i = 0; i < n; ++i)
        b.push_back(static_cast<std::uint8_t>(v >> (8 * i)));
}
std::uint32_t crc(const std::uint8_t* p, std::size_t n) {
    std::uint32_t c = 0xffffffffu;
    for (std::size_t i = 0; i < n; ++i) {
        c ^= p[i];
        for (int k = 0; k < 8; ++k)
            c = (c >> 1) ^ ((c & 1) ? 0xedb88320u : 0u);
    }
    return c ^ 0xffffffffu;
}
struct Reader {
    const std::vector<std::uint8_t>& b;
    std::size_t p{};
    std::uint64_t get(unsigned n) {
        if (n > b.size() - p)
            throw std::string("truncated replay");
        std::uint64_t v = 0;
        for (unsigned i = 0; i < n; ++i)
            v |= std::uint64_t(b[p++]) << (8 * i);
        return v;
    }
    std::vector<std::uint8_t> blob(std::size_t n) {
        if (n > b.size() - p)
            throw std::string("truncated replay");
        auto begin = b.begin() + static_cast<std::ptrdiff_t>(p);
        p += n;
        return {begin, begin + static_cast<std::ptrdiff_t>(n)};
    }
};
bool fail(std::string& e, const char* s) {
    e = s;
    return false;
}
} // namespace
bool verify(const Replay& r, classic::State& final_state, std::string& error) {
    error.clear();
    if (r.inputs.size() > max_frames)
        return fail(error, "frame limit exceeded");
    if (!classic::valid_state(r.initial))
        return fail(error, "invalid initial state");
    if (r.initial.tick > std::numeric_limits<std::uint64_t>::max() - r.inputs.size())
        return fail(error, "tick wrap forbidden");
    auto state = r.initial;
    for (auto held : r.inputs)
        if (!classic::step(state, held))
            return fail(error, "invalid held input or state");
    final_state = state;
    return true;
}
bool encode(const Replay& r, std::vector<std::uint8_t>& bytes, std::string& error) {
    classic::State final_state;
    if (!verify(r, final_state, error))
        return false;
    auto state_bytes = classic::canonical_bytes(r.initial);
    std::vector<std::uint8_t> b{'S', 'P', 'C', 'R'};
    put(b, 1, 2);
    put(b, 0, 2);
    put(b, rules.size(), 4);
    put(b, state_bytes.size(), 4);
    put(b, r.inputs.size(), 4);
    const auto count =
        static_cast<std::uint32_t>(r.inputs.size() / 600 + 1 + (r.inputs.size() % 600 != 0));
    put(b, count, 4);
    b.insert(b.end(), rules.begin(), rules.end());
    b.insert(b.end(), state_bytes.begin(), state_bytes.end());
    for (auto held : r.inputs)
        put(b, held, 4);
    auto s = r.initial;
    put(b, 0, 4);
    put(b, classic::state_hash(s), 8);
    for (std::size_t i = 0; i < r.inputs.size(); ++i) {
        classic::step(s, r.inputs[i]);
        if ((i + 1) % 600 == 0 || i + 1 == r.inputs.size()) {
            put(b, i + 1, 4);
            put(b, classic::state_hash(s), 8);
        }
    }
    put(b, crc(b.data(), b.size()), 4);
    bytes = std::move(b);
    return true;
}
bool decode(const std::vector<std::uint8_t>& bytes, Replay& out, std::string& error) {
    error.clear();
    try {
        if (bytes.size() < 28 || bytes.size() > max_bytes)
            return fail(error, "invalid replay size");
        Reader checksum{bytes, bytes.size() - 4};
        if (checksum.get(4) != crc(bytes.data(), bytes.size() - 4))
            return fail(error, "replay CRC mismatch");
        Reader p{bytes};
        if (p.get(4) != 0x52435053u || p.get(2) != 1 || p.get(2) != 0)
            return fail(error,
                        "incompatible Classic replay format (requires per-piece softdrop v4; older "
                        "versions rejected)");
        const auto rules_len = p.get(4), state_len = p.get(4), frames = p.get(4), hashes = p.get(4);
        if (rules_len > 4096 || state_len > 4096 || frames > max_frames ||
            hashes != frames / 600 + 1 + (frames % 600 != 0))
            return fail(error, "invalid replay lengths");
        const auto expected = 24 + rules_len + state_len + frames * 4 + hashes * 12 + 4;
        if (expected != bytes.size())
            return fail(error, "truncated or trailing replay bytes");
        auto rule_bytes = p.blob(static_cast<std::size_t>(rules_len));
        if (std::string_view(reinterpret_cast<const char*>(rule_bytes.data()), rule_bytes.size()) !=
            rules)
            return fail(error, "unsupported rules identity");
        Replay candidate;
        auto initial_bytes = p.blob(static_cast<std::size_t>(state_len));
        if (!classic::decode_state(initial_bytes, candidate.initial) ||
            classic::canonical_bytes(candidate.initial) != initial_bytes)
            return fail(error, "invalid canonical initial state");
        if (candidate.initial.tick > std::numeric_limits<std::uint64_t>::max() - frames)
            return fail(error, "tick wrap forbidden");
        candidate.inputs.reserve(static_cast<std::size_t>(frames));
        for (std::uint64_t i = 0; i < frames; ++i) {
            auto held = static_cast<std::uint32_t>(p.get(4));
            if (held & ~classic::action_mask)
                return fail(error, "invalid input mask");
            candidate.inputs.push_back(held);
        }
        auto s = candidate.initial;
        if (p.get(4) != 0 || p.get(8) != classic::state_hash(s))
            return fail(error, "initial hash mismatch");
        for (std::size_t i = 0; i < candidate.inputs.size(); ++i) {
            if (!classic::step(s, candidate.inputs[i]))
                return fail(error, "replay simulation rejected frame");
            if ((i + 1) % 600 == 0 || i + 1 == candidate.inputs.size()) {
                if (p.get(4) != i + 1 || p.get(8) != classic::state_hash(s))
                    return fail(error, "checkpoint or final hash mismatch");
            }
        }
        if (p.p != bytes.size() - 4)
            return fail(error, "trailing replay records");
        out = std::move(candidate);
        return true;
    } catch (const std::string& e) {
        error = e;
        return false;
    } catch (const std::exception& e) {
        error = e.what();
        return false;
    }
}
bool save(const std::filesystem::path& path, const Replay& r, std::string& error) {
    std::vector<std::uint8_t> b;
    if (!encode(r, b, error))
        return false;
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f)
        return fail(error, "cannot open replay for writing");
    f.write(reinterpret_cast<const char*>(b.data()), static_cast<std::streamsize>(b.size()));
    f.close();
    if (!f)
        return fail(error, "failed writing replay");
    return true;
}
bool load(const std::filesystem::path& path, Replay& r, std::string& error) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f)
        return fail(error, "cannot open replay");
    auto size = f.tellg();
    if (size < 0 || size > static_cast<std::streamoff>(max_bytes))
        return fail(error, "invalid replay file size");
    std::vector<std::uint8_t> b(static_cast<std::size_t>(size));
    f.seekg(0);
    f.read(reinterpret_cast<char*>(b.data()), static_cast<std::streamsize>(b.size()));
    if (!f)
        return fail(error, "failed reading replay");
    return decode(b, r, error);
}
} // namespace stackpulse::replay
