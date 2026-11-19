#pragma once
#include <array>
#include <cstdint>
#include <vector>
namespace stackpulse::modern {
inline constexpr std::uint32_t rules_version = 1, action_mask = 255;
enum class Preset : std::uint8_t { fusion, modern };
enum Action : std::uint32_t {
    left = 1,
    right = 2,
    down = 4,
    clockwise = 8,
    counterclockwise = 16,
    hold = 32,
    hard_drop = 64,
    target_next = 128
};
enum class Piece : std::uint8_t { I, J, L, O, S, T, Z };
struct Cell {
    std::int32_t x{}, y{};
    bool operator==(const Cell&) const = default;
};
struct Active {
    Piece piece{Piece::T};
    std::uint8_t rotation{};
    std::int32_t x{3}, y{-2};
    bool operator==(const Active&) const = default;
};
struct Packet {
    std::uint64_t id{}, creation{}, eligible{};
    std::uint16_t source{}, rows{};
    std::uint8_t hole{};
    bool operator==(const Packet&) const = default;
};
struct State {
    Preset preset{Preset::modern};
    std::uint64_t tick{}, score{}, lines{};
    std::array<std::uint8_t, 400> board{};
    Active active{};
    std::uint32_t rng{1}, garbage_rng{1}, target_rng{1}, held{};
    std::array<Piece, 7> bag{};
    std::uint8_t bag_cursor{7};
    std::array<Piece, 5> next{};
    Piece held_piece{Piece::I};
    bool has_hold{}, hold_used{};
    std::uint64_t gravity_timer{}, das_timer{}, lock_elapsed{};
    std::int32_t direction{}, combo{-1};
    std::uint8_t lock_resets{}, last_kick{};
    bool last_rotation{}, back_to_back{}, game_over{};
    std::vector<Packet> incoming;
    std::vector<std::uint64_t> received_ids;
    bool operator==(const State&) const = default;
};
struct Events {
    bool moved{}, shifted{}, soft_dropped{}, rotated{}, held{}, locked{}, spawned{}, topped_out{},
        spin{}, perfect_clear{}, back_to_back{};
    std::uint32_t cleared{}, attack{}, canceled{}, inserted{};
    std::uint64_t score_delta{};
};
[[nodiscard]] State initial_state(std::uint32_t seed, Preset preset = Preset::modern);
[[nodiscard]] std::array<Cell, 4> cells(const Active&) noexcept;
[[nodiscard]] bool collides(const State&, const Active&) noexcept;
[[nodiscard]] Active ghost(const State&) noexcept;
[[nodiscard]] std::array<Cell, 5> kick_offsets(Piece, std::uint8_t from, std::uint8_t to) noexcept;
bool rotate(State&, bool clockwise) noexcept;
bool step(State&, std::uint32_t held, Events* events = nullptr);
// Arrival is authoritative/practice input; duplicates are idempotent, max 256 pending.
bool enqueue(State&, const Packet&);
[[nodiscard]] bool valid_state(const State&) noexcept;
[[nodiscard]] std::uint32_t level(const State&) noexcept;
[[nodiscard]] std::vector<std::uint8_t> canonical_bytes(const State&);
bool decode_state(const std::vector<std::uint8_t>&, State&);
[[nodiscard]] std::uint64_t state_hash(const State&);
// Independently authored, playable CW spin entry; rows=1,2,3. No pre-set spin marker.
[[nodiscard]] State spin_fixture(std::uint32_t seed, Preset, unsigned rows);
} // namespace stackpulse::modern
