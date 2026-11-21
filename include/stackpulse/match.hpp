#pragma once
#include <stackpulse/modern.hpp>
#include <string>
namespace stackpulse::match {
enum class Result : std::uint8_t { playing, winner, draw };
struct Player {
    std::uint16_t id{}, target{};
    modern::State board;
    std::uint8_t placement{};
    bool disconnected{};
    std::uint64_t deadline{};
    bool operator==(const Player&) const = default;
};
struct Arrival {
    std::uint64_t delivery{};
    std::uint16_t recipient{};
    modern::Packet packet;
    bool operator==(const Arrival&) const = default;
};
struct State {
    std::uint64_t tick{}, next_packet_id{1};
    std::vector<Player> players;
    std::vector<Arrival> scheduled;
    Result result{Result::playing};
    std::uint64_t result_tick{};
    bool operator==(const State&) const = default;
};
struct Events {
    std::vector<modern::Events> boards;
    std::vector<Arrival> dispatched;
    std::vector<std::uint16_t> eliminated;
    bool result_generated{};
};
// IDs must be nonzero, strictly ascending, count 2..8. Invalid creation throws invalid_argument.
State initial_state(const std::vector<std::uint16_t>& ids,
                    std::uint32_t seed,
                    modern::Preset preset = modern::Preset::modern,
                    std::uint32_t start_level = 0);
bool valid_state(const State&) noexcept;
// One held mask per roster player. Invalid/capacity/overflow errors preserve state and events.
bool advance(State&, const std::vector<std::uint32_t>& held, Events*, std::string& error);
bool disconnect(State&, std::uint16_t id);
bool reconnect(State&, std::uint16_t id);
std::vector<std::uint8_t> canonical_bytes(const State&);
bool decode_state(const std::vector<std::uint8_t>&, State&);
std::uint64_t state_hash(const State&);
} // namespace stackpulse::match
