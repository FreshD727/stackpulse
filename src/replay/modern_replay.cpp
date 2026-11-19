#include <fstream>
#include <limits>
#include <stackpulse/modern_replay.hpp>
#include <string_view>
namespace stackpulse::modern_replay {
namespace {
constexpr std::string_view rules =
    "modern-fusion-issue4-v4;codec=modern-state-v4;tick=60/1;board=10x40;"
    "geometry=project-90-v1;rotation=SRS-project;fusion=classic-uniform,preview5,hold1,spawn-top-"
    "cell0-TJL2,"
    "classic-gravity,startlevel0-29,immediate,local-spin-lock30,cap15;"
    "modern=bag7,preview5,spawn-2,das10,repeat3,lock30,cap15;fusion-das16-blockcharge-nexttick-"
    "reset10,opposites-neutral;"
    "softdrop=one-cell-per-tick-no-backlog;harddrop=modern-only;allclear=2000*(level+1);"
    "score=fusion-classic,spin1600-2400-3200*(level+1)|modern-battle-v1;"
    "battle=battle-v1;held-mask=255;hash=fnv1a64;checkpoints=600;overflow=reject";
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
bool verify(const Replay& r, modern::State& final_state, std::string& error) {
    error.clear();
    if (r.inputs.size() > max_frames)
        return fail(error, "frame limit exceeded");
    if (!modern::valid_state(r.initial))
        return fail(error, "invalid initial state");
    if (r.initial.tick > std::numeric_limits<std::uint64_t>::max() - r.inputs.size())
        return fail(error, "tick wrap forbidden");
    auto state = r.initial;
    for (auto held : r.inputs)
        if (!modern::step(state, held))
            return fail(error, "invalid held input or state");
    final_state = state;
    return true;
}
bool encode(const Replay& r, std::vector<std::uint8_t>& bytes, std::string& error) {
    error.clear();
    if (r.inputs.size() > max_frames)
        return fail(error, "frame limit exceeded");
    if (!modern::valid_state(r.initial))
        return fail(error, "invalid initial state");
    if (r.initial.tick > std::numeric_limits<std::uint64_t>::max() - r.inputs.size())
        return fail(error, "tick wrap forbidden");
    auto state_bytes = modern::canonical_bytes(r.initial);
    if (state_bytes.size() > 262144)
        return fail(error, "initial state limit exceeded");
    std::vector<std::uint8_t> b{'S', 'P', 'M', 'R'};
    put(b, 4, 2);
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
    put(b, modern::state_hash(s), 8);
    for (std::size_t i = 0; i < r.inputs.size(); ++i) {
        if (!modern::step(s, r.inputs[i]))
            return fail(error, "invalid held input or state");
        if ((i + 1) % 600 == 0 || i + 1 == r.inputs.size()) {
            put(b, i + 1, 4);
            put(b, modern::state_hash(s), 8);
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
        if (p.get(4) != 0x524d5053u || p.get(2) != 4 || p.get(2) != 0)
            return fail(error,
                        "incompatible Modern/Fusion replay format (requires issue-4 v4; older "
                        "v1/v2/v3 rejected)");
        const auto rules_len = p.get(4), state_len = p.get(4), frames = p.get(4), hashes = p.get(4);
        if (rules_len > 4096 || state_len > 262144 || frames > max_frames ||
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
        if (!modern::decode_state(initial_bytes, candidate.initial) ||
            modern::canonical_bytes(candidate.initial) != initial_bytes)
            return fail(error, "invalid canonical initial state");
        if (candidate.initial.tick > std::numeric_limits<std::uint64_t>::max() - frames)
            return fail(error, "tick wrap forbidden");
        candidate.inputs.reserve(static_cast<std::size_t>(frames));
        for (std::uint64_t i = 0; i < frames; ++i) {
            auto held = static_cast<std::uint32_t>(p.get(4));
            if (held & ~modern::action_mask)
                return fail(error, "invalid input mask");
            candidate.inputs.push_back(held);
        }
        auto s = candidate.initial;
        if (p.get(4) != 0 || p.get(8) != modern::state_hash(s))
            return fail(error, "initial hash mismatch");
        for (std::size_t i = 0; i < candidate.inputs.size(); ++i) {
            if (!modern::step(s, candidate.inputs[i]))
                return fail(error, "replay simulation rejected frame");
            if ((i + 1) % 600 == 0 || i + 1 == candidate.inputs.size()) {
                if (p.get(4) != i + 1 || p.get(8) != modern::state_hash(s))
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
} // namespace stackpulse::modern_replay
