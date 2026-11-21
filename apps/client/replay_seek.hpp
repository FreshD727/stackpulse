#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <map>
#include <utility>
#include <vector>

namespace stackpulse::client {
// Complete snapshots preserve DAS, randomizer, hold, and garbage state.
// The cache belongs to one loaded replay and never writes replay history.
template <typename State> class ReplaySeek {
  public:
    static constexpr std::size_t checkpoint_interval = 600;
    void reset(const State& initial) {
        checkpoints_.clear();
        checkpoints_.emplace(0, initial);
    }
    void observe(std::size_t frame, const State& state) {
        if (frame % checkpoint_interval == 0)
            checkpoints_.try_emplace(frame, state);
    }
    template <typename Step>
    bool
    seek(const std::vector<std::uint32_t>& inputs, std::size_t target, State& state, Step step) {
        if (checkpoints_.empty())
            return false;
        target = std::min(target, inputs.size());
        auto checkpoint = checkpoints_.upper_bound(target);
        --checkpoint;
        State restored = checkpoint->second;
        for (auto frame = checkpoint->first; frame < target; ++frame) {
            if (!step(restored, inputs[frame]))
                return false;
            observe(frame + 1, restored);
        }
        state = std::move(restored);
        return true;
    }

  private:
    std::map<std::size_t, State> checkpoints_;
};
inline std::size_t replay_seek_target(std::size_t current, std::size_t total, int delta) {
    current = std::min(current, total);
    if (delta < 0) {
        const auto distance = static_cast<std::size_t>(-static_cast<std::int64_t>(delta));
        return distance > current ? 0 : current - distance;
    }
    return current + std::min(static_cast<std::size_t>(delta), total - current);
}
} // namespace stackpulse::client
