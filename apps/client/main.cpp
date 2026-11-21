#include "audio.hpp"
#ifdef STACKPULSE_NETWORK
#include "online.hpp"
#endif
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <algorithm>
#include <array>
#include <charconv>
#include <cstdio>
#include <deque>
#include <filesystem>
#include <fstream>
#include <map>
#include <optional>
#include <stackpulse/classic.hpp>
#include <stackpulse/input.hpp>
#include <stackpulse/modern_replay.hpp>
#include <stackpulse/replay.hpp>
#include <string>
#include <string_view>

namespace {
namespace game = stackpulse::classic;
namespace in = stackpulse::input;
namespace replay = stackpulse::replay;
namespace modern = stackpulse::modern;
namespace mr = stackpulse::modern_replay;
namespace client = stackpulse::client;
constexpr in::Device keyboard = 1ULL << 63;
constexpr std::array<const char*, 8> names{
    "LEFT", "RIGHT", "SOFT DOWN", "ROTATE CW", "ROTATE CCW", "PAUSE", "HARD DROP", "HOLD"};
constexpr std::array<in::Action, 8> actions{in::Action::left,
                                            in::Action::right,
                                            in::Action::down,
                                            in::Action::clockwise,
                                            in::Action::counterclockwise,
                                            in::Action::pause,
                                            in::Action::drop,
                                            in::Action::hold};
struct Settings {
    std::array<int, 8> keys{SDL_SCANCODE_LEFT,
                            SDL_SCANCODE_RIGHT,
                            SDL_SCANCODE_DOWN,
                            SDL_SCANCODE_X,
                            SDL_SCANCODE_Z,
                            SDL_SCANCODE_ESCAPE,
                            SDL_SCANCODE_SPACE,
                            SDL_SCANCODE_C};
    std::array<int, 8> buttons{SDL_GAMEPAD_BUTTON_DPAD_LEFT,
                               SDL_GAMEPAD_BUTTON_DPAD_RIGHT,
                               SDL_GAMEPAD_BUTTON_DPAD_DOWN,
                               SDL_GAMEPAD_BUTTON_SOUTH,
                               SDL_GAMEPAD_BUTTON_WEST,
                               SDL_GAMEPAD_BUTTON_START,
                               SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER,
                               SDL_GAMEPAD_BUTTON_LEFT_SHOULDER};
    bool sticks{};
    std::uint32_t start_level{};
    void load(const std::filesystem::path& path) {
        std::ifstream file(path);
        int version = 0, stick = 0;
        Settings candidate;
        if (!(file >> version) || (version < 1 || version > 3))
            return;
        for (std::size_t i = 0; i < (version == 3 ? 8U : 6U); ++i)
            if (!(file >> candidate.keys[i]) || candidate.keys[i] <= SDL_SCANCODE_UNKNOWN ||
                candidate.keys[i] >= SDL_SCANCODE_COUNT)
                return;
        for (std::size_t i = 0; i < (version == 3 ? 8U : 6U); ++i)
            if (!(file >> candidate.buttons[i]) || candidate.buttons[i] < 0 ||
                candidate.buttons[i] >= SDL_GAMEPAD_BUTTON_COUNT)
                return;
        if (!(file >> stick) || (stick != 0 && stick != 1))
            return;
        if (version < 3) {
            for (std::size_t slot = 6; slot < 8; ++slot) {
                while (std::find(candidate.keys.begin(),
                                 candidate.keys.begin() + slot,
                                 candidate.keys[slot]) != candidate.keys.begin() + slot)
                    candidate.keys[slot] = candidate.keys[slot] % (SDL_SCANCODE_COUNT - 1) + 1;
                while (std::find(candidate.buttons.begin(),
                                 candidate.buttons.begin() + slot,
                                 candidate.buttons[slot]) != candidate.buttons.begin() + slot)
                    candidate.buttons[slot] =
                        (candidate.buttons[slot] + 1) % SDL_GAMEPAD_BUTTON_COUNT;
            }
        }
        for (std::size_t a = 0; a < 8; ++a)
            for (std::size_t b = a + 1; b < 8; ++b)
                if (candidate.keys[a] == candidate.keys[b] ||
                    candidate.buttons[a] == candidate.buttons[b])
                    return;
        if (version >= 2 && (!(file >> candidate.start_level) || candidate.start_level > 29))
            return;
        candidate.sticks = stick != 0;
        *this = candidate;
    }
    bool save(const std::filesystem::path& path) const {
        std::ofstream file(path);
        file << "3\n";
        for (int value : keys)
            file << value << ' ';
        file << '\n';
        for (int value : buttons)
            file << value << ' ';
        file << '\n' << sticks << '\n' << start_level << '\n';
        file.close();
        return static_cast<bool>(file);
    }
};
struct Pad {
    SDL_Gamepad* handle{};
    std::array<in::AxisHysteresis, 2> axes;
};
enum class Screen { home, play, pause, settings };
void color(SDL_Renderer* renderer, SDL_Color c) {
    SDL_SetRenderDrawColor(renderer, c.r, c.g, c.b, c.a);
}
constexpr SDL_Color ink{214, 228, 244, 255}, muted{125, 149, 178, 255}, mint{82, 235, 185, 255};
void rect(SDL_Renderer* r, float x, float y, float w, float h, SDL_Color c) {
    color(r, c);
    SDL_FRect box{x, y, w, h};
    SDL_RenderFillRect(r, &box);
}
void text(SDL_Renderer* r, float x, float y, const std::string& value, SDL_Color c = ink) {
    color(r, c);
    SDL_RenderDebugText(r, x, y, value.c_str());
}
void block(SDL_Renderer* r,
           float x,
           float y,
           game::Piece piece,
           std::uint32_t gravity_interval,
           float size = 14) {
    const auto c = client::piece_color(piece, gravity_interval);
    rect(r, x + 1, y + 1, size - 2, size - 2, c);
    rect(r, x + 2, y + 2, size - 4, 2, client::block_highlight(c));
    rect(r, x + 3, y + size - 4, 2, 2, {35, 50, 69, 255});
}
std::string number(std::uint64_t value) {
    return std::to_string(value);
}

#ifdef STACKPULSE_NETWORK
int run_online(SDL_Renderer* renderer,
               const Settings& settings,
               ClientAudio& audio,
               const std::string& host,
               std::uint16_t port,
               modern::Preset preset,
               bool bot,
               std::uint64_t exit_ticks,
               const std::filesystem::path& capture_path = {}) {
    namespace net = stackpulse::net;
    net::Client connection;
    client::OnlinePrediction prediction;
    if (!connection.connect(host, port, preset)) {
        std::fprintf(stderr, "Online connection: %s\n", connection.view().detail.c_str());
        return 1;
    }
    in::Adapter input;
    std::map<SDL_JoystickID, Pad> pads;
    auto open_pad = [&](SDL_JoystickID id) {
        if (!pads.contains(id))
            if (auto* handle = SDL_OpenGamepad(id))
                pads.emplace(id, Pad{handle, {}});
    };
    int count = 0;
    auto* ids = SDL_GetGamepads(&count);
    for (int i = 0; i < count; ++i)
        open_pad(ids[i]);
    SDL_free(ids);
    auto boundary = SDL_GetTicksNS();
    auto next_tick = boundary + 1'000'000'000 / 60;
    auto started = boundary;
    std::uint64_t result_time = 0, snapshot_time = boundary, clock_tick = 0;
    std::uint64_t previous_revision = 0;
    bool running = true, focused = true, overlay = false, terminal = false,
         quit_application = false, ready_requested = false;
    auto release = [&]() {
        input.release_all(std::max(SDL_GetTicksNS(), boundary + 1));
        for (auto& [id, pad] : pads) {
            (void)id;
            for (auto& axis : pad.axes)
                axis.reset();
        }
    };
    auto action = [&](int code, bool pad) -> std::optional<in::Action> {
        const auto& bindings = pad ? settings.buttons : settings.keys;
        for (std::size_t i = 0; i < bindings.size(); ++i)
            if (bindings[i] == code)
                return actions[i];
        return {};
    };
    auto transition = [&](std::uint64_t when,
                          in::Device device,
                          std::uint32_t code,
                          in::Action logical,
                          bool down) {
        input.push({std::max(when, boundary + 1), device, code, logical, down});
    };
    auto draw_board = [&](const modern::State& board,
                          float bx,
                          float by,
                          float cell,
                          bool local = false) {
        rect(renderer, bx - 3, by - 3, cell * 10 + 6, cell * 20 + 6, {47, 67, 89, 255});
        rect(renderer, bx, by, cell * 10, cell * 20, {12, 22, 35, 255});
        const auto speed = modern::gravity_interval(board);
        for (int y = 0; y < 20; ++y)
            for (int x = 0; x < 10; ++x) {
                rect(renderer, bx + x * cell, by + y * cell, cell - 1, cell - 1, {17, 29, 44, 255});
                auto value = board.board[static_cast<std::size_t>((y + 20) * 10 + x)];
                if (value)
                    block(renderer,
                          bx + x * cell,
                          by + y * cell,
                          client::classic_palette_piece(
                              static_cast<modern::Piece>(value <= 7 ? value - 1 : 0)),
                          speed,
                          cell);
            }
        if (local && board.preset == modern::Preset::modern && !board.game_over) {
            color(renderer, {94, 123, 149, 255});
            for (const auto c : modern::cells(modern::ghost(board)))
                if (c.y >= 0 && c.y < 20) {
                    SDL_FRect outline{bx + c.x * cell + 2, by + c.y * cell + 2, cell - 4, cell - 4};
                    SDL_RenderRect(renderer, &outline);
                }
        }
        if (!board.game_over)
            for (auto c : modern::cells(board.active))
                if (c.y >= 0 && c.y < 20)
                    block(renderer,
                          bx + c.x * cell,
                          by + c.y * cell,
                          client::classic_palette_piece(board.active.piece),
                          speed,
                          cell);
    };
    while (running) {
        connection.poll();
        const auto now = SDL_GetTicksNS();
        const auto& view = connection.view();
        if (view.snapshot_revision != previous_revision && view.has_snapshot) {
            if (prediction.apply(view)) {
                previous_revision = view.snapshot_revision;
                snapshot_time = now;
                clock_tick = view.server_tick;
            }
        }
        if (bot && view.phase == net::Phase::waiting && !view.ready && !ready_requested) {
            connection.set_ready(true);
            ready_requested = true;
        }
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT) {
                running = false;
                quit_application = true;
            } else if (event.type == SDL_EVENT_WINDOW_FOCUS_LOST) {
                focused = false;
                release();
            } else if (event.type == SDL_EVENT_WINDOW_FOCUS_GAINED)
                focused = true;
            else if (event.type == SDL_EVENT_GAMEPAD_ADDED)
                open_pad(event.gdevice.which);
            else if (event.type == SDL_EVENT_GAMEPAD_REMOVED) {
                input.release_device(std::max(event.gdevice.timestamp, boundary + 1),
                                     event.gdevice.which);
                if (auto it = pads.find(event.gdevice.which); it != pads.end()) {
                    SDL_CloseGamepad(it->second.handle);
                    pads.erase(it);
                }
            } else if (event.type == SDL_EVENT_KEYBOARD_REMOVED) {
                input.release_device(std::max(event.kdevice.timestamp, boundary + 1),
                                     keyboard | event.kdevice.which);
            } else if (event.type == SDL_EVENT_KEY_DOWN || event.type == SDL_EVENT_KEY_UP ||
                       event.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN ||
                       event.type == SDL_EVENT_GAMEPAD_BUTTON_UP) {
                const bool pad = event.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN ||
                                 event.type == SDL_EVENT_GAMEPAD_BUTTON_UP;
                const bool down = pad ? event.gbutton.down : event.key.down;
                const int code = pad ? event.gbutton.button : event.key.scancode;
                if ((!pad && event.key.repeat) || (!focused && !bot))
                    continue;
                const auto logical = action(code, pad);
                if (down) {
                    if ((!pad && code == SDL_SCANCODE_F10) ||
                        (pad && code == SDL_GAMEPAD_BUTTON_BACK)) {
                        running = false;
                        release();
                        continue;
                    }
                    if (!pad && code == SDL_SCANCODE_F9) {
                        release();
                        connection.reconnect();
                        continue;
                    }
                    if ((!pad && code == SDL_SCANCODE_RETURN) ||
                        (pad && code == SDL_GAMEPAD_BUTTON_SOUTH &&
                         view.phase == net::Phase::waiting)) {
                        if (view.phase == net::Phase::waiting)
                            connection.set_ready(!view.ready);
                    }
                    if (logical == in::Action::pause) {
                        overlay = !overlay;
                        release();
                        continue;
                    }
                }
                if (logical && *logical != in::Action::pause && !overlay)
                    transition(pad ? event.gbutton.timestamp : event.key.timestamp,
                               pad ? event.gbutton.which : keyboard | event.key.which,
                               static_cast<std::uint32_t>(code),
                               *logical,
                               down);
            } else if (event.type == SDL_EVENT_GAMEPAD_AXIS_MOTION && settings.sticks && focused &&
                       !overlay && event.gaxis.axis <= SDL_GAMEPAD_AXIS_LEFTY) {
                if (auto it = pads.find(event.gaxis.which); it != pads.end()) {
                    const int direction = it->second.axes[event.gaxis.axis].update(
                        static_cast<float>(event.gaxis.value) / 32768.F);
                    if (event.gaxis.axis == SDL_GAMEPAD_AXIS_LEFTX) {
                        transition(event.gaxis.timestamp,
                                   event.gaxis.which,
                                   100,
                                   in::Action::left,
                                   direction == -1);
                        transition(event.gaxis.timestamp,
                                   event.gaxis.which,
                                   101,
                                   in::Action::right,
                                   direction == 1);
                    } else
                        transition(event.gaxis.timestamp,
                                   event.gaxis.which,
                                   102,
                                   in::Action::down,
                                   direction == 1);
                }
            }
        }
        audio.set_playing(view.phase == net::Phase::playing && !overlay && (focused || bot));
        int catchup = 0;
        while (next_tick <= now && catchup++ < 8) {
            boundary = next_tick;
            next_tick += 1'000'000'000 / 60;
            const auto sampled = input.sample(boundary);
            if (view.phase != net::Phase::playing || !prediction.initialized())
                continue;
            auto mask = overlay || (!focused && !bot) ? 0U : client::modern_input(sampled, preset);
            if (bot) {
                const auto tick = prediction.board().tick;
                mask = modern::down;
                if (preset == modern::Preset::modern && tick % 8 == 0)
                    mask |= modern::hard_drop;
            }
            // Snapshot clock plus elapsed monotonic time. Lead is sampled as stable
            // neutral frames before the live input; no tick is rewritten on resend.
            const auto estimated = clock_tick + (now - snapshot_time) / (1'000'000'000 / 60);
            // A received snapshot is about half an RTT old, and the uplink
            // needs the other half. Reserve both halves plus three ticks.
            const auto network_lead = std::min<std::uint64_t>(
                90, 3 + (std::uint64_t(view.round_trip_ms) * 60 + 999) / 1000);
            const auto target = estimated + network_lead;
            modern::Events events;
            for (int lead = 0; prediction.board().tick + 1 < target && lead < 7; ++lead)
                if (!prediction.sample(0, events))
                    break;
            const bool had_hold = prediction.board().has_hold;
            if (prediction.board().tick <= target && prediction.sample(mask, events)) {
                if (events.shifted)
                    audio.play(ClientAudio::Cue::shift);
                if (events.soft_dropped)
                    audio.play(ClientAudio::Cue::soft_drop);
                if (events.rotated)
                    audio.play(ClientAudio::Cue::rotate);
                if (events.hard_dropped)
                    audio.play(ClientAudio::Cue::hard_drop);
                if (auto cue = client::hold_cue(events.held, had_hold))
                    audio.play(*cue);
                // Clear/attack/result effects require authoritative external events;
                // speculative clears intentionally make no competitive outcome cue.
            }
            connection.submit(prediction.rolling());
        }
        if (next_tick + 8 * (1'000'000'000 / 60) < now)
            next_tick = now + 1'000'000'000 / 60;
        if (view.phase == net::Phase::results && view.has_snapshot &&
            view.authoritative.result != stackpulse::match::Result::playing) {
            terminal = true;
            if (!result_time)
                result_time = now;
            if (bot && now - result_time > 1'000'000'000)
                running = false;
        }
        if (bot && ((exit_ticks && view.server_tick >= exit_ticks && !terminal) ||
                    now - started > 90'000'000'000ULL))
            running = false;
        if (bot && (view.phase == net::Phase::error || view.phase == net::Phase::disconnected) &&
            now - started > 5'000'000'000ULL)
            running = false;
        color(renderer, {9, 15, 27, 255});
        SDL_RenderClear(renderer);
        text(renderer, 20, 14, "STACKPULSE / AUTHORITATIVE DUEL", mint);
        text(renderer,
             20,
             32,
             std::string(preset == modern::Preset::fusion ? "FUSION" : "MODERN") + " / " + host +
                 ":" + number(port),
             muted);
        const std::array<const char*, 8> phases{"CONNECTING",
                                                "WAITING",
                                                "COUNTDOWN",
                                                "PLAYING",
                                                "RESULTS",
                                                "DISCONNECTED",
                                                "ERROR",
                                                "?"};
        text(renderer, 360, 14, phases[static_cast<std::size_t>(view.phase)], mint);
        text(renderer,
             360,
             32,
             "PLAYER " + number(view.player) + " / SERVER " + number(view.server_tick),
             muted);
        if (prediction.initialized()) {
            const auto& board = prediction.board();
            draw_board(board, 150, 61, 14, true);
            text(renderer, 324, 64, "NEXT", mint);
            for (std::size_t i = 0; i < board.next.size(); ++i) {
                modern::Active next{
                    board.next[i], modern::spawn_rotation(board.next[i], board.preset), 0, 0};
                for (auto c : modern::cells(next))
                    block(renderer,
                          324 + c.x * 10.F,
                          83 + static_cast<float>(i) * 39.F + c.y * 10.F,
                          client::classic_palette_piece(next.piece),
                          modern::gravity_interval(board),
                          10);
            }
            text(renderer, 16, 177, board.hold_used ? "HOLD / USED" : "HOLD", muted);
            if (board.has_hold)
                for (auto c : modern::cells({board.held_piece,
                                             modern::spawn_rotation(board.held_piece, board.preset),
                                             0,
                                             0}))
                    block(renderer,
                          98 + c.x * 8.F,
                          177 + c.y * 8.F,
                          client::classic_palette_piece(board.held_piece),
                          modern::gravity_interval(board),
                          8);
        } else
            text(renderer, 150, 100, "WAITING FOR SNAPSHOT", muted);
        if (view.has_snapshot)
            for (const auto& opponent : view.authoritative.players)
                if (opponent.id != view.player) {
                    draw_board(opponent.board, 411, 87, 10);
                    text(renderer, 408, 65, "OPPONENT / AUTHORITATIVE", mint);
                }
        text(renderer, 16, 68, "LOCAL PREDICTED", mint);
        if (prediction.initialized()) {
            text(renderer, 16, 96, "SCORE " + number(prediction.board().score));
            text(renderer, 16, 115, "LINES " + number(prediction.board().lines));
            unsigned incoming = 0;
            if (view.has_snapshot)
                for (const auto& player : view.authoritative.players)
                    if (player.id == view.player)
                        for (const auto& packet : player.board.incoming)
                            incoming += packet.rows;
            text(renderer, 16, 143, "INCOMING " + number(incoming), mint);
            for (unsigned row = 0; row < std::min(incoming, 20U); ++row)
                rect(renderer, 306, 330.F - row * 12.F, 7, 10, {248, 130, 88, 255});
            text(renderer, 16, 164, "RESYNCS " + number(prediction.hard_resets()), muted);
        }
        if (view.phase == net::Phase::waiting) {
            text(renderer, 16, 203, view.ready ? "YOU: READY" : "ENTER / SOUTH: READY", mint);
            text(renderer,
                 16,
                 225,
                 view.opponent_ready ? "OPPONENT: READY" : "OPPONENT: WAITING",
                 muted);
        } else if (view.phase == net::Phase::countdown)
            text(renderer, 16, 203, "START IN " + number((view.countdown + 59) / 60), mint);
        else if (terminal) {
            std::string result = "DRAW";
            if (view.authoritative.result == stackpulse::match::Result::winner)
                for (const auto& player : view.authoritative.players)
                    if (!player.placement)
                        result = player.id == view.player ? "YOU WIN" : "OPPONENT WINS";
            text(renderer, 16, 203, result, mint);
            text(renderer, 16, 225, "SERVER CONFIRMED", muted);
        }
        if (overlay) {
            rect(renderer, 328, 292, 291, 45, {26, 61, 65, 255});
            text(renderer, 337, 302, "INPUT RELEASED / PLAY CONTINUES", mint);
            text(renderer, 337, 319, "ESC / START RESUMES INPUT", muted);
        }
        text(renderer, 16, 268, "F9 RECONNECT", muted);
        text(renderer, 16, 287, "F10 LEAVE", muted);
        text(renderer, 360, 322, "ONLINE REPLAY OFF", muted);
        rect(renderer, 0, 345, 640, 15, {19, 33, 49, 255});
        text(renderer, 8, 349, view.detail.substr(0, 76), mint);
        if (!running && !capture_path.empty()) {
            SDL_Surface* capture = SDL_RenderReadPixels(renderer, nullptr);
            if (!capture || !SDL_SaveBMP(capture, capture_path.string().c_str()))
                std::fprintf(stderr, "Online capture: %s\n", SDL_GetError());
            SDL_DestroySurface(capture);
        }
        SDL_RenderPresent(renderer);
        SDL_Delay(1);
    }
    const auto& final = connection.view();
    if (final.has_snapshot)
        std::printf(
            "Online finished: player=%u tick=%llu result=%u authoritative_hash=%016llx "
            "resyncs=%llu\n",
            static_cast<unsigned>(final.player),
            static_cast<unsigned long long>(final.authoritative.tick),
            static_cast<unsigned>(final.authoritative.result),
            static_cast<unsigned long long>(stackpulse::match::state_hash(final.authoritative)),
            static_cast<unsigned long long>(prediction.hard_resets()));
    else
        std::fprintf(
            stderr, "Online finished without authoritative snapshot: %s\n", final.detail.c_str());
    if (terminal && final.has_snapshot)
        std::printf(
            "AUTHORITATIVE_RESULT tick=%llu result=%s hash=%016llx\n",
            static_cast<unsigned long long>(final.authoritative.tick),
            final.authoritative.result == stackpulse::match::Result::draw ? "draw" : "winner",
            static_cast<unsigned long long>(stackpulse::match::state_hash(final.authoritative)));
    connection.disconnect();
    for (auto& [id, pad] : pads) {
        (void)id;
        SDL_CloseGamepad(pad.handle);
    }
    audio.set_playing(false);
    return quit_application ? 2 : bot && !terminal ? 1 : 0;
}
#endif
} // namespace
int main(int argc, char** argv) {
    int smoke_frames = 0;
    bool smoke_play = false, smoke_settings = false, force_sticks = false, muted_audio = false;
    std::filesystem::path data_directory, verify_path, capture_path;
    bool smoke_spin_debug = false;
    int mode = 0; // Classic, Fusion, Modern
    unsigned fixture_rows = 1;
    bool garbage_fixture = false;
    std::string online_host;
    std::uint16_t online_port = 27877;
    std::uint64_t online_exit_ticks = 0;
    bool online_bot = false;
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg = argv[i];
        if (arg == "--connect" && i + 1 < argc)
            online_host = argv[++i];
        else if (arg == "--online-bot")
            online_bot = true;
        else if ((arg == "--port" || arg == "--online-exit-ticks") && i + 1 < argc) {
            std::uint64_t value = 0;
            const std::string_view number_arg = argv[++i];
            const auto parsed =
                std::from_chars(number_arg.data(), number_arg.data() + number_arg.size(), value);
            if (parsed.ec != std::errc{} || parsed.ptr != number_arg.data() + number_arg.size() ||
                !value || (arg == "--port" && value > 65535))
                return 2;
            if (arg == "--port")
                online_port = static_cast<std::uint16_t>(value);
            else
                online_exit_ticks = value;
        } else if (arg == "--mode" && i + 1 < argc) {
            const std::string_view value = argv[++i];
            if (value == "classic")
                mode = 0;
            else if (value == "fusion")
                mode = 1;
            else if (value == "modern")
                mode = 2;
            else
                return 2;
        } else if (arg == "--verify" && i + 1 < argc)
            verify_path = argv[++i];
        else if (arg == "--capture" && i + 1 < argc)
            capture_path = argv[++i];
        else if (arg == "--smoke-settings")
            smoke_settings = true;
        else if (arg == "--data-dir" && i + 1 < argc)
            data_directory = argv[++i];
        else if (arg == "--spin-debug")
            smoke_spin_debug = true;
        else if (arg == "--smoke-play")
            smoke_play = true;
        else if (arg == "--sticks")
            force_sticks = true;
        else if (arg == "--mute")
            muted_audio = true;
        else if (arg == "--smoke-frames" && i + 1 < argc) {
            const std::string_view value = argv[++i];
            const auto result =
                std::from_chars(value.data(), value.data() + value.size(), smoke_frames);
            if (result.ec != std::errc{} || result.ptr != value.data() + value.size() ||
                smoke_frames < 1)
                return 2;
        } else {
            std::fprintf(stderr,
                         "Usage: stackpulse_client [--sticks] [--mute] [--data-dir PATH] "
                         "[--mode classic|fusion|modern] [--smoke-frames N] [--smoke-play] "
                         "[--smoke-settings] [--spin-debug] [--capture FILE.bmp] "
                         "[--verify FILE] [--connect HOST --port N] "
                         "[--online-bot --online-exit-ticks N]\n");
            return 2;
        }
    }
    if (!online_host.empty() && mode == 0)
        mode = 1;
#ifndef STACKPULSE_NETWORK
    if (!online_host.empty() || online_bot || online_exit_ticks) {
        std::fprintf(stderr, "Networking disabled in this build\n");
        return 2;
    }
#endif
    if (smoke_spin_debug && mode == 0)
        mode = 1;
    if (!verify_path.empty()) {
        mr::Replay modern_value;
        modern::State modern_final;
        std::string modern_error;
        if (mr::load(verify_path, modern_value, modern_error) &&
            mr::verify(modern_value, modern_final, modern_error)) {
            std::printf("Modern/Fusion replay verified: frames=%zu hash=%llu\n",
                        modern_value.inputs.size(),
                        static_cast<unsigned long long>(modern::state_hash(modern_final)));
            return 0;
        }
        std::ifstream header(verify_path, std::ios::binary);
        std::array<char, 4> magic{};
        header.read(magic.data(), static_cast<std::streamsize>(magic.size()));
        if (magic == std::array<char, 4>{'S', 'P', 'M', 'R'}) {
            std::fprintf(stderr, "Replay rejected: %s\n", modern_error.c_str());
            return 1;
        }
        replay::Replay value;
        game::State final_state;
        std::string error;
        if (!replay::load(verify_path, value, error) ||
            !replay::verify(value, final_state, error)) {
            std::fprintf(stderr, "Replay rejected: %s\n", error.c_str());
            return 1;
        }
        std::printf("Replay verified: frames=%zu tick=%llu hash=%llu\n",
                    value.inputs.size(),
                    static_cast<unsigned long long>(final_state.tick),
                    static_cast<unsigned long long>(game::state_hash(final_state)));
        return 0;
    }
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) {
        std::fprintf(stderr, "%s\n", SDL_GetError());
        return 1;
    }
    SDL_Window* window = nullptr;
    SDL_Renderer* renderer = nullptr;
    if (!SDL_CreateWindowAndRenderer("Stackpulse / Classic / Fusion / Modern",
                                     1280,
                                     720,
                                     SDL_WINDOW_RESIZABLE,
                                     &window,
                                     &renderer)) {
        SDL_Quit();
        return 1;
    }
    SDL_SetWindowMinimumSize(window, 640, 360);
    if (!client::configure_presentation(renderer)) {
        std::fprintf(stderr, "Logical presentation: %s\n", SDL_GetError());
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
    SDL_SetRenderVSync(renderer, 1);
    ClientAudio audio;
    audio.initialize(muted_audio);
    std::filesystem::path directory = ".";
    if (char* pref = SDL_GetPrefPath("Stackpulse", "Classic-v1")) {
        directory = pref;
        SDL_free(pref);
    }
    if (!data_directory.empty())
        directory = data_directory;
    std::error_code directory_error;
    std::filesystem::create_directories(directory, directory_error);
    const auto settings_path = directory / "settings.txt", replay_path = directory / "last.spcr";
    Settings settings;
    settings.load(settings_path);
    if (force_sticks)
        settings.sticks = true;
#ifdef STACKPULSE_NETWORK
    if (!online_host.empty()) {
        const auto result = run_online(renderer,
                                       settings,
                                       audio,
                                       online_host,
                                       online_port,
                                       mode == 1 ? modern::Preset::fusion : modern::Preset::modern,
                                       online_bot,
                                       online_exit_ticks,
                                       capture_path);
        audio.shutdown();
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return result == 2 ? 0 : result;
    }
#endif
    std::map<SDL_JoystickID, Pad> pads;
    auto open_pad = [&](SDL_JoystickID id) {
        if (!pads.contains(id))
            if (auto* handle = SDL_OpenGamepad(id))
                pads.emplace(id, Pad{handle, {}});
    };
    int count = 0;
    auto* ids = SDL_GetGamepads(&count);
    for (int i = 0; i < count; ++i)
        open_pad(ids[i]);
    SDL_free(ids);
    in::Adapter adapter;
    in::Timestamp boundary = 0, epoch = SDL_GetTicksNS(), ticks = 0;
    auto tick_boundary = [&](in::Timestamp index) {
        return epoch + (index / 60) * 1'000'000'000 + (index % 60) * 1'000'000'000 / 60;
    };
    std::uint64_t late = 0, overload = 0;
    int frames = 0, selection = 0, settings_row = 0, capture = -1;
    bool running = true, focused = true, practice = false, playback = false, step_once = false;
    bool spin_debug = false;
    std::uint8_t spin_debug_rotation = 0;
    Screen screen = Screen::home, settings_return = Screen::home;
    game::State state = game::initial_state(1);
    modern::State modern_state = modern::initial_state(1);
    mr::Replay modern_recording{modern_state, {}}, modern_loaded;
    auto preset = [&]() { return mode == 1 ? modern::Preset::fusion : modern::Preset::modern; };
    auto mode_name = [&]() -> std::string {
        return mode == 0 ? "CLASSIC" : mode == 1 ? "FUSION" : "MODERN";
    };
    auto sync_display = [&]() {
        if (!mode)
            return;
        state.tick = modern_state.tick;
        state.score = modern_state.score;
        state.lines = static_cast<std::uint32_t>(modern_state.lines);
        state.level = modern::level(modern_state);
        state.game_over = modern_state.game_over;
    };
    auto current_hash = [&]() {
        return mode ? modern::state_hash(modern_state) : game::state_hash(state);
    };
    std::uint32_t seed = 1;
    replay::Replay recording{state, {}}, loaded;
    std::size_t playback_index = 0;
    std::string notice = "READY / OFFLINE";
    std::deque<std::string> timeline;
    auto release = [&](in::Timestamp when) {
        adapter.release_all(std::max(when, boundary + 1));
        for (auto& [id, pad] : pads) {
            (void)id;
            for (auto& axis : pad.axes)
                axis.reset();
        }
    };
    // Encoding validates every recorded tick. Run that work on an owned snapshot;
    // never replay a long run on the event/render thread. Only one disk writer runs.
    client::SaveWorker saves;
    auto poll_save = [&](bool wait) {
        const auto result = saves.poll(wait);
        if (!result)
            return;
        if (result->empty()) {
            notice = "REPLAY SAVED";
        } else {
            notice = "SAVE FAILED: " + *result;
            std::fprintf(stderr, "Replay save: %s\n", result->c_str());
        }
    };
    auto save_recording = [&]() {
        poll_save(false);
        if (saves.pending()) {
            if (saves.dirty())
                notice = "PREVIOUS REPLAY SAVE IN PROGRESS / RETRY";
            return !saves.dirty();
        }
        if (saves.retry_needed()) {
            saves.retry();
            notice = "RETRYING PREVIOUS REPLAY SAVE";
            return false;
        }
        if (!saves.dirty() || playback || smoke_frames > 0)
            return true;
        if (mode) {
            saves.submit([snapshot = modern_recording, path = directory / "last.spmr"]() {
                std::string error;
                mr::save(path, snapshot, error);
                return error;
            });
        } else {
            saves.submit([snapshot = recording, path = replay_path]() {
                std::string error;
                replay::save(path, snapshot, error);
                return error;
            });
        }
        notice = "SAVING REPLAY";
        return true;
    };
    auto start = [&](bool lab, bool fixture) {
        if (!save_recording())
            return;
        spin_debug = false;
        practice = lab;
        playback = false;
        step_once = false;
        state =
            fixture ? game::clear_fixture(seed) : game::initial_state(seed, settings.start_level);
        recording = {state, {}};
        if (mode) {
            modern_state = fixture && !garbage_fixture
                               ? modern::spin_fixture(seed, preset(), fixture_rows)
                               : modern::initial_state(seed, preset(), settings.start_level);
            if (garbage_fixture) {
                modern_state = modern::initial_state(seed, preset(), settings.start_level);
                modern::enqueue(modern_state, {1, 0, 60, 1, 4, 4});
            }
            modern_recording = {modern_state, {}};
            sync_display();
        }
        saves.set_dirty(false);
        screen = Screen::play;
        timeline.clear();
        release(SDL_GetTicksNS());
        notice = mode && garbage_fixture ? "GARBAGE DEMO / 4 ROWS ELIGIBLE AT TICK 60"
                 : fixture
                     ? (mode ? "SPIN FIXTURE / CW THEN LOCK / " + number(fixture_rows) + " LINES"
                             : "FOUR-LINE FIXTURE / HOLD DOWN")
                     : "NEW RUN / SEED " + number(seed);
    };
    auto push = [&](in::Transition value, bool repeat = false) {
        if (repeat)
            return;
        value.source_timestamp = value.timestamp;
        if (value.timestamp <= boundary) {
            value.timestamp = boundary + 1;
            ++late;
        }
        adapter.push(value);
    };
    auto map_action = [&](int code, bool pad) -> std::optional<in::Action> {
        const auto& bindings = pad ? settings.buttons : settings.keys;
        for (std::size_t i = 0; i < bindings.size(); ++i)
            if (bindings[i] == code)
                return actions[i];
        return {};
    };
    auto save_settings = [&]() {
        notice = (smoke_frames > 0 || settings.save(settings_path)) ? "SETTINGS SAVED"
                                                                    : "SETTINGS SAVE FAILED";
    };
    auto adjust_level = [&](int code, bool pad) {
        if (screen != Screen::settings || settings_row != 17)
            return;
        const auto next =
            client::adjusted_start_level(settings.start_level, client::level_direction(code, pad));
        if (next != settings.start_level) {
            settings.start_level = next;
            save_settings();
        }
    };
    auto remap = [&](int code, bool pad) {
        const bool wants_pad = capture >= 8;
        if (pad != wants_pad)
            return;
        auto& values = pad ? settings.buttons : settings.keys;
        const auto slot = static_cast<std::size_t>(capture % 8);
        if (!pad &&
            (code == SDL_SCANCODE_UP || code == SDL_SCANCODE_RETURN || code == SDL_SCANCODE_F1 ||
             code == SDL_SCANCODE_F2 || code == SDL_SCANCODE_F3 || code == SDL_SCANCODE_F4 ||
             code == SDL_SCANCODE_F5 || code == SDL_SCANCODE_F6 || code == SDL_SCANCODE_F7 ||
             code == SDL_SCANCODE_F8 || code == SDL_SCANCODE_F9 || code == SDL_SCANCODE_F10 ||
             code == SDL_SCANCODE_F11 || code == SDL_SCANCODE_F12)) {
            notice = "RESERVED MENU / PRACTICE KEY";
            return;
        }
        if (pad && slot == 5 &&
            (code == SDL_GAMEPAD_BUTTON_SOUTH || code == SDL_GAMEPAD_BUTTON_DPAD_UP ||
             code == SDL_GAMEPAD_BUTTON_DPAD_DOWN || code == SDL_GAMEPAD_BUTTON_BACK)) {
            notice = "RESERVED MENU KEY FOR PAUSE";
            return;
        }
        if (!pad && slot == 5 &&
            (code == SDL_SCANCODE_DOWN || code == SDL_SCANCODE_W || code == SDL_SCANCODE_S)) {
            notice = "RESERVED MENU KEY FOR PAUSE";
            return;
        }
        for (std::size_t i = 0; i < values.size(); ++i)
            if (i != slot && values[i] == code) {
                notice = "CONFLICT: ALREADY BOUND TO " + std::string(names[i]);
                return;
            }
        values[slot] = code;
        capture = -1;
        save_settings();
    };
    auto navigate = [&](int direction) {
        const int size = screen == Screen::home ? 7 : screen == Screen::pause ? 7 : 19;
        int& row = screen == Screen::settings ? settings_row : selection;
        row = (row + direction + size) % size;
    };
    auto confirm = [&]() {
        if (screen == Screen::settings) {
            if (settings_row < 16) {
                capture = settings_row;
                notice = "PRESS A NEW BINDING / ESC CANCELS";
            } else if (settings_row == 16) {
                settings.sticks = !settings.sticks;
                save_settings();
            } else if (settings_row == 17) {
                settings.start_level = (settings.start_level + 1) % 30;
                save_settings();
            } else
                screen = settings_return;
        } else if (screen == Screen::home) {
            if (selection == 0) {
                if (save_recording()) {
                    mode = (mode + 1) % 3;
                    notice = "SELECTED " + mode_name();
                }
            } else if (selection == 1 || selection == 2) {
                garbage_fixture = false;
                start(selection == 2, false);
            } else if (selection == 3) {
                std::string error;
                if (!save_recording())
                    return;
                if (!saves.ready_to_load()) {
                    notice = "REPLAY SAVE IN PROGRESS / RETRY PLAY REPLAY";
                    return;
                }
                bool ok = mode ? mr::load(directory / "last.spmr", modern_loaded, error)
                               : replay::load(replay_path, loaded, error);
                if (ok) {
                    if (mode) {
                        modern_state = modern_loaded.initial;
                        mode = modern_state.preset == modern::Preset::fusion ? 1 : 2;
                        sync_display();
                    } else
                        state = loaded.initial;
                    practice = false;
                    playback = true;
                    playback_index = 0;
                    screen = Screen::play;
                    release(SDL_GetTicksNS());
                    notice = "VERIFIED REPLAY";
                } else
                    notice = "REPLAY: " + error;
            } else if (selection == 4) {
                settings_return = screen;
                screen = Screen::settings;
            } else if (selection == 5) {
#ifdef STACKPULSE_NETWORK
                if (!save_recording())
                    return;
                release(SDL_GetTicksNS());
                const auto online_result =
                    run_online(renderer,
                               settings,
                               audio,
                               "127.0.0.1",
                               online_port,
                               mode == 2 ? modern::Preset::modern : modern::Preset::fusion,
                               false,
                               0);
                if (online_result == 2)
                    running = false;
                // The online session consumed hotplug events; refresh offline handles.
                for (auto& [id, pad] : pads) {
                    (void)id;
                    SDL_CloseGamepad(pad.handle);
                }
                pads.clear();
                int online_pad_count = 0;
                auto* online_pad_ids = SDL_GetGamepads(&online_pad_count);
                for (int i = 0; i < online_pad_count; ++i)
                    open_pad(online_pad_ids[i]);
                SDL_free(online_pad_ids);
                release(SDL_GetTicksNS());
                notice = "RETURNED FROM MULTIPLAYER";
#else
                notice = "NETWORKING DISABLED IN THIS BUILD";
#endif
            } else
                running = false;
        } else if (screen == Screen::pause) {
            if (selection == 0) {
                screen = Screen::play;
                release(SDL_GetTicksNS());
            } else if (selection == 1) {
                if (playback) {
                    state = loaded.initial;
                    if (mode) {
                        modern_state = modern_loaded.initial;
                        sync_display();
                    }
                    playback_index = 0;
                    screen = Screen::play;
                    release(SDL_GetTicksNS());
                    notice = "REPLAY RESTARTED";
                } else
                    start(practice, false);
            } else if (selection == 2) {
                if (save_recording()) {
                    ++seed;
                    start(practice, false);
                }
            } else if (selection == 3) {
                if (practice && !playback) {
                    screen = Screen::play;
                    step_once = true;
                } else
                    notice = "FRAME STEP IS A PRACTICE TOOL";
            } else if (selection == 4) {
                if (practice && !playback) {
                    garbage_fixture = false;
                    start(true, true);
                } else
                    notice = "FIXTURE IS A PRACTICE TOOL";
            } else if (selection == 5) {
                settings_return = screen;
                screen = Screen::settings;
            } else {
                if (!save_recording())
                    return;
                screen = Screen::home;
                selection = 0;
                release(SDL_GetTicksNS());
            }
        }
    };
    auto pause = [&]() {
        if (screen == Screen::play) {
            screen = Screen::pause;
            selection = 0;
            release(SDL_GetTicksNS());
            save_recording();
        } else if (screen == Screen::pause) {
            screen = Screen::play;
            release(SDL_GetTicksNS());
        } else if (screen == Screen::settings) {
            capture = -1;
            screen = settings_return;
        }
    };
    if (smoke_play)
        start(true, true);
    if (smoke_spin_debug) {
        start(true, true);
        spin_debug = true;
        spin_debug_rotation = modern_state.active.rotation;
        notice = "SPIN INSPECTOR / SIMULATION FROZEN";
    }
    if (smoke_settings)
        screen = Screen::settings;
    std::printf("Stackpulse independent provisional presets; prefs=%s\n",
                directory.string().c_str());
    auto previous_frame_time = SDL_GetTicksNS();
    bool previous_frame_playing = screen == Screen::play && !spin_debug;
    while (running) {
        const auto frame_time = SDL_GetTicksNS();
        // Paused/menu time is not simulation debt when play resumes.
        if (!previous_frame_playing)
            epoch += frame_time - previous_frame_time;
        previous_frame_time = frame_time;
        poll_save(false);
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            switch (event.type) {
            case SDL_EVENT_QUIT:
                running = false;
                break;
            case SDL_EVENT_WINDOW_FOCUS_LOST:
                focused = false;
                release(event.common.timestamp);
                if (screen == Screen::play) {
                    screen = Screen::pause;
                    selection = 0;
                    notice = "FOCUS LOST / INPUT RELEASED";
                }
                break;
            case SDL_EVENT_WINDOW_FOCUS_GAINED:
                focused = true;
                break;
            case SDL_EVENT_GAMEPAD_ADDED:
                open_pad(event.gdevice.which);
                notice = "CONTROLLER CONNECTED";
                break;
            case SDL_EVENT_GAMEPAD_REMOVED:
                adapter.release_device(std::max(event.gdevice.timestamp, boundary + 1),
                                       event.gdevice.which);
                if (auto found = pads.find(event.gdevice.which); found != pads.end()) {
                    SDL_CloseGamepad(found->second.handle);
                    pads.erase(found);
                }
                if (screen == Screen::play) {
                    screen = Screen::pause;
                    selection = 0;
                }
                notice = "CONTROLLER REMOVED / PAUSED";
                break;
            case SDL_EVENT_KEYBOARD_REMOVED:
                adapter.release_device(std::max(event.kdevice.timestamp, boundary + 1),
                                       keyboard | event.kdevice.which);
                break;
            case SDL_EVENT_KEY_DOWN:
            case SDL_EVENT_KEY_UP: {
                if (!focused || event.key.repeat)
                    break;
                const auto code = event.key.scancode;
                if (event.key.down && capture >= 0) {
                    if (code == SDL_SCANCODE_ESCAPE) {
                        capture = -1;
                        notice = "REMAP CANCELLED";
                    } else
                        remap(code, false);
                    break;
                }
                if (event.key.down && practice && mode && !playback &&
                    (screen == Screen::play || screen == Screen::pause) &&
                    code == SDL_SCANCODE_F11) {
                    spin_debug = !spin_debug;
                    if (spin_debug)
                        spin_debug_rotation = modern_state.active.rotation;
                    release(event.key.timestamp);
                    notice =
                        spin_debug ? "SPIN INSPECTOR / SIMULATION FROZEN" : "SPIN INSPECTOR CLOSED";
                    break;
                }
                if (spin_debug) {
                    if (event.key.down && code == SDL_SCANCODE_F12) {
                        spin_debug_rotation = (spin_debug_rotation + 1) % 4;
                    } else if (event.key.down && code == SDL_SCANCODE_ESCAPE) {
                        spin_debug = false;
                        release(event.key.timestamp);
                        notice = "SPIN INSPECTOR CLOSED";
                    }
                    break;
                }
                if (event.key.down) {
                    if (code == SDL_SCANCODE_ESCAPE ||
                        (map_action(code, false) == in::Action::pause &&
                         (screen == Screen::play ||
                          (screen == Screen::pause && code != SDL_SCANCODE_UP &&
                           code != SDL_SCANCODE_DOWN && code != SDL_SCANCODE_W &&
                           code != SDL_SCANCODE_S && code != SDL_SCANCODE_RETURN)))) {
                        pause();
                        break;
                    }
                    if (screen != Screen::play) {
                        if (code == SDL_SCANCODE_UP || code == SDL_SCANCODE_W)
                            navigate(-1);
                        else if (code == SDL_SCANCODE_DOWN || code == SDL_SCANCODE_S)
                            navigate(1);
                        else if (code == SDL_SCANCODE_LEFT || code == SDL_SCANCODE_RIGHT)
                            adjust_level(code, false);
                        else if (code == SDL_SCANCODE_RETURN)
                            confirm();
                        else if (practice && screen == Screen::pause && code == SDL_SCANCODE_F4) {
                            screen = Screen::play;
                            step_once = true;
                        }
                        break;
                    }
                    if (code == SDL_SCANCODE_F1) {
                        pause();
                        break;
                    }
                    if (code == SDL_SCANCODE_F2 && !playback) {
                        start(practice, false);
                        break;
                    }
                    if (code == SDL_SCANCODE_F3 && !playback) {
                        if (save_recording()) {
                            ++seed;
                            start(practice, false);
                        }
                        break;
                    }
                    if (code == SDL_SCANCODE_F4 && practice && !playback) {
                        step_once = true;
                        break;
                    }
                    if (code == SDL_SCANCODE_F5 && practice && !playback) {
                        garbage_fixture = false;
                        start(true, true);
                        break;
                    }
                    if (practice && !playback && mode && code >= SDL_SCANCODE_F7 &&
                        code <= SDL_SCANCODE_F10) {
                        fixture_rows = static_cast<unsigned>(code - SDL_SCANCODE_F7 + 1);
                        garbage_fixture = code == SDL_SCANCODE_F10;
                        start(true, true);
                        break;
                    }
                    if (code == SDL_SCANCODE_F6) {
                        save_recording();
                        break;
                    }
                }
                if (screen == Screen::play && !playback)
                    if (auto action = map_action(code, false))
                        push({event.key.timestamp,
                              keyboard | event.key.which,
                              static_cast<std::uint32_t>(code),
                              *action,
                              event.key.down});
                break;
            }
            case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
            case SDL_EVENT_GAMEPAD_BUTTON_UP: {
                if (!focused || !pads.contains(event.gbutton.which))
                    break;
                const int code = event.gbutton.button;
                if (event.gbutton.down && capture >= 0) {
                    if (code == SDL_GAMEPAD_BUTTON_BACK) {
                        capture = -1;
                        notice = "REMAP CANCELLED";
                    } else
                        remap(code, true);
                    break;
                }
                if (spin_debug)
                    break;
                if (event.gbutton.down) {
                    if (code == SDL_GAMEPAD_BUTTON_BACK ||
                        (map_action(code, true) == in::Action::pause &&
                         (screen == Screen::play ||
                          (screen == Screen::pause && code != SDL_GAMEPAD_BUTTON_SOUTH &&
                           code != SDL_GAMEPAD_BUTTON_DPAD_UP &&
                           code != SDL_GAMEPAD_BUTTON_DPAD_DOWN)))) {
                        pause();
                        break;
                    }
                    if (screen != Screen::play) {
                        if (code == SDL_GAMEPAD_BUTTON_DPAD_UP)
                            navigate(-1);
                        else if (code == SDL_GAMEPAD_BUTTON_DPAD_DOWN)
                            navigate(1);
                        else if (code == SDL_GAMEPAD_BUTTON_DPAD_LEFT ||
                                 code == SDL_GAMEPAD_BUTTON_DPAD_RIGHT)
                            adjust_level(code, true);
                        else if (code == SDL_GAMEPAD_BUTTON_SOUTH)
                            confirm();
                        break;
                    }
                }
                if (screen == Screen::play && !playback)
                    if (auto action = map_action(code, true))
                        push({event.gbutton.timestamp,
                              event.gbutton.which,
                              static_cast<std::uint32_t>(code),
                              *action,
                              event.gbutton.down});
                break;
            }
            case SDL_EVENT_GAMEPAD_AXIS_MOTION:
                if (screen == Screen::play && !spin_debug && !playback && focused &&
                    settings.sticks && event.gaxis.axis <= SDL_GAMEPAD_AXIS_LEFTY)
                    if (auto found = pads.find(event.gaxis.which); found != pads.end()) {
                        const int direction = found->second.axes[event.gaxis.axis].update(
                            static_cast<float>(event.gaxis.value) / 32768.F);
                        if (event.gaxis.axis == SDL_GAMEPAD_AXIS_LEFTX) {
                            push({event.gaxis.timestamp,
                                  event.gaxis.which,
                                  100,
                                  in::Action::left,
                                  direction == -1});
                            push({event.gaxis.timestamp,
                                  event.gaxis.which,
                                  101,
                                  in::Action::right,
                                  direction == 1});
                        } else
                            push({event.gaxis.timestamp,
                                  event.gaxis.which,
                                  102,
                                  in::Action::down,
                                  direction == 1});
                    }
                break;
            default:
                break;
            }
        }
        audio.set_playing(screen == Screen::play && !spin_debug);
        const auto now = SDL_GetTicksNS();
        int catchup = 0;
        while (tick_boundary(ticks + 1) <= now && catchup < 8) {
            ++catchup;
            boundary = tick_boundary(++ticks);
            const auto sample = adapter.sample(boundary);
            for (const auto& transition : sample.transitions) {
                std::string label = number(state.tick) + " " + (transition.down ? "+" : "-");
                for (std::size_t i = 0; i < actions.size(); ++i)
                    if (transition.action == actions[i])
                        label += names[i];
                if (transition.synthetic)
                    label += " CLEAR";
                timeline.push_front(label);
                if (timeline.size() > 4)
                    timeline.pop_back();
            }
            if (screen == Screen::play && !spin_debug && (!state.game_over || playback)) {
                std::uint32_t mask = 0;
                for (std::size_t i = 0; i < 5; ++i)
                    if (sample.held[static_cast<std::size_t>(actions[i])])
                        mask |= 1U << i;
                if (mode)
                    mask = client::modern_input(sample, preset());
                if (smoke_play && !playback)
                    mask = mode && modern_state.tick == 0 ? modern::clockwise : game::down;
                if (playback) {
                    if (playback_index ==
                        (mode ? modern_loaded.inputs.size() : loaded.inputs.size())) {
                        screen = Screen::pause;
                        selection = 0;
                        notice = "REPLAY COMPLETE / HASH " + number(current_hash());
                        continue;
                    }
                    mask = mode ? modern_loaded.inputs[playback_index++]
                                : loaded.inputs[playback_index++];
                }
                if (!playback && (mode ? modern_recording.inputs.size()
                                       : recording.inputs.size()) >= replay::max_frames) {
                    screen = Screen::pause;
                    selection = 1;
                    if (save_recording())
                        notice = "RECORDING LIMIT / RUN PAUSED";
                    continue;
                }
                game::Events events;
                modern::Events modern_events;
                const bool before_has_hold = modern_state.has_hold;
                const bool stepped = mode ? modern::step(modern_state, mask, &modern_events)
                                          : game::step(state, mask, &events);
                if (mode) {
                    sync_display();
                    events.shifted = modern_events.shifted;
                    events.soft_dropped = modern_events.soft_dropped;
                    events.rotated = modern_events.rotated;
                    events.cleared = modern_events.cleared;
                }
                if (!stepped) {
                    screen = Screen::pause;
                    notice = "INVALID STATE / SIMULATION STOPPED";
                } else {
                    if (mode)
                        if (const auto cue = client::hold_cue(modern_events.held, before_has_hold))
                            audio.play(*cue);
                    if (events.shifted)
                        audio.play(ClientAudio::Cue::shift);
                    if (events.soft_dropped)
                        audio.play(ClientAudio::Cue::soft_drop);
                    if (events.rotated)
                        audio.play(ClientAudio::Cue::rotate);
                    if (mode && modern_events.hard_dropped)
                        audio.play(ClientAudio::Cue::hard_drop);
                    if (const auto cue =
                            client::clear_cue(events.cleared, mode && modern_events.spin))
                        audio.play(*cue);
                    if (!playback) {
                        if (mode)
                            modern_recording.inputs.push_back(mask);
                        else
                            recording.inputs.push_back(mask);
                        saves.set_dirty(true);
                    }
                    if (events.cleared)
                        notice = number(events.cleared) + " LINES CLEARED";
                    if (mode && (modern_events.locked || modern_events.inserted))
                        notice = std::string(modern_events.spin ? "SPIN " : "CLEAR ") +
                                 number(modern_events.cleared) + " ATK " +
                                 number(modern_events.attack) + " CANCEL " +
                                 number(modern_events.canceled) + " INSERT " +
                                 number(modern_events.inserted) +
                                 (modern_events.back_to_back ? " B2B" : "") +
                                 (modern_events.perfect_clear ? " PERFECT" : "");
                    if (state.game_over && !playback) {
                        notice = "GAME OVER / RESTART FROM PAUSE";
                        save_recording();
                        screen = Screen::pause;
                        selection = 1;
                    }
                }
                if (step_once) {
                    step_once = false;
                    screen = Screen::pause;
                    selection = 3;
                }
            }
        }
        previous_frame_playing = screen == Screen::play && !spin_debug;
        audio.set_playing(screen == Screen::play && !spin_debug);
        const bool behind = tick_boundary(ticks + 1) <= now;
        if (behind)
            ++overload;
        color(renderer, {9, 15, 27, 255});
        SDL_RenderClear(renderer);
        for (int line = 0; line < 12; ++line)
            rect(renderer, 0, static_cast<float>(line * 32), 640, 1, {13, 23, 38, 255});
        text(renderer, 20, 15, "STACKPULSE", mint);
        text(renderer, 20, 31, mode_name() + " / PROVISIONAL", muted);
        text(renderer, 368, 15, "PROVISIONAL RULES / 60 Hz", muted);
        text(renderer, 368, 31, "NO REFERENCE FIDELITY CLAIM", muted);
        rect(renderer, 18, 50, 604, 1, {48, 66, 88, 255});
        if (screen == Screen::home) {
            text(renderer, 36, 77, "BUILD. CLEAR. FIND YOUR RHYTHM.", ink);
            const std::array<std::string, 7> menu{"MODE: " + mode_name() + " / ENTER CYCLES",
                                                  "MARATHON",
                                                  "PRACTICE LAB",
                                                  "PLAY LAST REPLAY",
                                                  "SETTINGS / CONTROLS",
                                                  "MULTIPLAYER / LOCALHOST",
                                                  "QUIT"};
            for (int i = 0; i < 7; ++i) {
                if (selection == i)
                    rect(renderer, 28, 101.F + i * 26.F, 342, 25, {26, 61, 65, 255});
                text(renderer,
                     40,
                     109.F + i * 26.F,
                     std::string(selection == i ? "> " : "  ") + menu[i],
                     selection == i ? mint : ink);
            }
            text(renderer,
                 382,
                 119,
                 mode == 2   ? "FIVE NEXT / HOLD / GHOST"
                 : mode == 1 ? "FIVE NEXT / HOLD"
                             : "ONE NEXT PIECE",
                 mint);
            text(renderer,
                 382,
                 142,
                 mode == 2   ? "HARD DROP / SRS KICKS"
                 : mode == 1 ? "SRS / LOCAL SPIN LOCK"
                             : "NO HOLD / GHOST / KICKS");
            text(renderer, 382, 202, "PRACTICE TOOLS", mint);
            text(renderer, 382, 225, "SEEDS / FRAME STEP");
            text(renderer, 382, 244, mode ? "F7-9 SPINS / F10 GARBAGE" : "FOUR-LINE FIXTURE");
            text(renderer, 36, 294, "ARROWS OR W/S + ENTER / D-PAD + SOUTH", muted);
        } else if (screen == Screen::settings) {
            text(renderer, 30, 65, "CONTROLS / SELECT TO REMAP", mint);
            for (int i = 0; i < 16; ++i) {
                const bool pad = i >= 8;
                const auto slot = static_cast<std::size_t>(i % 8);
                const float x = pad ? 330.F : 30.F, y = 84.F + static_cast<float>(i % 8) * 21.F;
                if (settings_row == i)
                    rect(renderer, x - 6, y - 4, 278, 23, {26, 61, 65, 255});
                const char* value =
                    pad ? SDL_GetGamepadStringForButton(
                              static_cast<SDL_GamepadButton>(settings.buttons[slot]))
                        : SDL_GetScancodeName(static_cast<SDL_Scancode>(settings.keys[slot]));
                text(renderer,
                     x,
                     y,
                     std::string(pad ? "PAD " : "KEY ") + names[slot],
                     settings_row == i ? mint : ink);
                text(renderer,
                     x + 144,
                     y,
                     capture == i ? "[PRESS...]"
                     : value      ? value
                                  : "?",
                     muted);
            }
            if (settings_row == 16)
                rect(renderer, 24, 262, 585, 22, {26, 61, 65, 255});
            text(renderer,
                 30,
                 269,
                 std::string("OPTIONAL LEFT STICK: ") +
                     (settings.sticks ? "ON (.55/.35 HYSTERESIS)" : "OFF"));
            if (settings_row == 17)
                rect(renderer, 24, 290, 585, 22, {26, 61, 65, 255});
            text(renderer,
                 30,
                 297,
                 "CLASSIC / FUSION START LEVEL: " + number(settings.start_level) +
                     " / LEFT - RIGHT + / ENTER CYCLES");
            if (settings_row == 18)
                rect(renderer, 24, 316, 585, 22, {26, 61, 65, 255});
            text(renderer, 30, 323, "BACK / ESC OR PAD BACK");
        } else {
            const auto palette_speed =
                mode ? modern::gravity_interval(modern_state) : game::gravity_frames(state.level);
            constexpr float bx = 158, by = 61, cell = 14;
            rect(renderer, bx - 3, by - 3, 146, 286, {47, 67, 89, 255});
            rect(renderer, bx, by, 140, 280, {12, 22, 35, 255});
            for (int y = 0; y < 20; ++y)
                for (int x = 0; x < 10; ++x) {
                    rect(renderer, bx + x * cell, by + y * cell, 13, 13, {17, 29, 44, 255});
                    const auto value =
                        mode ? modern_state.board[static_cast<std::size_t>((y + 20) * 10 + x)]
                             : state.board[static_cast<std::size_t>(y * 10 + x)];
                    if (value)
                        block(renderer,
                              bx + x * cell,
                              by + y * cell,
                              mode && value <= 7
                                  ? client::classic_palette_piece(
                                        static_cast<modern::Piece>(value - 1))
                                  : static_cast<game::Piece>(value <= 7 ? value - 1 : 0),
                              palette_speed);
                }
            if (mode && !state.game_over && !spin_debug) {
                if (mode == 2)
                    for (auto c : modern::cells(modern::ghost(modern_state)))
                        if (c.y >= 0) {
                            color(renderer, {80, 109, 129, 255});
                            SDL_FRect outline{
                                bx + c.x * cell + 2, by + c.y * cell + 2, cell - 4, cell - 4};
                            SDL_RenderRect(renderer, &outline);
                        }
                for (auto c : modern::cells(modern_state.active))
                    if (c.y >= 0)
                        block(renderer,
                              bx + c.x * cell,
                              by + c.y * cell,
                              client::classic_palette_piece(modern_state.active.piece),
                              palette_speed);
            }
            if (!mode && !state.game_over && state.entry_timer == 0)
                for (const auto cell_position : game::cells(state.active))
                    if (cell_position.y >= 0)
                        block(renderer,
                              bx + cell_position.x * cell,
                              by + cell_position.y * cell,
                              state.active.piece,
                              palette_speed);
            text(renderer, 20, 68, playback ? "REPLAY" : practice ? "PRACTICE" : "MARATHON", mint);
            text(renderer, 20, 96, "SCORE", muted);
            text(renderer, 20, 111, number(state.score));
            text(renderer, 20, 139, "LINES", muted);
            text(renderer, 20, 154, number(state.lines));
            text(renderer, 20, 182, "LEVEL", muted);
            text(renderer, 20, 197, number(state.level));
            text(renderer, 20, 225, "SEED", muted);
            text(renderer, 20, 240, playback ? "RECORDED" : number(seed));
            text(renderer, 20, 268, "TICK", muted);
            text(renderer, 20, 283, number(state.tick));
            text(renderer, 320, 66, "NEXT", mint);
            if (mode) {
                for (int i = 0; i < 5; ++i) {
                    modern::Active preview{
                        modern_state.next[i],
                        modern::spawn_rotation(modern_state.next[i], modern_state.preset),
                        0,
                        0};
                    for (auto c : modern::cells(preview))
                        block(renderer,
                              320 + c.x * 10.F,
                              85 + i * 39.F + c.y * 10.F,
                              client::classic_palette_piece(preview.piece),
                              palette_speed,
                              10);
                }
                {
                    text(renderer, 20, 305, modern_state.hold_used ? "HOLD (USED)" : "HOLD", mint);
                    if (modern_state.has_hold)
                        for (auto c : modern::cells({modern_state.held_piece,
                                                     modern::spawn_rotation(modern_state.held_piece,
                                                                            modern_state.preset),
                                                     0,
                                                     0}))
                            block(renderer,
                                  20 + c.x * 9.F,
                                  319 + c.y * 9.F,
                                  client::classic_palette_piece(modern_state.held_piece),
                                  palette_speed,
                                  9);
                }
            } else {
                game::Active preview{state.next, game::spawn_rotation(state.next), 0, 0};
                for (auto c : game::cells(preview))
                    block(renderer, 320 + c.x * 14.F, 85 + c.y * 14.F, state.next, palette_speed);
            }
            if (spin_debug && practice && mode) {
                const auto positions =
                    modern::enumerate_spin_positions(modern_state, spin_debug_rotation);
                constexpr SDL_Color cw{72, 218, 255, 255}, ccw{245, 117, 220, 255},
                    multiple{255, 255, 255, 255};
                std::size_t hidden = 0;
                for (const auto& position : positions) {
                    const auto& reason = position.reason;
                    const auto tint = reason.cw && reason.ccw ? multiple : reason.cw ? cw : ccw;
                    bool visible = false;
                    color(renderer, tint);
                    for (const auto c : modern::cells(position.active)) {
                        if (c.y < 0)
                            continue;
                        visible = true;
                        SDL_FRect outline{
                            bx + c.x * cell + 3, by + c.y * cell + 3, cell - 6, cell - 6};
                        SDL_RenderRect(renderer, &outline);
                    }
                    if (!visible)
                        ++hidden;
                    // A small square marks the local origin; a cross marks the T pivot.
                    const auto& a = position.active;
                    if (a.x >= 0 && a.y >= 0)
                        rect(renderer, bx + a.x * cell + 1, by + a.y * cell + 1, 3, 3, tint);
                    if (a.y + 1 >= 0) {
                        const float px = bx + (a.x + 1.5F) * cell;
                        const float py = by + (a.y + 1.5F) * cell;
                        rect(renderer, px - 3, py - 1, 6, 2, tint);
                        rect(renderer, px - 1, py - 3, 2, 6, tint);
                    }
                }
                text(renderer, 382, 65, "SPIN INSPECTOR / FROZEN", mint);
                text(renderer, 382, 83, "F11 / ESC CLOSE", muted);
                text(renderer, 382, 101, "F12 ORIENTATION: " + number(spin_debug_rotation));
                text(renderer, 382, 119, "CANDIDATES IGNORE HISTORY", muted);
                text(renderer,
                     382,
                     137,
                     "ACCEPTED " + number(positions.size()) + " / HIDDEN " + number(hidden),
                     mint);
                text(renderer, 382, 157, "CW FIRST OPEN KICK", cw);
                text(renderer, 382, 173, "CCW FIRST OPEN KICK", ccw);
                text(renderer, 382, 189, "BOTH ROUTES", multiple);
                text(renderer, 382, 207, "SQUARE ORIGIN / + PIVOT", muted);
                text(renderer, 382, 223, "SOLID = BOARD BLOCKERS", muted);
                const auto actual = modern::spin_possibility(modern_state);
                text(renderer,
                     382,
                     244,
                     std::string("ACTUAL CANDIDATE: ") + (actual.possible() ? "YES" : "NO"),
                     mint);
                text(renderer, 382, 260, "CW " + number(actual.cw) + " CCW " + number(actual.ccw));
                text(renderer,
                     382,
                     276,
                     std::string("SCORING READY: ") + (actual.already_spin ? "YES" : "NO"),
                     muted);
                text(renderer, 382, 292, "SPIN SCORING REQUIRES LOCK", muted);
                text(renderer, 382, 308, "LOCAL ROUTE; NO PATH TEST", muted);
                text(renderer, 382, 324, "LIVE ACTIVE FILL HIDDEN", muted);
            } else if (screen == Screen::pause) {
                text(renderer, 414, 65, state.game_over ? "GAME OVER" : "PAUSED", mint);
                constexpr std::array<const char*, 7> menu{"RESUME",
                                                          "RESTART SEED",
                                                          "NEW SEED",
                                                          "STEP ONE TICK",
                                                          "CLEAR FIXTURE",
                                                          "SETTINGS",
                                                          "MAIN MENU"};
                for (int i = 0; i < 7; ++i) {
                    const float y = 89.F + i * 25.F;
                    if (selection == i)
                        rect(renderer, 406, y - 4, 214, 22, {26, 61, 65, 255});
                    text(renderer,
                         414,
                         y,
                         std::string(selection == i ? "> " : "  ") +
                             menu[static_cast<std::size_t>(i)],
                         selection == i ? mint : ink);
                }
            } else {
                text(renderer, 414, 66, mode_name(), mint);
                text(renderer, 414, 87, "CONTROLS", mint);
                unsigned control_row = 0;
                for (std::size_t i = 0; i < 8; ++i) {
                    if ((i == 6 && mode != 2) || (i == 7 && !mode))
                        continue;
                    text(renderer,
                         414,
                         106.F + static_cast<float>(control_row++) * 16.F,
                         std::string(names[i]) + ": " +
                             SDL_GetScancodeName(static_cast<SDL_Scancode>(settings.keys[i])),
                         muted);
                }
                text(renderer, 414, 243, "F2 RESTART  F3 NEW SEED", muted);
                text(renderer,
                     414,
                     259,
                     practice ? "F4 STEP / F5 FIXTURE" : "ESC / START: PAUSE",
                     muted);
                text(renderer, 414, 275, "F6 SAVE REPLAY", muted);
                if (mode) {
                    unsigned pending = 0;
                    for (const auto& packet : modern_state.incoming)
                        pending += packet.rows;
                    text(renderer,
                         414,
                         291,
                         "INCOMING " + number(pending) + " / COMBO " +
                             std::to_string(modern_state.combo),
                         mint);
                    text(renderer,
                         414,
                         307,
                         modern_state.back_to_back ? "BACK TO BACK" : "B2B READY",
                         muted);
                    if (practice)
                        text(renderer, 320, 329, "F7-9 SPIN F10 GARB F11 VIEW", muted);
                }
            }
            if (practice && !mode) {
                int row = 0;
                for (const auto& item : timeline)
                    text(renderer, 320, 310.F + static_cast<float>(row++) * 9.F, item, muted);
            }
        }
        rect(renderer, 0, 345, 640, 15, {19, 33, 49, 255});
        std::string status = notice;
        if (status.size() > 60)
            status.resize(60);
        text(renderer, 8, 349, status, behind ? SDL_Color{255, 154, 93, 255} : mint);
        text(renderer,
             500,
             349,
             behind ? "OVERLOAD " + number(overload) : "PADS " + number(pads.size()),
             muted);
        if (!capture_path.empty() &&
            (!running || (smoke_frames > 0 && frames + 1 >= smoke_frames))) {
            SDL_Surface* capture_surface = SDL_RenderReadPixels(renderer, nullptr);
            if (!capture_surface || !SDL_SaveBMP(capture_surface, capture_path.string().c_str()))
                std::fprintf(stderr, "Screenshot capture failed: %s\n", SDL_GetError());
            SDL_DestroySurface(capture_surface);
        }
        SDL_RenderPresent(renderer);
        ++frames;
        if (smoke_frames > 0 && frames >= smoke_frames)
            running = false;
        SDL_Delay(1);
    }
    poll_save(true);
    // Finish the active snapshot before scheduling the final run, then drain it.
    if (saves.retry_needed()) {
        saves.retry();
        poll_save(true);
    }
    save_recording();
    poll_save(true);
    std::printf("Client finished: frames=%d ticks=%llu game_ticks=%llu lines=%u late=%llu "
                "overload=%llu hash=%llu\n",
                frames,
                static_cast<unsigned long long>(ticks),
                static_cast<unsigned long long>(state.tick),
                state.lines,
                static_cast<unsigned long long>(late),
                static_cast<unsigned long long>(overload),
                static_cast<unsigned long long>(current_hash()));
    for (auto& [id, pad] : pads) {
        (void)id;
        SDL_CloseGamepad(pad.handle);
    }
    audio.shutdown();
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
