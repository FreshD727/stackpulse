#include <catch2/catch_test_macros.hpp>
#include <limits>
#include <stackpulse/modern_replay.hpp>
#include <stackpulse/replay.hpp>
using namespace stackpulse;
namespace {
void reseal(std::vector<std::uint8_t>& b) {
    std::uint32_t c = 0xffffffffu;
    for (std::size_t i = 0; i < b.size() - 4; ++i) {
        c ^= b[i];
        for (int k = 0; k < 8; ++k)
            c = (c >> 1) ^ ((c & 1) ? 0xedb88320u : 0u);
    }
    c ^= 0xffffffffu;
    for (unsigned i = 0; i < 4; ++i)
        b[b.size() - 4 + i] = static_cast<std::uint8_t>(c >> (8 * i));
}
std::uint32_t u32(const std::vector<std::uint8_t>& b, std::size_t p) {
    std::uint32_t n = 0;
    for (unsigned i = 0; i < 4; ++i)
        n |= static_cast<std::uint32_t>(b[p + i]) << (8 * i);
    return n;
}
} // namespace
TEST_CASE("Modern replay roundtrip from marathon and practice complete state") {
    for (bool practice : {false, true}) {
        modern_replay::Replay r{practice ? modern::spin_fixture(42, modern::Preset::modern, 2)
                                         : modern::initial_state(42),
                                {}};
        for (unsigned i = 0; i < 1300; ++i)
            r.inputs.push_back(i % 60 < 30 ? modern::down : 0u);
        std::string error;
        std::vector<std::uint8_t> bytes;
        REQUIRE(modern_replay::encode(r, bytes, error));
        REQUIRE(bytes[0] == 'S');
        REQUIRE(bytes[1] == 'P');
        REQUIRE(bytes[2] == 'M');
        REQUIRE(bytes[3] == 'R');
        REQUIRE(u32(bytes, 12) > 400);
        modern_replay::Replay restored;
        REQUIRE(modern_replay::decode(bytes, restored, error));
        REQUIRE(restored.initial == r.initial);
        REQUIRE(restored.inputs == r.inputs);
        modern::State a, b;
        REQUIRE(modern_replay::verify(r, a, error));
        REQUIRE(modern_replay::verify(restored, b, error));
        REQUIRE(a == b);
    }
}
TEST_CASE("Modern replay empty input and file boundary roundtrip") {
    modern_replay::Replay r{modern::initial_state(7), {}};
    std::string error;
    std::vector<std::uint8_t> bytes;
    REQUIRE(modern_replay::encode(r, bytes, error));
    modern_replay::Replay loaded;
    REQUIRE(modern_replay::decode(bytes, loaded, error));
    REQUIRE(loaded.initial == r.initial);
    const auto file = std::filesystem::temp_directory_path() / "stackpulse-modern-test.spmr";
    REQUIRE(modern_replay::save(file, r, error));
    REQUIRE(modern_replay::load(file, loaded, error));
    REQUIRE(loaded.initial == r.initial);
    std::filesystem::remove(file);
}
TEST_CASE("Modern replay rejects every truncated prefix corruption trailing and unknown versions") {
    modern_replay::Replay r{modern::initial_state(5), {0, modern::down, 0}};
    std::string error;
    std::vector<std::uint8_t> bytes;
    REQUIRE(modern_replay::encode(r, bytes, error));
    modern_replay::Replay out = r;
    for (std::size_t n = 0; n < bytes.size(); ++n) {
        auto broken = bytes;
        broken.resize(n);
        REQUIRE_FALSE(modern_replay::decode(broken, out, error));
        REQUIRE(out.initial == r.initial);
        REQUIRE(out.inputs == r.inputs);
    }
    for (std::size_t n = 0; n < bytes.size(); ++n) {
        auto broken = bytes;
        broken[n] ^= 0x80;
        REQUIRE_FALSE(modern_replay::decode(broken, out, error));
    }
    auto broken = bytes;
    broken.push_back(0);
    REQUIRE_FALSE(modern_replay::decode(broken, out, error));
    for (std::size_t p : {std::size_t{0},
                          std::size_t{4},
                          std::size_t{6},
                          std::size_t{8},
                          std::size_t{12},
                          std::size_t{16},
                          std::size_t{20},
                          std::size_t{24}}) {
        broken = bytes;
        broken[p] ^= 0x80;
        reseal(broken);
        REQUIRE_FALSE(modern_replay::decode(broken, out, error));
    }
    const auto state_start = 24 + u32(bytes, 8);
    const auto input_start = state_start + u32(bytes, 12);
    broken = bytes;
    broken[state_start] = 255;
    reseal(broken);
    REQUIRE_FALSE(modern_replay::decode(broken, out, error));
    broken = bytes;
    broken[input_start + 1] = 1;
    reseal(broken);
    REQUIRE_FALSE(modern_replay::decode(broken, out, error));
    broken = bytes;
    broken[broken.size() - 5] ^= 1;
    reseal(broken);
    REQUIRE_FALSE(modern_replay::decode(broken, out, error));
}

TEST_CASE("Modern replay restores hidden timers hold and pending garbage") {
    auto s = modern::initial_state(49);
    REQUIRE(modern::step(s, modern::hold));
    REQUIRE(modern::step(s, modern::left));
    REQUIRE(modern::enqueue(s, {19, s.tick, s.tick + 30, 2, 5, 8}));
    modern_replay::Replay r{s, {0, modern::clockwise, 0, modern::hard_drop, 0}};
    std::vector<std::uint8_t> bytes;
    std::string error;
    REQUIRE(modern_replay::encode(r, bytes, error));
    modern_replay::Replay out;
    REQUIRE(modern_replay::decode(bytes, out, error));
    REQUIRE(out.initial == s);
    modern::State a, b;
    REQUIRE(modern_replay::verify(r, a, error));
    REQUIRE(modern_replay::verify(out, b, error));
    REQUIRE(a == b);
    r.inputs[0] = 256;
    REQUIRE_FALSE(modern_replay::encode(r, bytes, error));
    r.inputs = {0};
    r.initial.tick = std::numeric_limits<std::uint64_t>::max();
    REQUIRE_FALSE(modern_replay::encode(r, bytes, error));
}

TEST_CASE("Modern replay verifies intermediate checkpoint indexes and hashes") {
    modern_replay::Replay r{modern::initial_state(8), std::vector<std::uint32_t>(1201, 0)};
    std::string error;
    std::vector<std::uint8_t> bytes;
    REQUIRE(modern_replay::encode(r, bytes, error));
    const auto start = 24 + u32(bytes, 8) + u32(bytes, 12) + r.inputs.size() * 4;
    modern_replay::Replay out;
    for (auto p : {start + 12, start + 16}) {
        auto broken = bytes;
        broken[p] ^= 1;
        reseal(broken);
        REQUIRE_FALSE(modern_replay::decode(broken, out, error));
    }
    r.inputs.resize(modern_replay::max_frames + 1);
    REQUIRE_FALSE(modern_replay::encode(r, bytes, error));
}

TEST_CASE("Modern and Classic replay codecs reject each others containers") {
    replay::Replay classic_record{classic::initial_state(8), {0}};
    modern_replay::Replay modern_record{modern::initial_state(8), {0}};
    std::vector<std::uint8_t> bytes;
    std::string error;
    REQUIRE(replay::encode(classic_record, bytes, error));
    REQUIRE_FALSE(modern_replay::decode(bytes, modern_record, error));
    REQUIRE(modern_replay::encode(modern_record, bytes, error));
    REQUIRE_FALSE(replay::decode(bytes, classic_record, error));
}

TEST_CASE("Fusion version five replay retains Classic start level hold and complete stream state") {
    auto s = modern::initial_state(49, modern::Preset::fusion, 19);
    REQUIRE(modern::step(s, modern::hold));
    REQUIRE(modern::step(s, modern::left));
    REQUIRE(modern::enqueue(s, {19, s.tick, s.tick + 30, 2, 5, 8}));
    modern_replay::Replay r{
        s, {0, modern::clockwise, 0, modern::hard_drop, 0, modern::hold, 0, modern::down, 0}};
    std::vector<std::uint8_t> bytes;
    std::string error;
    REQUIRE(modern_replay::encode(r, bytes, error));
    REQUIRE(bytes[4] == 5);
    modern_replay::Replay out;
    REQUIRE(modern_replay::decode(bytes, out, error));
    REQUIRE(out.initial == s);
    REQUIRE(out.initial.start_level == 19);
    modern::State a, b;
    REQUIRE(modern_replay::verify(r, a, error));
    REQUIRE(modern_replay::verify(out, b, error));
    REQUIRE(a == b);
    REQUIRE(a.rng == b.rng);
    REQUIRE(a.next == b.next);

    auto broken = bytes;
    broken[4] = 1;
    reseal(broken);
    REQUIRE_FALSE(modern_replay::decode(broken, out, error));
    REQUIRE(out.initial == s);
    broken = bytes;
    broken[4] = 2;
    reseal(broken);
    REQUIRE_FALSE(modern_replay::decode(broken, out, error));
    REQUIRE(error.find("older v1/v2/v3/v4 rejected") != std::string::npos);
    REQUIRE(out.initial == s);
    broken = bytes;
    broken[4] = 3;
    reseal(broken);
    REQUIRE_FALSE(modern_replay::decode(broken, out, error));
    REQUIRE(out.initial == s);
    broken = bytes;
    broken[4] = 4;
    reseal(broken);
    REQUIRE_FALSE(modern_replay::decode(broken, out, error));
    REQUIRE(out.initial == s);
    broken = bytes;
    const auto state_start = 24 + u32(bytes, 8);
    broken[state_start] = 1;
    reseal(broken);
    REQUIRE_FALSE(modern_replay::decode(broken, out, error));
    REQUIRE(out.initial == s);
    broken = bytes;
    broken[24] ^= 1;
    reseal(broken);
    REQUIRE_FALSE(modern_replay::decode(broken, out, error));
    REQUIRE(out.initial == s);
}
