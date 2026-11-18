#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <stackpulse/input.hpp>

#include <algorithm>
#include <array>
#include <charconv>
#include <cstdio>
#include <deque>
#include <map>
#include <optional>
#include <string_view>

namespace {
using namespace stackpulse::input;
constexpr Device keyboard = 1ULL << 63;
constexpr std::array<const char*, action_count> names{
    "LEFT", "RIGHT", "DOWN", "CW", "CCW", "DROP", "HOLD", "PAUSE"};
std::optional<Action> key(SDL_Scancode code) {
    switch (code) {
    case SDL_SCANCODE_LEFT:
    case SDL_SCANCODE_A:
        return Action::left;
    case SDL_SCANCODE_RIGHT:
    case SDL_SCANCODE_D:
        return Action::right;
    case SDL_SCANCODE_DOWN:
    case SDL_SCANCODE_S:
        return Action::down;
    case SDL_SCANCODE_X:
    case SDL_SCANCODE_UP:
        return Action::clockwise;
    case SDL_SCANCODE_Z:
        return Action::counterclockwise;
    case SDL_SCANCODE_SPACE:
        return Action::drop;
    case SDL_SCANCODE_C:
        return Action::hold;
    case SDL_SCANCODE_ESCAPE:
        return Action::pause;
    default:
        return {};
    }
}
std::optional<Action> button(Uint8 code) {
    switch (code) {
    case SDL_GAMEPAD_BUTTON_DPAD_LEFT:
        return Action::left;
    case SDL_GAMEPAD_BUTTON_DPAD_RIGHT:
        return Action::right;
    case SDL_GAMEPAD_BUTTON_DPAD_DOWN:
        return Action::down;
    case SDL_GAMEPAD_BUTTON_SOUTH:
        return Action::clockwise;
    case SDL_GAMEPAD_BUTTON_WEST:
        return Action::counterclockwise;
    case SDL_GAMEPAD_BUTTON_NORTH:
        return Action::hold;
    case SDL_GAMEPAD_BUTTON_DPAD_UP:
        return Action::drop;
    case SDL_GAMEPAD_BUTTON_START:
        return Action::pause;
    default:
        return {};
    }
}
struct Pad {
    SDL_Gamepad* handle{};
    std::array<AxisHysteresis, 2> axes;
};
void text(SDL_Renderer* renderer, float x, float y, const char* message) {
    SDL_SetRenderDrawColor(renderer, 208, 222, 237, 255);
    SDL_RenderDebugText(renderer, x, y, message);
}
} // namespace

int main(int argc, char** argv) {
    int smoke_frames = 0;
    bool sticks = false;
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg = argv[i];
        if (arg == "--sticks")
            sticks = true;
        else if (arg == "--smoke-frames" && i + 1 < argc) {
            const std::string_view value = argv[++i];
            const auto parsed =
                std::from_chars(value.data(), value.data() + value.size(), smoke_frames);
            if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size() ||
                smoke_frames <= 0)
                return 2;
        } else {
            std::fprintf(stderr, "Usage: stackpulse_client [--sticks] [--smoke-frames N]\n");
            return 2;
        }
    }
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) {
        std::fprintf(stderr, "%s\n", SDL_GetError());
        return 1;
    }
    SDL_Window* window = nullptr;
    SDL_Renderer* renderer = nullptr;
    if (!SDL_CreateWindowAndRenderer("Stackpulse M0 - Input diagnostic",
                                     1280,
                                     720,
                                     SDL_WINDOW_RESIZABLE,
                                     &window,
                                     &renderer)) {
        std::fprintf(stderr, "%s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }
    SDL_SetWindowMinimumSize(window, 640, 360);
    if (!SDL_SetRenderLogicalPresentation(
            renderer, 640, 360, SDL_LOGICAL_PRESENTATION_INTEGER_SCALE)) {
        std::fprintf(stderr, "%s\n", SDL_GetError());
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
    SDL_SetRenderVSync(renderer, 1);
    Adapter input;
    std::map<SDL_JoystickID, Pad> pads;
    auto open_pad = [&](SDL_JoystickID id) {
        if (!pads.contains(id))
            if (auto* handle = SDL_OpenGamepad(id)) {
                pads.emplace(id, Pad{handle, {}});
                SDL_SetWindowTitle(window,
                                   "Stackpulse M0 - Input diagnostic / controller connected");
            }
    };
    int count = 0;
    SDL_JoystickID* ids = SDL_GetGamepads(&count);
    for (int i = 0; i < count; ++i)
        open_pad(ids[i]);
    SDL_free(ids);
    Timestamp boundary = 0;
    const Timestamp epoch = SDL_GetTicksNS();
    auto tick_boundary = [&](Timestamp index) {
        return epoch + (index / 60) * 1'000'000'000 + (index % 60) * 1'000'000'000 / 60;
    };
    Timestamp next_tick = tick_boundary(1);
    unsigned long long ticks = 0, late = 0, skipped = 0;
    bool focused = true, running = true;
    int frames = 0;
    TickInput current;
    std::deque<Transition> history;
    // SDL timestamps share SDL_GetTicksNS's epoch. Delayed events are moved to
    // the first unsampled instant, counted visibly; never retroactively resimulate.
    auto push = [&](Transition transition, bool repeat = false) {
        if (repeat)
            return;
        transition.source_timestamp = transition.timestamp;
        if (transition.timestamp <= boundary) {
            transition.timestamp = boundary + 1;
            ++late;
        }
        input.push(transition);
    };
    std::printf("Stackpulse M0 diagnostic: SDL %d, 640x360 integer letterbox, 60Hz diagnostic "
                "samples, pads=%zu, smoke_frames=%d\n",
                SDL_GetVersion(),
                pads.size(),
                smoke_frames);
    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            switch (event.type) {
            case SDL_EVENT_QUIT:
                running = false;
                break;
            case SDL_EVENT_WINDOW_FOCUS_LOST:
                focused = false;
                input.release_all(std::max(event.common.timestamp, boundary + 1));
                for (auto& [id, pad] : pads) {
                    (void)id;
                    for (auto& axis : pad.axes)
                        axis.reset();
                }
                break;
            case SDL_EVENT_WINDOW_FOCUS_GAINED:
                focused = true;
                break;
            case SDL_EVENT_KEYBOARD_REMOVED:
                input.release_device(std::max(event.kdevice.timestamp, boundary + 1),
                                     keyboard | event.kdevice.which);
                break;
            case SDL_EVENT_KEY_DOWN:
            case SDL_EVENT_KEY_UP:
                if (focused)
                    if (auto action = key(event.key.scancode))
                        push({event.key.timestamp,
                              keyboard | event.key.which,
                              static_cast<std::uint32_t>(event.key.scancode),
                              *action,
                              event.key.down},
                             event.key.repeat);
                break;
            case SDL_EVENT_GAMEPAD_ADDED:
                open_pad(event.gdevice.which);
                break;
            case SDL_EVENT_GAMEPAD_REMOVED:
                input.release_device(std::max(event.gdevice.timestamp, boundary + 1),
                                     event.gdevice.which);
                if (auto pad = pads.find(event.gdevice.which); pad != pads.end()) {
                    SDL_CloseGamepad(pad->second.handle);
                    pads.erase(pad);
                    SDL_SetWindowTitle(
                        window, "Stackpulse M0 - Controller disconnected; reconnect to resume");
                }
                break;
            case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
            case SDL_EVENT_GAMEPAD_BUTTON_UP:
                if (focused && pads.contains(event.gbutton.which))
                    if (auto action = button(event.gbutton.button))
                        push({event.gbutton.timestamp,
                              event.gbutton.which,
                              event.gbutton.button,
                              *action,
                              event.gbutton.down});
                break;
            case SDL_EVENT_GAMEPAD_AXIS_MOTION:
                if (focused && sticks && event.gaxis.axis <= SDL_GAMEPAD_AXIS_LEFTY) {
                    auto pad = pads.find(event.gaxis.which);
                    if (pad == pads.end())
                        break;
                    const bool horizontal = event.gaxis.axis == SDL_GAMEPAD_AXIS_LEFTX;
                    const int direction = pad->second.axes[event.gaxis.axis].update(
                        static_cast<float>(event.gaxis.value) / 32768.F);
                    if (horizontal) {
                        push({event.gaxis.timestamp,
                              event.gaxis.which,
                              100,
                              Action::left,
                              direction == -1});
                        push({event.gaxis.timestamp,
                              event.gaxis.which,
                              101,
                              Action::right,
                              direction == 1});
                    } else
                        push({event.gaxis.timestamp,
                              event.gaxis.which,
                              102,
                              Action::down,
                              direction == 1});
                }
                break;
            default:
                break;
            }
        }
        const auto now = SDL_GetTicksNS();
        auto sample = [&](Timestamp timestamp) {
            current = input.sample(timestamp);
            boundary = timestamp;
            for (const auto& transition : current.transitions) {
                history.push_front(transition);
                if (history.size() > 12)
                    history.pop_back();
            }
        };
        int catchup = 0;
        while (next_tick <= now && catchup++ < 8) {
            sample(next_tick);
            next_tick = tick_boundary(++ticks + 1);
        }
        if (next_tick <= now) {
            // Offline diagnostic overload: collapse overdue samples, retain raw
            // transitions, and expose skipped ticks. No simulation runs here.
            const Timestamp elapsed = now - epoch;
            const Timestamp due =
                (elapsed / 1'000'000'000) * 60 + (elapsed % 1'000'000'000) * 60 / 1'000'000'000;
            if (due > ticks) {
                sample(tick_boundary(due));
                skipped += due - ticks;
                ticks = due;
            }
            next_tick = tick_boundary(ticks + 1);
        }
        SDL_SetRenderDrawColor(renderer, 12, 19, 30, 255);
        SDL_RenderClear(renderer);
        text(renderer, 16, 12, "STACKPULSE / M0 INPUT LAB");
        text(renderer, 16, 28, "DIAGNOSTIC ONLY - NO GAMEPLAY OR FIDELITY CLAIM");
        text(renderer, 16, 48, "ACTION");
        text(renderer, 88, 48, "HELD");
        text(renderer, 132, 48, "PRESS");
        text(renderer, 176, 48, "REL");
        text(renderer, 232, 48, "RAW TIMELINE (ns / binding)");
        for (std::size_t i = 0; i < action_count; ++i) {
            const float y = 70.F + static_cast<float>(i) * 23.F;
            text(renderer, 16, y + 5, names[i]);
            for (int column = 0; column < 3; ++column) {
                const bool enabled = column == 0   ? current.held[i]
                                     : column == 1 ? current.pressed[i]
                                                   : current.released[i];
                SDL_SetRenderDrawColor(
                    renderer, enabled ? 64 : 29, enabled ? 210 : 43, enabled ? 175 : 60, 255);
                const SDL_FRect box{90.F + static_cast<float>(column) * 45.F, y, 30, 18};
                SDL_RenderFillRect(renderer, &box);
            }
        }
        int row = 0;
        for (const auto& transition : history) {
            char line[96];
            std::snprintf(line,
                          sizeof(line),
                          "%llu %c%llu %s %s b%u%s%s",
                          static_cast<unsigned long long>(transition.source_timestamp
                                                              ? transition.source_timestamp
                                                              : transition.timestamp),
                          (transition.device & keyboard) ? 'K' : 'G',
                          static_cast<unsigned long long>(transition.device & ~keyboard),
                          names[static_cast<std::size_t>(transition.action)],
                          transition.down ? "+" : "-",
                          transition.binding,
                          transition.synthetic ? " clear" : "",
                          transition.source_timestamp &&
                                  transition.source_timestamp != transition.timestamp
                              ? " late"
                              : "");
            text(renderer, 232, 70.F + static_cast<float>(row++) * 14.F, line);
        }
        char status[96];
        std::snprintf(status,
                      sizeof(status),
                      "tick %llu pads %zu focus %s late %llu skipped %llu",
                      ticks,
                      pads.size(),
                      focused ? "YES" : "NO",
                      late,
                      skipped);
        text(renderer, 16, 270, status);
        text(renderer, 16, 288, "Arrows/A D S: directions  X/Z: rotate  Space: drop  C: hold");
        text(renderer, 16, 304, "Gamepad: D-pad / South / West / North / Start");
        text(renderer,
             16,
             320,
             sticks ? "Stick enabled: engage .55 / release .35"
                    : "Stick disabled; launch with --sticks to enable");
        text(renderer, 16, 340, "60Hz samples; sub-tick taps remain raw events only");
        SDL_RenderPresent(renderer);
        ++frames;
        if (smoke_frames > 0 && frames >= smoke_frames)
            running = false;
        SDL_Delay(1);
    }
    std::printf("Diagnostic finished: frames=%d ticks=%llu late=%llu skipped=%llu\n",
                frames,
                ticks,
                late,
                skipped);
    for (auto& [id, pad] : pads) {
        (void)id;
        SDL_CloseGamepad(pad.handle);
    }
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
