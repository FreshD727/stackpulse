#include <charconv>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <stackpulse/modern.hpp>
#include <string>
#include <string_view>

namespace {
std::uint32_t random(std::uint32_t& value) {
    value ^= value << 13;
    value ^= value >> 17;
    value ^= value << 5;
    return value;
}
bool number(std::string_view text, std::uint32_t& value) {
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
    return result.ec == std::errc{} && result.ptr == text.data() + text.size() && value > 0;
}
} // namespace
int main(int argc, char** argv) {
    std::uint32_t seeds = 100, ticks = 100000;
    std::string output;
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg = argv[i];
        if (arg == "--output" && i + 1 < argc)
            output = argv[++i];
        else if (arg == "--seeds" && i + 1 < argc) {
            if (!number(argv[++i], seeds))
                return 2;
        } else if (arg == "--ticks" && i + 1 < argc) {
            if (!number(argv[++i], ticks))
                return 2;
        } else
            return 2;
    }
    std::ofstream file;
    if (!output.empty()) {
        file.open(output, std::ios::binary);
        if (!file)
            return 2;
    }
    std::ostream& out = output.empty() ? std::cout : file;
    out << "trace_version,rules_version,preset,seed,ticks,episodes,locks,checkpoints,state_hash,"
           "trace_hash\n";
    for (const auto preset :
         {stackpulse::modern::Preset::fusion, stackpulse::modern::Preset::modern}) {
        for (std::uint32_t index = 0; index < seeds; ++index) {
            const std::uint32_t seed = index + 1;
            auto state = stackpulse::modern::initial_state(seed, preset);
            std::uint32_t rng = seed ^ 0xa511e9b3u, episodes = 1, locks = 0, checkpoints = 0;
            std::uint64_t digest = 14695981039346656037ull;
            for (std::uint32_t tick = 0; tick < ticks; ++tick) {
                // Trace v1: xorshift32 once per tick; every mask is an exact integer operation.
                const auto value = random(rng);
                std::uint32_t input =
                    (value & 1u) ? stackpulse::modern::left : stackpulse::modern::right;
                if ((value & 7u) == 0)
                    input = 0;
                if ((value & 15u) == 2)
                    input |= stackpulse::modern::clockwise;
                if ((value & 31u) == 3)
                    input |= stackpulse::modern::counterclockwise;
                if ((value & 63u) == 4)
                    input |= stackpulse::modern::hold;
                if (tick % 13u == 0)
                    input |= stackpulse::modern::hard_drop;
                if ((value & 3u) == 0)
                    input |= stackpulse::modern::down;
                stackpulse::modern::Events events;
                if (!stackpulse::modern::step(state, input, &events))
                    return 1;
                if (events.locked)
                    ++locks;
                for (const auto byte : stackpulse::modern::canonical_bytes(state)) {
                    digest ^= byte;
                    digest *= 1099511628211ull;
                }
                if (tick % 997u == 0) {
                    stackpulse::modern::State restored;
                    if (!stackpulse::modern::decode_state(
                            stackpulse::modern::canonical_bytes(state), restored) ||
                        restored != state)
                        return 1;
                    auto expected = state;
                    if (!stackpulse::modern::step(expected, input) ||
                        !stackpulse::modern::step(restored, input) ||
                        stackpulse::modern::canonical_bytes(expected) !=
                            stackpulse::modern::canonical_bytes(restored))
                        return 1;
                    ++checkpoints;
                }
                // Restart only after hashing terminal state: keeps long traces exercising live
                // play.
                if (state.game_over && tick + 1u < ticks) {
                    state =
                        stackpulse::modern::initial_state(seed + episodes * 0x9e3779b9u, preset);
                    ++episodes;
                }
            }
            out << "1," << stackpulse::modern::rules_version << ','
                << (preset == stackpulse::modern::Preset::fusion ? "fusion" : "modern") << ','
                << seed << ',' << ticks << ',' << episodes << ',' << locks << ',' << checkpoints
                << ',' << std::hex << std::setfill('0') << std::setw(16)
                << stackpulse::modern::state_hash(state) << ',' << std::setw(16) << digest
                << std::dec << '\n';
        }
    }
    return out.good() ? 0 : 1;
}
