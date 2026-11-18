#include <catch2/catch_test_macros.hpp>
#include <stackpulse/input.hpp>
#include <stdexcept>

using namespace stackpulse::input;
namespace { constexpr auto left = static_cast<std::size_t>(Action::left); }

TEST_CASE("sample boundaries preserve rapid taps without generating tick actions", "[input]") {
    Adapter input;
    REQUIRE(input.push({11, 1, 1, Action::left, true}));
    REQUIRE(input.push({19, 1, 1, Action::left, false}));
    const auto tick = input.sample(20);
    REQUIRE(tick.transitions.size() == 2);
    REQUIRE_FALSE(tick.held[left]);
    REQUIRE_FALSE(tick.pressed[left]);
    REQUIRE_FALSE(tick.released[left]);
    REQUIRE(input.push({30, 1, 1, Action::left, true}));
    REQUIRE(input.sample(29).transitions.empty());
    REQUIRE(input.sample(30).pressed[left]);
    REQUIRE_FALSE(input.push({30, 1, 1, Action::left, false}));
    REQUIRE_THROWS_AS(input.sample(30), std::invalid_argument);
}
TEST_CASE("overlapping bindings and devices release independently", "[input]") {
    Adapter input;
    input.push({1, 1, 1, Action::left, true});
    input.push({2, 1, 2, Action::left, true});
    input.push({3, 2, 1, Action::left, true});
    REQUIRE(input.sample(3).pressed[left]);
    input.push({4, 1, 1, Action::left, false});
    REQUIRE(input.sample(4).held[left]);
    input.release_device(5, 1);
    auto disconnected = input.sample(5);
    REQUIRE(disconnected.held[left]);
    REQUIRE(disconnected.transitions.size() == 1);
    REQUIRE(disconnected.transitions.front().synthetic);
    input.release_device(6, 2);
    REQUIRE(input.sample(6).released[left]);
}
TEST_CASE("repeat and duplicate events do not accumulate reference counts", "[input]") {
    Adapter input;
    input.push({1, 1, 1, Action::left, true}, true);
    REQUIRE_FALSE(input.sample(1).held[left]);
    input.push({2, 1, 1, Action::left, true});
    input.push({3, 1, 1, Action::left, true});
    input.push({4, 1, 1, Action::left, true}, true);
    REQUIRE(input.sample(4).transitions.size() == 1);
    input.push({5, 1, 1, Action::left, false});
    REQUIRE(input.sample(5).released[left]);
}
TEST_CASE("focus release respects queued time order and clears all devices", "[input]") {
    Adapter input;
    input.push({9, 2, 1, Action::left, true});
    input.push({3, 1, 1, Action::left, true});
    input.release_all(10);
    const auto tick = input.sample(10);
    REQUIRE(tick.transitions.size() == 4);
    REQUIRE(tick.transitions.front().timestamp == 3);
    REQUIRE_FALSE(tick.held[left]);
    input.push({11, 1, 1, Action::left, true});
    REQUIRE(input.sample(11).pressed[left]);
}
TEST_CASE("release then press in a held tick is diagnostic only", "[input]") {
    Adapter input;
    input.push({1, 1, 1, Action::left, true});
    input.sample(1);
    input.push({2, 1, 1, Action::left, false});
    input.push({3, 1, 1, Action::left, true});
    const auto tick = input.sample(3);
    REQUIRE(tick.held[left]);
    REQUIRE_FALSE(tick.pressed[left]);
    REQUIRE_FALSE(tick.released[left]);
    REQUIRE(tick.transitions.size() == 2);
}
TEST_CASE("axis hysteresis has separate engage and release thresholds", "[input]") {
    AxisHysteresis axis;
    REQUIRE(axis.update(0.54F) == 0);
    REQUIRE(axis.update(0.56F) == 1);
    REQUIRE(axis.update(0.40F) == 1);
    REQUIRE(axis.update(0.34F) == 0);
    REQUIRE(axis.update(-0.8F) == -1);
    REQUIRE(axis.update(0.8F) == 1);
    axis.reset();
    REQUIRE(axis.update(0.4F) == 0);
    REQUIRE_THROWS_AS(AxisHysteresis(0.2F, 0.3F), std::invalid_argument);
}

TEST_CASE("late physical release can be rescheduled without losing source timestamp", "[input]") {
    Adapter input;
    input.push({1, 1, 1, Action::left, true});
    REQUIRE(input.sample(20).held[left]);
    Transition delayed{19, 1, 1, Action::left, false};
    REQUIRE_FALSE(input.push(delayed));
    delayed.source_timestamp = delayed.timestamp;
    delayed.timestamp = 21; // Native shell policy: first unsampled instant.
    REQUIRE(input.push(delayed));
    const auto tick = input.sample(40);
    REQUIRE(tick.released[left]);
    REQUIRE(tick.transitions.front().source_timestamp == 19);
    REQUIRE(tick.transitions.front().timestamp == 21);
}
