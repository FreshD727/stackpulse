#pragma once
#include <cstdint>
#include <limits>
#include <random>

namespace stackpulse::seeds {
// Keep entropy at the application boundary. The simulation/replay consumes only
// the selected integer. Reject zero (an alias of seed one) and immediate repeats.
template <class Draw> std::uint32_t choose(Draw&& draw, std::uint32_t previous = 0) {
    std::uint32_t seed{};
    do {
        seed = draw();
    } while (!seed || seed == previous);
    return seed;
}
inline std::uint32_t random(std::uint32_t previous = 0) {
    std::random_device entropy;
    std::uniform_int_distribution<std::uint32_t> distribution(
        1, (std::numeric_limits<std::uint32_t>::max)());
    return choose([&]() { return distribution(entropy); }, previous);
}
} // namespace stackpulse::seeds
