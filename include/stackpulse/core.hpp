#pragma once
#include <array>
#include <cstdint>

// M0 feasibility diagnostic. This is not a falling-block game or Classic rules.
namespace stackpulse::diagnostic {
enum Action : std::uint32_t { left = 1, right = 2, down = 4, rotate = 8 };
inline constexpr std::uint32_t action_mask = 15;
struct InputFrame { std::uint32_t held{}, pressed{}, released{}; };
struct State {
    std::uint64_t tick{};
    std::uint32_t rng{0x6d2b79f5u};
    std::int32_t x{4}, y{};
    std::uint32_t held{};
    std::uint64_t presses{};
    bool operator==(const State&) const = default;
};
enum class Error { none, invalid_input };
struct StepResult { Error error{}; bool moved{}; std::uint32_t pressed_mask{}; };
[[nodiscard]] State initial_state(std::uint32_t seed) noexcept;
[[nodiscard]] StepResult step(State& state, InputFrame input) noexcept;
[[nodiscard]] std::array<std::uint8_t, 36> canonical_bytes(const State& state) noexcept;
[[nodiscard]] std::uint64_t state_hash(const State& state) noexcept;
}
