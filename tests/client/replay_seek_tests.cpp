#include "../../apps/client/replay_seek.hpp"
#include <catch2/catch_test_macros.hpp>
#include <limits>
#include <stackpulse/classic.hpp>
#include <stackpulse/modern.hpp>

namespace client = stackpulse::client;
namespace classic = stackpulse::classic;
namespace modern = stackpulse::modern;

namespace {
template <typename State, typename Step> void check_seeking(const State& initial, Step step) {
    std::vector<std::uint32_t> inputs;
    std::vector<State> expected{initial};
    auto state = initial;
    // Exercise held input edges, rotations, DAS, gravity, and later terminal ticks.
    for (std::size_t i = 0; i < 1907; ++i) {
        const auto mask = static_cast<std::uint32_t>((i / 23) % 8);
        REQUIRE(step(state, mask));
        inputs.push_back(mask);
        expected.push_back(state);
    }
    const auto original_inputs = inputs;
    client::ReplaySeek<State> seek;
    seek.reset(initial);
    for (const std::size_t frame : {1800U, 601U, 0U, 1907U, 299U, 1200U, 600U, 1906U}) {
        REQUIRE(seek.seek(inputs, frame, state, step));
        REQUIRE(state == expected[frame]);
        if (frame < inputs.size()) {
            REQUIRE(step(state, inputs[frame]));
            REQUIRE(state == expected[frame + 1]);
            seek.observe(frame + 1, state);
        }
    }
    REQUIRE(inputs == original_inputs);
    REQUIRE(seek.seek(inputs, std::numeric_limits<std::size_t>::max(), state, step));
    REQUIRE(state == expected.back());
    seek.reset(initial);
    REQUIRE(seek.seek({}, 0, state, step));
    REQUIRE(state == initial);
}
} // namespace

TEST_CASE("Classic replay seeks restore complete deterministic state", "[client][replay-seek]") {
    check_seeking(classic::initial_state(4251, 19),
                  [](auto& state, auto mask) { return classic::step(state, mask); });
    auto finished = classic::initial_state(42);
    finished.game_over = true;
    client::ReplaySeek<classic::State> seek;
    seek.reset(finished);
    auto restored = classic::initial_state(1);
    REQUIRE(seek.seek(
        {}, 0, restored, [](auto& state, auto mask) { return classic::step(state, mask); }));
    REQUIRE(restored == finished);
}

TEST_CASE("Fusion and Modern replay seeks preserve hold and garbage state",
          "[client][replay-seek]") {
    for (const auto preset : {modern::Preset::fusion, modern::Preset::modern}) {
        auto initial = modern::initial_state(54, preset);
        REQUIRE(modern::step(initial, modern::hold));
        REQUIRE(modern::enqueue(initial, {1, 0, 60, 1, 4, 4}));
        check_seeking(initial, [](auto& state, auto mask) { return modern::step(state, mask); });
    }
}

TEST_CASE("Seek controls clamp at replay bounds", "[client][replay-seek]") {
    REQUIRE(client::replay_seek_target(10, 100, -300) == 0);
    REQUIRE(client::replay_seek_target(10, 100, 300) == 100);
    REQUIRE(client::replay_seek_target(75, 100, -30) == 45);
    REQUIRE(client::replay_seek_target(0, 0, 300) == 0);
    REQUIRE(client::replay_seek_target(1, 100, std::numeric_limits<int>::min()) == 0);
    REQUIRE(client::replay_seek_target(1000, 100, -30) == 70);
}
