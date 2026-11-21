#pragma once
#include <array>
#include <cstdint>
#include <vector>

namespace stackpulse::classic {
inline constexpr std::uint32_t rules_version = 4;
inline constexpr std::uint32_t tick_numerator = 60, tick_denominator = 1;
enum Action : std::uint32_t { left = 1, right = 2, down = 4, clockwise = 8, counterclockwise = 16 };
inline constexpr std::uint32_t action_mask = 31;
enum class Piece : std::uint8_t { I, O, T, S, Z, J, L };
// Spawn and next preview share orientation. I/S/Z toggle between two states.
[[nodiscard]] constexpr unsigned rotation_count(Piece piece) noexcept {
    return piece == Piece::O
               ? 1u
               : (piece == Piece::I || piece == Piece::S || piece == Piece::Z ? 2u : 4u);
}
[[nodiscard]] constexpr std::uint8_t spawn_rotation(Piece piece) noexcept {
    return piece == Piece::T || piece == Piece::J || piece == Piece::L ? 2 : 0;
}
struct Cell {
    std::int32_t x{}, y{};
    bool operator==(const Cell&) const = default;
};
struct Active {
    Piece piece{Piece::T};
    std::uint8_t rotation{};
    std::int32_t x{3}, y{-1};
    bool operator==(const Active&) const = default;
};
struct State {
    std::uint64_t tick{}, score{};
    std::uint32_t rng{1}, held{}, lines{}, level{}, start_level{};
    std::array<std::uint8_t, 200> board{};
    Active active{};
    Piece next{Piece::I};
    std::uint32_t gravity_timer{}, das_timer{}, entry_timer{};
    std::int32_t direction{};
    bool game_over{};
    bool soft_drop_armed{true};
    bool operator==(const State&) const = default;
};
// moved includes gravity; shifted/soft_dropped identify successful player movement.
struct Events {
    bool moved{}, shifted{}, soft_dropped{}, rotated{}, locked{}, spawned{}, topped_out{};
    std::uint32_t cleared{};
};
[[nodiscard]] State initial_state(std::uint32_t seed, std::uint32_t start_level = 0) noexcept;
[[nodiscard]] std::array<Cell, 4> cells(const Active&) noexcept;
[[nodiscard]] bool collides(const State&, const Active&) noexcept;
// False for invalid masks/state; state remains unchanged. Inputs are held masks.
bool step(State&, std::uint32_t held, Events* events = nullptr) noexcept;
[[nodiscard]] std::uint32_t transition_lines(std::uint32_t start_level) noexcept;
[[nodiscard]] std::uint32_t level_for_lines(std::uint32_t start_level,
                                            std::uint32_t lines) noexcept;
[[nodiscard]] std::uint32_t gravity_frames(std::uint32_t level) noexcept;
[[nodiscard]] std::vector<std::uint8_t> canonical_bytes(const State&);
bool decode_state(const std::vector<std::uint8_t>&, State&) noexcept;
[[nodiscard]] std::uint64_t state_hash(const State&) noexcept;
[[nodiscard]] bool valid_state(const State&) noexcept;
// Authored practice setup: four rows with one I-shaped gap; active I vertical.
[[nodiscard]] State clear_fixture(std::uint32_t seed) noexcept;
} // namespace stackpulse::classic
