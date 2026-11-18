#include <stackpulse/core.hpp>
#include <iomanip>
#include <iostream>

int main() {
    using namespace stackpulse::diagnostic;
    auto state = initial_state(1);
    for (std::uint32_t tick = 0; tick < 100000; ++tick) {
        const auto held = tick % 4 == 0 ? right : tick % 4 == 2 ? left : 0u;
        const InputFrame frame{held, held & ~state.held, state.held & ~held};
        if (step(state, frame).error != Error::none) return 1;
    }
    std::cout << "M0 diagnostic only; ticks=" << state.tick << " hash="
              << std::hex << std::setfill('0') << std::setw(16) << state_hash(state) << '\n';
}
