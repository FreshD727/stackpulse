#include <stackpulse/input.hpp>

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace stackpulse::input {
bool Adapter::push(Transition event, bool repeat) {
    if (repeat)
        return true;
    if (event.action >= Action::count || (sampled_ && event.timestamp <= boundary_))
        return false;
    pending_.push_back({event, Kind::transition});
    return true;
}
bool Adapter::release_device(Timestamp timestamp, Device device) {
    if (sampled_ && timestamp <= boundary_)
        return false;
    pending_.push_back({{timestamp, device}, Kind::device_release});
    return true;
}
bool Adapter::release_all(Timestamp timestamp) {
    if (sampled_ && timestamp <= boundary_)
        return false;
    pending_.push_back({{timestamp}, Kind::all_release});
    return true;
}
TickInput Adapter::sample(Timestamp boundary) {
    if (sampled_ && boundary <= boundary_)
        throw std::invalid_argument("input boundaries must increase");
    TickInput result;
    result.boundary = boundary;
    std::stable_sort(pending_.begin(), pending_.end(), [](const auto& a, const auto& b) {
        return a.event.timestamp < b.event.timestamp;
    });
    auto end = pending_.begin();
    for (; end != pending_.end() && end->event.timestamp <= boundary; ++end) {
        const auto& event = end->event;
        if (end->kind != Kind::transition) {
            for (auto it = bindings_.begin(); it != bindings_.end();) {
                if (end->kind == Kind::all_release || it->first.first == event.device) {
                    --references_[static_cast<std::size_t>(it->second)];
                    result.transitions.push_back({event.timestamp,
                                                  it->first.first,
                                                  it->first.second,
                                                  it->second,
                                                  false,
                                                  true});
                    it = bindings_.erase(it);
                } else
                    ++it;
            }
            continue;
        }
        const auto key = std::pair{event.device, event.binding};
        auto found = bindings_.find(key);
        if (event.down && found == bindings_.end()) {
            bindings_.emplace(key, event.action);
            ++references_[static_cast<std::size_t>(event.action)];
            result.transitions.push_back(event);
        } else if (!event.down && found != bindings_.end()) {
            auto release = event;
            release.action = found->second; // Release the original binding even if remapped.
            --references_[static_cast<std::size_t>(found->second)];
            bindings_.erase(found);
            result.transitions.push_back(release);
        }
    }
    pending_.erase(pending_.begin(), end);
    for (std::size_t i = 0; i < action_count; ++i) {
        result.held[i] = references_[i] != 0;
        result.pressed[i] = result.held[i] && !previous_[i];
        result.released[i] = !result.held[i] && previous_[i];
    }
    previous_ = result.held;
    boundary_ = boundary;
    sampled_ = true;
    return result;
}
AxisHysteresis::AxisHysteresis(float engage, float release) : engage_(engage), release_(release) {
    if (!(release >= 0 && release < engage && engage <= 1))
        throw std::invalid_argument("invalid stick thresholds");
}
int AxisHysteresis::update(float value) {
    if (!std::isfinite(value))
        value = 0;
    if (value >= engage_)
        direction_ = 1;
    else if (value <= -engage_)
        direction_ = -1;
    else if ((direction_ == 1 && value <= release_) || (direction_ == -1 && value >= -release_))
        direction_ = 0;
    return direction_;
}
} // namespace stackpulse::input
