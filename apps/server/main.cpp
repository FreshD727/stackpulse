#include <charconv>
#include <chrono>
#include <cstdio>
#include <stackpulse/network.hpp>
#include <string_view>
#include <thread>
int main(int argc, char** argv) {
    stackpulse::net::ServerOptions options;
    std::uint64_t limit = 0;
    for (int i = 1; i < argc; ++i) {
        std::string_view arg = argv[i];
        if (arg == "--bind" && i + 1 < argc)
            options.bind = argv[++i];
        else if (arg == "--mode" && i + 1 < argc) {
            std::string_view mode = argv[++i];
            if (mode == "fusion")
                options.preset = stackpulse::modern::Preset::fusion;
            else if (mode == "classic")
                options.preset = stackpulse::modern::Preset::classic;
            else if (mode == "modern")
                options.preset = stackpulse::modern::Preset::modern;
            else
                return 2;
        } else if ((arg == "--port" || arg == "--ticks" || arg == "--players" ||
                    arg == "--spectators" || arg == "--rooms") &&
                   i + 1 < argc) {
            std::string_view value = argv[++i];
            std::uint64_t n{};
            auto result = std::from_chars(value.data(), value.data() + value.size(), n);
            if (result.ec != std::errc{} || result.ptr != value.data() + value.size())
                return 2;
            if (arg == "--port") {
                if (!n || n > 65535)
                    return 2;
                options.port = static_cast<std::uint16_t>(n);
            } else if (arg == "--players") {
                if (n < 2 || n > 8)
                    return 2;
                options.players = static_cast<std::uint8_t>(n);
            } else if (arg == "--spectators") {
                if (n > 16)
                    return 2;
                options.spectators = static_cast<std::uint8_t>(n);
            } else if (arg == "--rooms") {
                if (!n || n > 8)
                    return 2;
                options.rooms = static_cast<std::uint8_t>(n);
            } else
                limit = n;
        } else
            return 2;
    }
    stackpulse::net::Server server(options);
    if (!server.valid()) {
        std::fprintf(stderr, "%s\n", server.error().c_str());
        return 1;
    }
    std::printf("Stackpulse rooms UDP %s:%u rules v%u all room modes (default %s)\n",
                options.bind.c_str(),
                options.port,
                stackpulse::modern::rules_version,
                options.preset == stackpulse::modern::Preset::fusion    ? "fusion"
                : options.preset == stackpulse::modern::Preset::classic ? "classic"
                                                                        : "modern");
    std::fflush(stdout);
    const auto start = std::chrono::steady_clock::now();
    std::uint64_t ticks = 0;
    bool result_logged = false;
    while (!limit || ticks < limit) {
        server.tick();
        if (!server.valid()) {
            std::fprintf(stderr, "%s\n", server.error().c_str());
            return 1;
        }
        if (server.view().phase != stackpulse::net::Phase::results)
            result_logged = false;
        if (!result_logged && server.view().phase == stackpulse::net::Phase::results) {
            std::printf("AUTHORITATIVE_RESULT tick=%llu result=%s hash=%016llx\n",
                        static_cast<unsigned long long>(server.view().authoritative.tick),
                        server.view().authoritative.result == stackpulse::match::Result::draw
                            ? "draw"
                            : "winner",
                        static_cast<unsigned long long>(
                            stackpulse::match::state_hash(server.view().authoritative)));
            std::fflush(stdout);
            result_logged = true;
        }
        ++ticks;
        std::this_thread::sleep_until(start + std::chrono::nanoseconds(ticks * 1000000000 / 60));
    }
    if (server.view().has_snapshot)
        std::printf("authoritative tick=%llu hash=%016llx result=%u\n",
                    static_cast<unsigned long long>(server.view().authoritative.tick),
                    static_cast<unsigned long long>(
                        stackpulse::match::state_hash(server.view().authoritative)),
                    static_cast<unsigned>(server.view().authoritative.result));
}
