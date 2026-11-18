#include <stackpulse/core.hpp>
#include <catch2/catch_test_macros.hpp>
#include <limits>
using namespace stackpulse::diagnostic;

TEST_CASE("xorshift seed contract and canonical little endian fields") {
    auto state = initial_state(1);
    REQUIRE(step(state, {}).error == Error::none);
    REQUIRE(state.rng == 270369u);
    REQUIRE(step(state, {}).error == Error::none);
    REQUIRE(state.rng == 67634689u);
    REQUIRE(step(state, {}).error == Error::none);
    REQUIRE(state.rng == 2647435461u);
    REQUIRE(initial_state(0).rng == 0x6d2b79f5u);
    auto bytes = canonical_bytes(initial_state(1));
    REQUIRE(bytes.size() == 36);
    REQUIRE(bytes[0] == 1);
    REQUIRE(bytes[12] == 1);
    REQUIRE(bytes[16] == 4);
    REQUIRE(bytes[35] == 0);
}
TEST_CASE("diagnostic bounds ties and rejected input") {
    auto state = initial_state(42);
    for (int i = 0; i < 100; ++i) {
        REQUIRE(step(state, {left | down, left | down, 0}).error == Error::none);
        REQUIRE(step(state, {0, 0, left | down}).error == Error::none);
    }
    REQUIRE(state.x == 0); REQUIRE(state.y == 19); REQUIRE(state.presses == 200);
    REQUIRE_FALSE(step(state, {left | right, left | right, 0}).moved);
    const auto saved = state;
    REQUIRE(step(state, {16, 0, 0}).error == Error::invalid_input);
    REQUIRE(state == saved);
    REQUIRE(step(state, {0, left, 0}).error == Error::invalid_input);
    REQUIRE(state == saved);
    REQUIRE(step(state, {left, 0, left}).error == Error::invalid_input);
    REQUIRE(state == saved);
}
TEST_CASE("state restoration preserves all deterministic evolution") {
    for (std::uint32_t seed = 1; seed <= 100; ++seed) {
        auto state = initial_state(seed);
        for (unsigned i = 0; i < 1000; ++i) REQUIRE(step(state, {}).error == Error::none);
        auto restored = state;
        for (unsigned i = 0; i < 1000; ++i) {
            const auto held = i % 2 == 0 ? right : 0u;
            const InputFrame frame{held, held & ~state.held, state.held & ~held};
            REQUIRE(step(state, frame).error == Error::none);
            REQUIRE(step(restored, frame).error == Error::none);
        }
        REQUIRE(state_hash(state) == state_hash(restored));
    }
}
TEST_CASE("unsigned counters wrap explicitly") {
    auto state = initial_state(1);
    state.tick = std::numeric_limits<std::uint64_t>::max();
    state.presses = std::numeric_limits<std::uint64_t>::max();
    REQUIRE(step(state, {rotate, rotate, 0}).error == Error::none);
    REQUIRE(state.tick == 0); REQUIRE(state.presses == 0);
}
