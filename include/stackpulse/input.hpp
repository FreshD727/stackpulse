#pragma once

#include <array>
#include <cstdint>
#include <map>
#include <vector>

namespace stackpulse::input {
enum class Action : std::uint8_t {
    left,
    right,
    down,
    clockwise,
    counterclockwise,
    drop,
    hold,
    pause,
    count
};
inline constexpr std::size_t action_count = static_cast<std::size_t>(Action::count);
using Device = std::uint64_t;
using Timestamp = std::uint64_t; // Monotonic nanoseconds, same epoch as the platform events.
struct Transition {
    Timestamp timestamp{};
    Device device{};
    std::uint32_t binding{};
    Action action{};
    bool down{};
    bool synthetic{};
    Timestamp source_timestamp{}; // Optional original capture time if scheduling was delayed.
};
struct TickInput {
    Timestamp boundary{};
    std::array<bool, action_count> held{}, pressed{}, released{};
    std::vector<Transition> transitions;
};
// Boundary-inclusive sampling: events <= boundary are applied in timestamp / arrival
// order. Edges compare final held state with the previous sample. A complete tap
// between boundaries remains diagnostic data and does not grant a tick action.
// No direction arbitration, repeat generation, movement or game rules live here.
class Adapter {
  public:
    // Returns false for late events (at or before an already sampled boundary).
    bool push(Transition event, bool repeat = false);
    bool release_device(Timestamp timestamp, Device device);
    bool release_all(Timestamp timestamp);
    TickInput sample(Timestamp boundary); // throws for non-increasing boundaries
  private:
    enum class Kind { transition, device_release, all_release };
    struct Pending {
        Transition event;
        Kind kind;
    };
    std::vector<Pending> pending_;
    std::map<std::pair<Device, std::uint32_t>, Action> bindings_;
    std::array<unsigned, action_count> references_{};
    std::array<bool, action_count> previous_{};
    Timestamp boundary_{};
    bool sampled_{};
};
// Call once per axis event, after normalizing its signed range to [-1, 1].
// Separate instances are required per device/axis; reset them on focus loss.
class AxisHysteresis {
  public:
    AxisHysteresis(float engage = 0.55F, float release = 0.35F);
    int update(float value);
    void reset() {
        direction_ = 0;
    }

  private:
    float engage_, release_;
    int direction_{};
};
} // namespace stackpulse::input
