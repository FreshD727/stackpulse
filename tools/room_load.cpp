#include <algorithm>
#include <charconv>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <memory>
#include <stackpulse/network.hpp>
#include <string_view>
#include <thread>
#ifdef _WIN32
#include <windows.h>

// PSAPI declarations require the Windows types above.
#include <psapi.h>
#endif

namespace {
using Clock = std::chrono::steady_clock;
std::uint64_t peak_memory() {
#ifdef _WIN32
    PROCESS_MEMORY_COUNTERS info{};
    return GetProcessMemoryInfo(GetCurrentProcess(), &info, sizeof(info))
               ? static_cast<std::uint64_t>(info.PeakWorkingSetSize)
               : 0;
#else
    std::ifstream status("/proc/self/status");
    std::string key, rest;
    while (status >> key) {
        if (key == "VmHWM:") {
            std::uint64_t kb{};
            status >> kb;
            return kb * 1024;
        }
        std::getline(status, rest);
    }
    return 0;
#endif
}
double percentile(std::vector<double> values, double fraction) {
    if (values.empty())
        return 0;
    std::sort(values.begin(), values.end());
    return values[static_cast<std::size_t>(fraction * static_cast<double>(values.size() - 1))];
}
} // namespace

int main(int argc, char** argv) {
    namespace net = stackpulse::net;
    namespace modern = stackpulse::modern;
    unsigned seconds = 1800;
    std::uint16_t port = 28992;
    std::string output = "out/room-soak.json";
    auto preset = modern::Preset::modern;
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg = argv[i];
        if (arg == "--output" && i + 1 < argc)
            output = argv[++i];
        else if (arg == "--mode" && i + 1 < argc) {
            const std::string_view mode = argv[++i];
            if (mode != "classic" && mode != "fusion" && mode != "modern")
                return 2;
            preset = mode == "classic"  ? modern::Preset::classic
                     : mode == "fusion" ? modern::Preset::fusion
                                        : modern::Preset::modern;
        } else if ((arg == "--seconds" || arg == "--port") && i + 1 < argc) {
            const std::string_view value = argv[++i];
            unsigned parsed{};
            const auto result = std::from_chars(value.data(), value.data() + value.size(), parsed);
            if (result.ec != std::errc{} || result.ptr != value.data() + value.size() || !parsed ||
                (arg == "--port" && parsed > 65535) || (arg == "--seconds" && parsed > 86400))
                return 2;
            if (arg == "--port")
                port = static_cast<std::uint16_t>(parsed);
            else
                seconds = parsed;
        } else
            return 2;
    }
    net::ServerOptions options;
    options.port = port;
    options.preset = preset;
    options.players = 8;
    options.spectators = 16;
    net::Server server(options);
    if (!server.valid()) {
        std::fprintf(stderr, "%s\n", server.error().c_str());
        return 1;
    }
    std::vector<std::unique_ptr<net::Client>> peers;
    for (unsigned i = 0; i < 24; ++i) {
        auto peer = std::make_unique<net::Client>();
        net::ConnectOptions connect;
        connect.port = port;
        connect.preset = preset;
        connect.name = "Load" + std::to_string(i + 1);
        connect.role = i < 8 ? net::Role::player : net::Role::spectator;
        if (!peer->connect(connect))
            return 1;
        peers.push_back(std::move(peer));
    }
    const auto started = Clock::now();
    auto next = started;
    std::vector<double> tick_times;
    std::vector<bool> ready_sent(24, false);
    std::vector<std::vector<net::Input>> rolling(24);
    std::vector<std::uint32_t> sequences(24);
    std::uint64_t ticks = 0, completed = 0, result_match = 0, previous_match = 0;
    std::uint64_t convergence_checks = 0;
    std::size_t maximum_inflight = 0, maximum_connections = 0;
    bool admitted = false, passed = true;
    std::string failure;
    auto fail = [&](const std::string& detail) {
        passed = false;
        failure = detail;
        std::fprintf(stderr, "%s\n", detail.c_str());
    };
    while (passed && Clock::now() - started < std::chrono::seconds(seconds)) {
        for (auto& peer : peers)
            peer->poll();
        const auto all_connected = std::all_of(peers.begin(), peers.end(), [](const auto& peer) {
            return peer->view().connected && peer->view().members.size() == 24;
        });
        admitted |= all_connected;
        if (!admitted && Clock::now() - started > std::chrono::seconds(10)) {
            fail("8 players and 16 spectators were not admitted within ten seconds");
            break;
        }
        if (admitted) {
            for (const auto& peer : peers)
                if (!peer->view().connected || peer->view().phase == net::Phase::error) {
                    fail("Capacity peer disconnected: " + peer->view().detail);
                    break;
                }
            const auto& authority = server.view();
            if (authority.match_id != previous_match) {
                previous_match = authority.match_id;
                for (auto& frames : rolling)
                    frames.clear();
            }
            for (std::size_t i = 0; i < peers.size(); ++i) {
                auto& peer = *peers[i];
                const auto& view = peer.view();
                if (view.phase == net::Phase::waiting && !view.ready && !ready_sent[i] &&
                    view.role == net::Role::player) {
                    peer.set_ready(true);
                    ready_sent[i] = true;
                }
                if (view.phase != net::Phase::waiting)
                    ready_sent[i] = false;
                if (authority.phase == net::Phase::playing && view.phase == net::Phase::playing &&
                    view.role == net::Role::player) {
                    auto& frames = rolling[i];
                    const auto tick = authority.authoritative.tick + 3;
                    if (frames.empty() || frames.back().tick < tick) {
                        const auto held =
                            (tick % 4 < 2 ? std::uint32_t(modern::down) : 0u) |
                            (preset == modern::Preset::modern && tick % 8 == 0 ? modern::hard_drop
                                                                               : 0u);
                        frames.push_back({tick, ++sequences[i], held});
                        if (frames.size() > 8)
                            frames.erase(frames.begin());
                        peer.submit(frames);
                    }
                }
            }
            if (authority.phase == net::Phase::results && authority.match_id != result_match) {
                const auto hash = stackpulse::match::state_hash(authority.authoritative);
                const auto converged =
                    std::all_of(peers.begin(), peers.end(), [&](const auto& peer) {
                        const auto& view = peer->view();
                        return view.match_id == authority.match_id &&
                               view.phase == net::Phase::results && view.has_snapshot &&
                               stackpulse::match::state_hash(view.authoritative) == hash;
                    });
                if (converged) {
                    convergence_checks += peers.size();
                    result_match = authority.match_id;
                    ++completed;
                    for (auto& peer : peers)
                        if (peer->view().member_id == peer->view().manager_id) {
                            peer->return_to_lobby();
                            break;
                        }
                }
            }
        }
        const auto before_tick = Clock::now();
        server.tick();
        tick_times.push_back(
            std::chrono::duration<double, std::micro>(Clock::now() - before_tick).count());
        const auto metrics = server.metrics();
        maximum_inflight = std::max(maximum_inflight, metrics.snapshots_inflight);
        maximum_connections = std::max(maximum_connections, metrics.connections);
        if (!server.valid())
            fail(server.error());
        ++ticks;
        next = started + std::chrono::nanoseconds(ticks * 1'000'000'000 / 60);
        std::this_thread::sleep_until(next);
    }
    if (!admitted || !completed)
        fail("No complete 24-peer match converged");
    const auto elapsed = std::chrono::duration<double>(Clock::now() - started).count();
    const auto metrics = server.metrics();
    std::ofstream file(output);
    if (!file)
        return 1;
    file << "{\n  \"passed\": " << (passed ? "true" : "false")
         << ",\n  \"players\": 8,\n  \"spectators\": 16,\n  \"elapsed_seconds\": " << elapsed
         << ",\n  \"server_ticks\": " << ticks << ",\n  \"completed_matches\": " << completed
         << ",\n  \"terminal_convergence_checks\": " << convergence_checks
         << ",\n  \"server_tick_p50_us\": " << percentile(tick_times, .50)
         << ",\n  \"server_tick_p95_us\": " << percentile(tick_times, .95)
         << ",\n  \"server_tick_p99_us\": " << percentile(tick_times, .99)
         << ",\n  \"server_tick_max_us\": " << percentile(tick_times, 1)
         << ",\n  \"server_sent_bytes\": " << metrics.sent_bytes
         << ",\n  \"server_received_bytes\": " << metrics.received_bytes
         << ",\n  \"server_send_bytes_per_second\": "
         << static_cast<double>(metrics.sent_bytes) / elapsed
         << ",\n  \"maximum_connections\": " << maximum_connections
         << ",\n  \"maximum_snapshot_transfers_inflight\": " << maximum_inflight
         << ",\n  \"combined_process_peak_working_set_bytes\": " << peak_memory() << "\n}\n";
    std::printf("room load: %.1fs, %llu matches, p99 %.1fus, passed=%d\n",
                elapsed,
                static_cast<unsigned long long>(completed),
                percentile(tick_times, .99),
                passed);
    return passed ? 0 : 1;
}
