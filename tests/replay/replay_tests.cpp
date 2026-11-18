#include <catch2/catch_test_macros.hpp>
#include <limits>
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
TEST_CASE("Classic replay roundtrip from marathon and practice complete state") {
    for (bool practice : {false, true}) {
        replay::Replay r{practice ? classic::clear_fixture(42) : classic::initial_state(42), {}};
        for (unsigned i = 0; i < 1300; ++i)
            r.inputs.push_back(i % 60 < 30 ? classic::down : 0u);
        std::string error;
        std::vector<std::uint8_t> bytes;
        REQUIRE(replay::encode(r, bytes, error));
        REQUIRE(bytes[0] == 'S');
        REQUIRE(bytes[1] == 'P');
        REQUIRE(bytes[2] == 'C');
        REQUIRE(bytes[3] == 'R');
        REQUIRE(u32(bytes, 12) == 268);
        replay::Replay restored;
        REQUIRE(replay::decode(bytes, restored, error));
        REQUIRE(restored.initial == r.initial);
        REQUIRE(restored.inputs == r.inputs);
        classic::State a, b;
        REQUIRE(replay::verify(r, a, error));
        REQUIRE(replay::verify(restored, b, error));
        REQUIRE(a == b);
        if (practice) {
            REQUIRE(a.lines == 4);
            REQUIRE(a.score >= 1200);
        }
    }
}
TEST_CASE("Classic replay empty input and file boundary roundtrip") {
    replay::Replay r{classic::initial_state(7), {}};
    std::string error;
    std::vector<std::uint8_t> bytes;
    REQUIRE(replay::encode(r, bytes, error));
    replay::Replay loaded;
    REQUIRE(replay::decode(bytes, loaded, error));
    REQUIRE(loaded.initial == r.initial);
    const auto file = std::filesystem::temp_directory_path() / "stackpulse-classic-test.spcr";
    REQUIRE(replay::save(file, r, error));
    REQUIRE(replay::load(file, loaded, error));
    REQUIRE(loaded.initial == r.initial);
    std::filesystem::remove(file);
}
TEST_CASE(
    "Classic replay rejects every truncated prefix corruption trailing and unknown versions") {
    replay::Replay r{classic::initial_state(5), {0, classic::down, 0}};
    std::string error;
    std::vector<std::uint8_t> bytes;
    REQUIRE(replay::encode(r, bytes, error));
    replay::Replay out = r;
    for (std::size_t n = 0; n < bytes.size(); ++n) {
        auto broken = bytes;
        broken.resize(n);
        REQUIRE_FALSE(replay::decode(broken, out, error));
        REQUIRE(out.initial == r.initial);
        REQUIRE(out.inputs == r.inputs);
    }
    for (std::size_t n = 0; n < bytes.size(); ++n) {
        auto broken = bytes;
        broken[n] ^= 0x80;
        REQUIRE_FALSE(replay::decode(broken, out, error));
    }
    auto broken = bytes;
    broken.push_back(0);
    REQUIRE_FALSE(replay::decode(broken, out, error));
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
        REQUIRE_FALSE(replay::decode(broken, out, error));
    }
    const auto state_start = 24 + u32(bytes, 8);
    const auto input_start = state_start + u32(bytes, 12);
    broken = bytes;
    broken[state_start] = 1;
    reseal(broken);
    REQUIRE_FALSE(replay::decode(broken, out, error));
    broken = bytes;
    broken[input_start] = 32;
    reseal(broken);
    REQUIRE_FALSE(replay::decode(broken, out, error));
    broken = bytes;
    broken[broken.size() - 5] ^= 1;
    reseal(broken);
    REQUIRE_FALSE(replay::decode(broken, out, error));
}
TEST_CASE("Classic replay rejects previous provisional rules with an explanatory identity error") {
    replay::Replay r{classic::initial_state(5), {0}};
    std::string error;
    std::vector<std::uint8_t> bytes;
    REQUIRE(replay::encode(r, bytes, error));
    std::string previous(reinterpret_cast<const char*>(bytes.data() + 24), u32(bytes, 8));
    previous.replace(previous.find("v3;"), 3, "v2;");
    const std::string spawn_rule = "spawn-rotation=I0,O0,T2,S0,Z0,J2,L2;";
    previous.erase(previous.find(spawn_rule), spawn_rule.size());
    const auto old_length = u32(bytes, 8);
    bytes.erase(bytes.begin() + 24, bytes.begin() + 24 + old_length);
    bytes.insert(bytes.begin() + 24, previous.begin(), previous.end());
    for (unsigned i = 0; i < 4; ++i)
        bytes[8 + i] = static_cast<std::uint8_t>(previous.size() >> (8 * i));
    reseal(bytes);
    replay::Replay restored;
    REQUIRE_FALSE(replay::decode(bytes, restored, error));
    REQUIRE(error == "unsupported rules identity");
    auto state = classic::canonical_bytes(r.initial);
    state[0] = 2;
    classic::State out;
    REQUIRE_FALSE(classic::decode_state(state, out));
}
TEST_CASE("Classic replay checks intermediate checkpoints and invalid recording") {
    replay::Replay r{classic::initial_state(1), std::vector<std::uint32_t>(1201, 0)};
    std::string error;
    std::vector<std::uint8_t> bytes;
    REQUIRE(replay::encode(r, bytes, error));
    const auto hashes = 24 + u32(bytes, 8) + u32(bytes, 12) + r.inputs.size() * 4;
    auto broken = bytes;
    broken[hashes + 12 + 4] ^= 1;
    reseal(broken);
    replay::Replay loaded;
    REQUIRE_FALSE(replay::decode(broken, loaded, error));
    broken = bytes;
    broken[hashes + 12] ^= 1;
    reseal(broken);
    REQUIRE_FALSE(replay::decode(broken, loaded, error));
    r.inputs[0] = 32;
    REQUIRE_FALSE(replay::encode(r, bytes, error));
    r.inputs = {0};
    r.initial.tick = std::numeric_limits<std::uint64_t>::max();
    REQUIRE_FALSE(replay::encode(r, bytes, error));
    r.initial = classic::initial_state(1);
    r.initial.rng = 0;
    REQUIRE_FALSE(replay::encode(r, bytes, error));
    r.initial = classic::initial_state(1);
    r.inputs.resize(replay::max_frames + 1);
    REQUIRE_FALSE(replay::encode(r, bytes, error));
}
TEST_CASE("Classic replay preserves a topout that completes a visible row") {
    auto initial = classic::initial_state(1);
    initial.active = {classic::Piece::O, 0, 3, -1};
    for (unsigned x = 0; x < 10; ++x)
        if (x != 4 && x != 5)
            initial.board[x] = 1;
    initial.board[14] = 1;
    initial.gravity_timer = 47;
    REQUIRE(classic::valid_state(initial));
    replay::Replay r{initial, {0, 0}};
    classic::State final;
    std::string error;
    REQUIRE(replay::verify(r, final, error));
    REQUIRE(final.game_over);
    REQUIRE(classic::valid_state(final));
    for (unsigned x = 0; x < 10; ++x)
        REQUIRE(final.board[x] != 0);
    std::vector<std::uint8_t> bytes;
    REQUIRE(replay::encode(r, bytes, error));
    replay::Replay restored;
    REQUIRE(replay::decode(bytes, restored, error));
    classic::State roundtrip;
    REQUIRE(replay::verify(restored, roundtrip, error));
    REQUIRE(roundtrip == final);
}
