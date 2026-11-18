#include <stackpulse/core.hpp>
#include <algorithm>
#include <bit>
#include <cstddef>

namespace stackpulse::diagnostic {
State initial_state(std::uint32_t seed) noexcept {
    State state;
    state.rng = seed == 0 ? 0x6d2b79f5u : seed;
    return state;
}
StepResult step(State& state, InputFrame input) noexcept {
    if (((input.held | input.pressed | input.released) & ~action_mask) != 0 ||
        input.pressed != (input.held & ~state.held) ||
        input.released != (state.held & ~input.held)) {
        return {Error::invalid_input, false, 0};
    }
    const auto old_x = state.x;
    const auto old_y = state.y;
    const auto horizontal = input.pressed & (left | right);
    if (horizontal == left) state.x = std::max(0, state.x - 1);
    if (horizontal == right) state.x = std::min(9, state.x + 1);
    if ((input.pressed & down) != 0) state.y = std::min(19, state.y + 1);
    ++state.tick;
    state.rng ^= state.rng << 13;
    state.rng ^= state.rng >> 17;
    state.rng ^= state.rng << 5;
    state.held = input.held;
    state.presses += static_cast<std::uint64_t>(std::popcount(input.pressed));
    return {Error::none, state.x != old_x || state.y != old_y, input.pressed};
}
std::array<std::uint8_t, 36> canonical_bytes(const State& state) noexcept {
    std::array<std::uint8_t, 36> bytes{};
    std::size_t cursor = 0;
    auto append = [&](std::uint64_t value, unsigned width) {
        for (unsigned i = 0; i < width; ++i) {
            bytes[cursor++] = static_cast<std::uint8_t>(value & 255u);
            value >>= 8;
        }
    };
    append(1, 4); append(state.tick, 8); append(state.rng, 4);
    append(static_cast<std::uint32_t>(state.x), 4);
    append(static_cast<std::uint32_t>(state.y), 4);
    append(state.held, 4); append(state.presses, 8);
    return bytes;
}
std::uint64_t state_hash(const State& state) noexcept {
    std::uint64_t hash = 14695981039346656037ull;
    for (auto byte : canonical_bytes(state)) { hash ^= byte; hash *= 1099511628211ull; }
    return hash;
}
}
