#include "audio.hpp"
#include "replay_library.hpp"
#include "replay_seek.hpp"
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
#include <stackpulse/seeds.hpp>
#include <string>
#include <string_view>

namespace {
namespace game = stackpulse::classic;
namespace in = stackpulse::input;
namespace replay = stackpulse::replay;
namespace modern = stackpulse::modern;
namespace mr = stackpulse::modern_replay;
namespace client = stackpulse::client;
namespace rl = stackpulse::client::replay_library;
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
enum class Screen { home, level, replays, play, pause, settings };
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
// Name acceptance is deliberately separate from lobby discovery.
std::optional<stackpulse::net::ConnectOptions>
online_menu(SDL_Renderer* renderer,
            modern::Preset preset,
            const std::filesystem::path& preferences,
            bool& quit,
            int smoke_frames = 0,
            const std::filesystem::path& capture_path = {}) {
    namespace net = stackpulse::net;
    net::ConnectOptions options;
    options.preset = preset;
    options.browse_only = true;
    options.name = "Player" + std::to_string(SDL_GetTicks() % 10000);
    std::ifstream saved(preferences);
    std::string saved_name;
    std::getline(saved, options.host);
    std::getline(saved, saved_name);
    if (options.host.empty() || options.host.size() > 128)
        options.host = "127.0.0.1";
    if (!saved_name.empty() && saved_name.size() <= 24 &&
        std::all_of(saved_name.begin(), saved_name.end(), [](unsigned char c) {
            return c >= 32 && c <= 126;
        }))
        options.name = saved_name;
    std::map<SDL_JoystickID, SDL_Gamepad*> pads;
    auto open = [&](SDL_JoystickID id) {
        if (!pads.contains(id))
            if (auto* p = SDL_OpenGamepad(id))
                pads[id] = p;
    };
    int count = 0;
    auto* ids = SDL_GetGamepads(&count);
    for (int i = 0; i < count; ++i)
        open(ids[i]);
    SDL_free(ids);
    int row = 0, frames = 0;
    bool editing = false, done = false, accepted = false;
    while (!done) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_EVENT_QUIT) {
                quit = true;
                done = true;
            } else if (e.type == SDL_EVENT_GAMEPAD_ADDED)
                open(e.gdevice.which);
            else if (e.type == SDL_EVENT_GAMEPAD_REMOVED) {
                if (auto it = pads.find(e.gdevice.which); it != pads.end()) {
                    SDL_CloseGamepad(it->second);
                    pads.erase(it);
                }
            } else if (editing && e.type == SDL_EVENT_TEXT_INPUT) {
                auto& value = row == 1 ? options.name : options.host;
                const auto limit = row == 1 ? 24U : 128U;
                for (unsigned char c : std::string(e.text.text))
                    if (c >= 32 && c <= 126 && value.size() < limit)
                        value += char(c);
            } else if ((e.type == SDL_EVENT_KEY_DOWN && !e.key.repeat) ||
                       e.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN) {
                bool pad = e.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN;
                int code = pad ? e.gbutton.button : e.key.scancode;
                auto key = [&](int k, int b) { return code == (pad ? b : k); };
                if (key(SDL_SCANCODE_ESCAPE, SDL_GAMEPAD_BUTTON_EAST)) {
                    if (editing) {
                        editing = false;
                        SDL_StopTextInput(SDL_GetRenderWindow(renderer));
                    } else
                        done = true;
                } else if (editing) {
                    if (!pad && code == SDL_SCANCODE_BACKSPACE) {
                        auto& value = row == 1 ? options.name : options.host;
                        if (!value.empty())
                            value.pop_back();
                    }
                    if (key(SDL_SCANCODE_RETURN, SDL_GAMEPAD_BUTTON_SOUTH)) {
                        editing = false;
                        SDL_StopTextInput(SDL_GetRenderWindow(renderer));
                    }
                } else if (key(SDL_SCANCODE_UP, SDL_GAMEPAD_BUTTON_DPAD_UP))
                    row = (row + 2) % 3;
                else if (key(SDL_SCANCODE_DOWN, SDL_GAMEPAD_BUTTON_DPAD_DOWN))
                    row = (row + 1) % 3;
                else if (key(SDL_SCANCODE_RETURN, SDL_GAMEPAD_BUTTON_SOUTH) ||
                         (pad && code == SDL_GAMEPAD_BUTTON_START)) {
                    if (row == 0 && !options.name.empty() && !options.host.empty()) {
                        accepted = true;
                        done = true;
                    } else {
                        editing = true;
                        SDL_StartTextInput(SDL_GetRenderWindow(renderer));
                    }
                }
            }
        }
        color(renderer, {9, 15, 27, 255});
        SDL_RenderClear(renderer);
        text(renderer, 40, 44, "WELCOME / " + options.name, mint);
        std::array<std::string, 3> rows{
            "ACCEPT NAME", "CHOOSE NAME: " + options.name, "SERVER: " + options.host};
        for (int i = 0; i < 3; ++i)
            text(renderer,
                 40,
                 100.F + i * 36.F,
                 std::string(row == i ? "> " : "  ") + rows[i],
                 row == i ? mint : ink);
        if (smoke_frames && ++frames >= smoke_frames) {
            if (!capture_path.empty()) {
                auto* capture = SDL_RenderReadPixels(renderer, nullptr);
                if (capture)
                    SDL_SaveBMP(capture, capture_path.string().c_str());
                SDL_DestroySurface(capture);
            }
            done = true;
        }
        SDL_RenderPresent(renderer);
        SDL_Delay(8);
    }
    SDL_StopTextInput(SDL_GetRenderWindow(renderer));
    for (auto [id, pad] : pads) {
        (void)id;
        SDL_CloseGamepad(pad);
    }
    if (accepted) {
        std::ofstream save(preferences);
        save << options.host << '\n' << options.name << '\n';
        return options;
    }
    return {};
}
int run_online(SDL_Renderer* renderer,
               const Settings& settings,
               ClientAudio& audio,
               const std::string& host,
               std::uint16_t port,
               modern::Preset preset,
               bool bot,
               std::uint64_t exit_ticks,
               const std::filesystem::path& capture_path = {},
               const stackpulse::net::ConnectOptions* room_options = nullptr,
               std::uint64_t capture_tick = 0,
               std::uint64_t ready_delay_ms = 0,
               const std::string& smoke_lobby_stage = {},
               bool smoke_connect_error = false) {
    namespace net = stackpulse::net;
    net::Client connection;
    net::View timeout_view;
    bool connect_timed_out = false;
    auto connect_started = SDL_GetTicksNS();
    client::OnlinePrediction prediction;
    client::OnlineSoundObserver online_sounds;
    net::ConnectOptions options;
    if (room_options)
        options = *room_options;
    else {
        options.host = host;
        options.port = port;
        options.preset = preset;
    }
    if (smoke_lobby_stage.empty() && !connection.connect(options)) {
        std::fprintf(stderr, "Online connection: %s\n", connection.view().detail.c_str());
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
    std::uint64_t previous_revision = 0, room_epoch = 0, match_epoch = 0;
    std::uint16_t previous_player = 0, featured = 0;
    modern::Preset epoch_preset = preset;
    std::uint8_t epoch_start_level = 0;
    bool target_held = false;
    int overlay_selection = 0;
    enum class LobbyPage { choice, create, browse, password };
    LobbyPage lobby_page = smoke_lobby_stage == "create" ? LobbyPage::create : LobbyPage::choice;
    int smoke_lobby_frames = 0;
    int connect_error_frames = 0;
    bool connect_error_captured = capture_path.empty();
    net::RoomOptions draft;
    draft.preset = preset;
    int lobby_row = 0;
    std::vector<net::RoomInfo> previous_directory;
    int error_row = 0;
    std::string password;
    client::RoomSelection room_selection;
    bool lobby_editing = false;
    int room_row = 0;
    auto room_phase = net::Phase::connecting;
    std::uint64_t directory_refresh = 0;
    auto lobby_field = [&]() -> std::string* {
        if (lobby_page == LobbyPage::password)
            return &password;
        if (lobby_page == LobbyPage::create && lobby_row < 2)
            return lobby_row == 0 ? &draft.name : &draft.password;
        return nullptr;
    };
    bool live_captured = false;
    bool running = true, focused = true, overlay = false, terminal = false,
         quit_application = false, ready_requested = false;
    auto release = [&]() {
        target_held = false;
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
        if (!board.game_over && (!board.classic_state || board.classic_state->entry_timer == 0))
            for (auto c : modern::cells(board.active))
                if (c.y >= 0 && c.y < 20)
                    block(renderer,
                          bx + c.x * cell,
                          by + c.y * cell,
                          client::classic_palette_piece(board.active.piece),
                          speed,
                          cell);
    };
    net::View smoke_view;
    const bool smoke_room = smoke_lobby_stage == "waiting" || smoke_lobby_stage == "countdown" ||
                            smoke_lobby_stage == "results";
    if (!smoke_lobby_stage.empty() && !smoke_room)
        smoke_view.phase = net::Phase::browsing;
    if (smoke_lobby_stage == "browse") {
        net::RoomInfo open_room;
        open_room.code = "OPEN";
        open_room.name = "Evening lobby";
        open_room.players = 2;
        open_room.capacity = 8;
        net::RoomInfo locked_room = open_room;
        locked_room.code = "LOCK";
        locked_room.name = "Friends lobby";
        locked_room.password_required = true;
        locked_room.preset = modern::Preset::classic;
        smoke_view.rooms = {open_room, locked_room};
        previous_directory = smoke_view.rooms;
        lobby_page = LobbyPage::browse;
        lobby_row = static_cast<int>(smoke_view.rooms.size()) + 1;
    }
    if (smoke_room) {
        smoke_view.phase = smoke_lobby_stage == "results"     ? net::Phase::results
                           : smoke_lobby_stage == "countdown" ? net::Phase::countdown
                                                              : net::Phase::waiting;
        smoke_view.room_id = 1;
        smoke_view.room_code = "ABCD";
        smoke_view.room.name = "Evening lobby";
        smoke_view.room.preset = preset;
        smoke_view.member_id = smoke_view.manager_id = smoke_view.player = 1;
        smoke_view.player_limit = 8;
        smoke_view.countdown = 180;
        smoke_view.members = {{1, 1, "Player", net::Role::player, false, true},
                              {2, 2, "Rival", net::Role::player, true, true}};
    }
    while (running) {
        if (smoke_lobby_stage.empty())
            connection.poll();
        const auto now = SDL_GetTicksNS();
        if (smoke_lobby_stage == "error") {
            smoke_view.phase = net::Phase::error;
            smoke_view.detail = "Connection timed out";
        }
        if (connection.view().phase == net::Phase::connecting &&
            now - connect_started >= 5'000'000'000ULL && !connect_timed_out) {
            timeout_view = connection.view();
            timeout_view.phase = net::Phase::error;
            timeout_view.detail = "Connection timed out. Check the server address and try again.";
            connection.disconnect();
            connect_timed_out = true;
        }
        const auto& view = !smoke_lobby_stage.empty() ? smoke_view
                           : connect_timed_out        ? timeout_view
                                                      : connection.view();
        const bool connection_failed =
            view.phase == net::Phase::error || view.phase == net::Phase::disconnected;
        if (view.phase == net::Phase::browsing && lobby_page == LobbyPage::browse)
            lobby_row = client::directory_selection(lobby_row, previous_directory, view.rooms);
        previous_directory = view.rooms;
        const auto room_actions = client::room_actions(view);
        if (room_phase != view.phase || room_row >= static_cast<int>(room_actions.size()))
            room_row = 0;
        room_phase = view.phase;
        if (view.room_id)
            preset = view.room.preset;
        if (view.phase == net::Phase::browsing && now >= directory_refresh) {
            connection.list_rooms();
            directory_refresh = now + 2'000'000'000;
        }
        if (room_epoch != view.room_id || match_epoch != view.match_id ||
            (view.room_id &&
             (epoch_preset != view.room.preset || epoch_start_level != view.room.start_level)) ||
            previous_player != view.player || view.phase == net::Phase::disconnected ||
            view.phase == net::Phase::error) {
            prediction.reset();
            input = in::Adapter{};
            target_held = false;
            room_epoch = view.room_id;
            epoch_preset = view.room.preset;
            epoch_start_level = view.room.start_level;
            match_epoch = view.match_id;
            previous_player = view.player;
            previous_revision = 0;
            ready_requested = false;
            terminal = false;
            result_time = 0;
            snapshot_time = now;
            clock_tick = view.server_tick;
            boundary = now;
            next_tick = now + 1'000'000'000 / 60;
            audio.set_playing(false);
        }
        if (view.snapshot_revision != previous_revision && view.has_snapshot) {
            if (view.role == net::Role::spectator || prediction.apply(view)) {
                previous_revision = view.snapshot_revision;
                snapshot_time = now;
                clock_tick = view.server_tick;
            }
        }
        if (bot && view.role == net::Role::player && view.phase == net::Phase::waiting &&
            !view.ready && !ready_requested && (now - started) / 1'000'000 >= ready_delay_ms) {
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
                target_held = false;
                input.release_device(std::max(event.gdevice.timestamp, boundary + 1),
                                     event.gdevice.which);
                if (auto it = pads.find(event.gdevice.which); it != pads.end()) {
                    SDL_CloseGamepad(it->second.handle);
                    pads.erase(it);
                }
            } else if (event.type == SDL_EVENT_TEXT_INPUT && lobby_editing) {
                if (auto* value = lobby_field())
                    for (unsigned char c : std::string(event.text.text))
                        if (c >= 32 && c <= 126 && value->size() < 24)
                            *value += char(c);
            } else if (event.type == SDL_EVENT_KEYBOARD_REMOVED) {
                target_held = false;
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
                if (connection_failed || view.phase == net::Phase::connecting) {
                    if (!down)
                        continue;
                    auto key = [&](int k, int b) { return code == (pad ? b : k); };
                    if (key(SDL_SCANCODE_ESCAPE, SDL_GAMEPAD_BUTTON_EAST) ||
                        (pad && code == SDL_GAMEPAD_BUTTON_BACK))
                        running = false;
                    else if (connection_failed &&
                             (key(SDL_SCANCODE_UP, SDL_GAMEPAD_BUTTON_DPAD_UP) ||
                              key(SDL_SCANCODE_DOWN, SDL_GAMEPAD_BUTTON_DPAD_DOWN)))
                        error_row = 1 - error_row;
                    else if (connection_failed &&
                             (key(SDL_SCANCODE_RETURN, SDL_GAMEPAD_BUTTON_SOUTH) ||
                              (pad && code == SDL_GAMEPAD_BUTTON_START))) {
                        if (error_row)
                            running = false;
                        else {
                            release();
                            overlay = false;
                            connect_timed_out = false;
                            connect_started = SDL_GetTicksNS();
                            connection.connect(options);
                        }
                    }
                    continue;
                }
                if (view.phase == net::Phase::browsing) {
                    if (!down)
                        continue;
                    auto key = [&](int k, int b) { return code == (pad ? b : k); };
                    if (lobby_editing) {
                        if (key(SDL_SCANCODE_RETURN, SDL_GAMEPAD_BUTTON_SOUTH) ||
                            key(SDL_SCANCODE_ESCAPE, SDL_GAMEPAD_BUTTON_EAST)) {
                            lobby_editing = false;
                            SDL_StopTextInput(SDL_GetRenderWindow(renderer));
                        } else if (!pad && code == SDL_SCANCODE_BACKSPACE) {
                            if (auto* value = lobby_field(); value && !value->empty())
                                value->pop_back();
                        }
                        continue;
                    }
                    if (key(SDL_SCANCODE_ESCAPE, SDL_GAMEPAD_BUTTON_EAST)) {
                        if (lobby_page == LobbyPage::choice)
                            running = false;
                        else {
                            lobby_page = LobbyPage::choice;
                            lobby_row = 0;
                        }
                        continue;
                    }
                    int rows = lobby_page == LobbyPage::choice   ? 3
                               : lobby_page == LobbyPage::create ? 7
                               : lobby_page == LobbyPage::password
                                   ? 2
                                   : static_cast<int>(view.rooms.size()) + 2;
                    if (key(SDL_SCANCODE_UP, SDL_GAMEPAD_BUTTON_DPAD_UP))
                        lobby_row = (lobby_row + rows - 1) % rows;
                    else if (key(SDL_SCANCODE_DOWN, SDL_GAMEPAD_BUTTON_DPAD_DOWN))
                        lobby_row = (lobby_row + 1) % rows;
                    else if (lobby_page == LobbyPage::create &&
                             (key(SDL_SCANCODE_LEFT, SDL_GAMEPAD_BUTTON_DPAD_LEFT) ||
                              key(SDL_SCANCODE_RIGHT, SDL_GAMEPAD_BUTTON_DPAD_RIGHT))) {
                        int delta = key(SDL_SCANCODE_LEFT, SDL_GAMEPAD_BUTTON_DPAD_LEFT) ? -1 : 1;
                        if (lobby_row == 2)
                            draft.players = static_cast<std::uint8_t>(
                                std::clamp(int(draft.players) + delta, 2, 8));
                        if (lobby_row == 3)
                            draft.preset =
                                static_cast<modern::Preset>((int(draft.preset) + delta + 3) % 3);
                        if (lobby_row == 4)
                            draft.start_level = static_cast<std::uint8_t>(
                                std::clamp(int(draft.start_level) + delta, 0, 29));
                    } else if (key(SDL_SCANCODE_RETURN, SDL_GAMEPAD_BUTTON_SOUTH) ||
                               (pad && code == SDL_GAMEPAD_BUTTON_START)) {
                        if (lobby_page == LobbyPage::choice) {
                            if (lobby_row == 2) {
                                running = false;
                                continue;
                            }
                            lobby_page = lobby_row == 0 ? LobbyPage::create : LobbyPage::browse;
                            lobby_row = 0;
                            connection.list_rooms();
                        } else if (lobby_page == LobbyPage::create) {
                            if (lobby_row < 2) {
                                lobby_editing = true;
                                SDL_StartTextInput(SDL_GetRenderWindow(renderer));
                            } else if (lobby_row == 5 && !draft.name.empty()) {
                                if (draft.preset == modern::Preset::modern)
                                    draft.start_level = 0;
                                connection.create_room(draft);
                            } else if (lobby_row == 6) {
                                lobby_page = LobbyPage::choice;
                                lobby_row = 0;
                            }
                        } else if (lobby_page == LobbyPage::browse) {
                            const auto directory_action =
                                client::directory_action(lobby_row, view.rooms.size());
                            if (directory_action == client::DirectoryAction::back) {
                                lobby_page = LobbyPage::choice;
                                lobby_row = 0;
                                continue;
                            }
                            if (directory_action == client::DirectoryAction::refresh) {
                                connection.list_rooms();
                                continue;
                            }
                            const auto& room = view.rooms[static_cast<std::size_t>(lobby_row)];
                            room_selection.choose(room, net::Role::player);
                            password.clear();
                            if (room.password_required) {
                                lobby_page = LobbyPage::password;
                                lobby_row = 0;
                            } else
                                room_selection.confirm(connection, "");
                        } else if (lobby_row == 0) {
                            lobby_editing = true;
                            SDL_StartTextInput(SDL_GetRenderWindow(renderer));
                        } else
                            room_selection.confirm(connection, password);
                    } else if (lobby_page == LobbyPage::browse &&
                               ((!pad && code == SDL_SCANCODE_W) ||
                                (pad && code == SDL_GAMEPAD_BUTTON_WEST)) &&
                               lobby_row < static_cast<int>(view.rooms.size())) {
                        const auto& room = view.rooms[static_cast<std::size_t>(lobby_row)];
                        room_selection.choose(room, net::Role::spectator);
                        password.clear();
                        if (room.password_required) {
                            lobby_page = LobbyPage::password;
                            lobby_row = 0;
                        } else
                            room_selection.confirm(connection, "");
                    }
                    continue;
                }
                if (down && !overlay && !room_actions.empty()) {
                    auto key = [&](int k, int b) { return code == (pad ? b : k); };
                    const int rows = static_cast<int>(room_actions.size());
                    if (key(SDL_SCANCODE_UP, SDL_GAMEPAD_BUTTON_DPAD_UP)) {
                        room_row = (room_row + rows - 1) % rows;
                        continue;
                    }
                    if (key(SDL_SCANCODE_DOWN, SDL_GAMEPAD_BUTTON_DPAD_DOWN)) {
                        room_row = (room_row + 1) % rows;
                        continue;
                    }
                    if (key(SDL_SCANCODE_RETURN, SDL_GAMEPAD_BUTTON_SOUTH) ||
                        (pad && code == SDL_GAMEPAD_BUTTON_START)) {
                        const auto selected = room_actions[static_cast<std::size_t>(room_row)];
                        if (selected == client::RoomAction::leave) {
                            release();
                            connection.leave_room();
                            lobby_page = LobbyPage::browse;
                            lobby_row = room_row = 0;
                            ready_requested = false;
                            directory_refresh = 0;
                        } else if (selected == client::RoomAction::return_to_lobby) {
                            connection.return_to_lobby();
                            ready_requested = false;
                        } else
                            connection.set_ready(!view.ready);
                        continue;
                    }
                }
                if (down && view.phase == net::Phase::waiting &&
                    view.member_id == view.manager_id && preset != modern::Preset::modern &&
                    ((!pad && (code == SDL_SCANCODE_LEFT || code == SDL_SCANCODE_RIGHT)) ||
                     (pad && (code == SDL_GAMEPAD_BUTTON_DPAD_LEFT ||
                              code == SDL_GAMEPAD_BUTTON_DPAD_RIGHT)))) {
                    auto updated = view.room;
                    bool left =
                        pad ? code == SDL_GAMEPAD_BUTTON_DPAD_LEFT : code == SDL_SCANCODE_LEFT;
                    updated.start_level = static_cast<std::uint8_t>(
                        std::clamp(int(updated.start_level) + (left ? -1 : 1), 0, 29));
                    connection.configure_room(updated);
                    continue;
                }
                if ((!pad && code == SDL_SCANCODE_TAB) ||
                    (pad && code == SDL_GAMEPAD_BUTTON_RIGHT_STICK)) {
                    target_held = down;
                    if (down && view.role == net::Role::spectator && view.has_snapshot &&
                        !view.authoritative.players.empty()) {
                        const auto& players = view.authoritative.players;
                        auto it = std::find_if(players.begin(), players.end(), [&](const auto& p) {
                            return p.id == featured;
                        });
                        featured = it == players.end() || ++it == players.end() ? players.front().id
                                                                                : it->id;
                        connection.set_featured(featured);
                    }
                    continue;
                }
                const auto logical = action(code, pad);
                if (down) {
                    if (overlay &&
                        ((!pad && (code == SDL_SCANCODE_UP || code == SDL_SCANCODE_DOWN)) ||
                         (pad && (code == SDL_GAMEPAD_BUTTON_DPAD_UP ||
                                  code == SDL_GAMEPAD_BUTTON_DPAD_DOWN)))) {
                        const bool up =
                            pad ? code == SDL_GAMEPAD_BUTTON_DPAD_UP : code == SDL_SCANCODE_UP;
                        overlay_selection = (overlay_selection + (up ? 2 : 1)) % 3;
                        continue;
                    }
                    if (overlay && ((!pad && code == SDL_SCANCODE_RETURN) ||
                                    (pad && code == SDL_GAMEPAD_BUTTON_SOUTH))) {
                        release();
                        if (overlay_selection == 1) {
                            ready_requested = false;
                            prediction.reset();
                            connect_started = SDL_GetTicksNS();
                            connect_timed_out = false;
                            connection.reconnect();
                        } else if (overlay_selection == 2) {
                            connection.leave_room();
                            lobby_page = LobbyPage::browse;
                            lobby_row = room_row = 0;
                            ready_requested = false;
                            directory_refresh = 0;
                        }
                        overlay = false;
                        continue;
                    }
                    if ((!pad && code == SDL_SCANCODE_F10) ||
                        (pad && code == SDL_GAMEPAD_BUTTON_BACK)) {
                        running = false;
                        release();
                        continue;
                    }
                    if (!pad && code == SDL_SCANCODE_F9) {
                        release();
                        ready_requested = false;
                        prediction.reset();
                        connect_started = SDL_GetTicksNS();
                        connect_timed_out = false;
                        connection.reconnect();
                        continue;
                    }
                    if (logical == in::Action::pause) {
                        overlay = !overlay;
                        release();
                        continue;
                    }
                }
                if (logical && *logical != in::Action::pause && !overlay &&
                    view.role == net::Role::player)
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
        audio.set_playing(
            (view.phase == net::Phase::playing || view.phase == net::Phase::results) && !overlay &&
            (focused || bot));
        for (const auto cue : online_sounds.observe(view))
            audio.play(cue);
        int catchup = 0;
        while (next_tick <= now && catchup++ < 8) {
            boundary = next_tick;
            next_tick += 1'000'000'000 / 60;
            const auto sampled = input.sample(boundary);
            if (view.role == net::Role::spectator || view.phase != net::Phase::playing ||
                !prediction.initialized())
                continue;
            auto mask = overlay || (!focused && !bot) ? 0U : client::modern_input(sampled, preset);
            if (target_held && !overlay && focused)
                mask |= modern::target_next;
            if (bot) {
                const auto tick = prediction.board().tick;
                mask = tick % 4 < 2 ? modern::down : 0U;
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
        if (bot && !smoke_connect_error &&
            (view.phase == net::Phase::error || view.phase == net::Phase::disconnected) &&
            now - started > 5'000'000'000ULL)
            running = false;
        color(renderer, {9, 15, 27, 255});
        SDL_RenderClear(renderer);
        text(renderer, 20, 14, "STACKPULSE", mint);
        if (view.phase != net::Phase::browsing && (smoke_lobby_stage.empty() || smoke_room)) {
            text(renderer,
                 20,
                 32,
                 std::string(preset == modern::Preset::classic  ? "CLASSIC"
                             : preset == modern::Preset::fusion ? "FUSION"
                                                                : "MODERN") +
                     " / " + view.room.name,
                 muted);
            const std::array<const char*, 8> phases{"CONNECTING",
                                                    "WAITING",
                                                    "COUNTDOWN",
                                                    "PLAYING",
                                                    "RESULTS",
                                                    "DISCONNECTED",
                                                    "ERROR",
                                                    "LOBBIES"};
            text(renderer, 360, 14, phases[static_cast<std::size_t>(view.phase)], mint);
            text(renderer,
                 360,
                 32,
                 (view.role == net::Role::spectator ? std::string("SPECTATOR")
                                                    : "PLAYER " + number(view.player)) +
                     "",
                 muted);
        }
        const bool watching = view.role == net::Role::spectator;
        const modern::State* main_board = prediction.initialized() ? &prediction.board() : nullptr;
        std::uint16_t main_id = view.player;
        if (watching && view.has_snapshot && !view.authoritative.players.empty()) {
            auto it = std::find_if(view.authoritative.players.begin(),
                                   view.authoritative.players.end(),
                                   [&](const auto& p) { return p.id == featured; });
            if (it == view.authoritative.players.end())
                it = view.authoritative.players.begin();
            main_board = &it->board;
            main_id = featured = it->id;
        }
        auto player_label = [&](std::uint16_t id) {
            auto it = std::find_if(view.members.begin(), view.members.end(), [&](const auto& m) {
                return m.player == id && m.role == net::Role::player;
            });
            return it == view.members.end() ? "P" + number(id) : it->name;
        };
        std::uint16_t selected_target = 0;
        if (!watching && view.has_snapshot)
            for (const auto& player : view.authoritative.players)
                if (player.id == view.player)
                    selected_target = player.target;
        text(renderer, 16, 58, "LEADERBOARD", mint);
        if (main_board) {
            draw_board(*main_board, 165, 74, 12, !watching);
            text(renderer, 165, 57, player_label(main_id).substr(0, 16), mint);
            text(renderer,
                 165,
                 320,
                 "SCORE " + number(main_board->score) + " / LEVEL " +
                     number(modern::level(*main_board)));
            if (preset != modern::Preset::classic)
                text(renderer, 16, 268, main_board->hold_used ? "HOLD USED" : "HOLD", muted);
            if (preset != modern::Preset::classic && main_board->has_hold)
                for (auto c : modern::cells(
                         {main_board->held_piece,
                          modern::spawn_rotation(main_board->held_piece, main_board->preset),
                          0,
                          0}))
                    block(renderer,
                          24 + c.x * 7.F,
                          287 + c.y * 7.F,
                          client::classic_palette_piece(main_board->held_piece),
                          modern::gravity_interval(*main_board),
                          7);
            text(renderer, 300, 58, "NEXT", muted);
            for (std::size_t i = 0;
                 i < (preset == modern::Preset::classic ? 1U : main_board->next.size());
                 ++i)
                for (auto c :
                     modern::cells({main_board->next[i],
                                    modern::spawn_rotation(main_board->next[i], main_board->preset),
                                    0,
                                    0}))
                    block(renderer,
                          300 + c.x * 7.F,
                          80 + static_cast<float>(i) * 24 + c.y * 7.F,
                          client::classic_palette_piece(main_board->next[i]),
                          modern::gravity_interval(*main_board),
                          7);
        } else
            text(renderer, 165, 100, "AWAITING SNAPSHOT", muted);
        if (view.has_snapshot) {
            int rank = 0;
            for (const auto& player : client::leaderboard(view)) {
                const bool disconnected = player.status == client::PlayerStatus::disconnected;
                const bool out = player.status == client::PlayerStatus::out;
                std::string status = disconnected ? "DISCONNECTED" : out ? "OUT" : "";
                auto tint = disconnected ? SDL_Color{255, 174, 93, 255}
                            : out        ? SDL_Color{239, 125, 145, 255}
                                         : ink;
                ++rank;
                text(renderer,
                     16,
                     60.F + rank * 22.F,
                     number(rank) + " " +
                         player.name.substr(
                             0, std::min<std::size_t>(10, 14 - number(player.score).size())) +
                         " " + number(player.score),
                     tint);
                if (!status.empty())
                    text(renderer, 24, 72.F + rank * 22.F, status, tint);
            }
            unsigned slot = 0;
            for (const auto& opponent : view.authoritative.players) {
                if (opponent.id == main_id)
                    continue;
                const float x = 365.F + (slot % 4) * 68.F;
                const float y = 76.F + (slot / 4) * 130.F;
                draw_board(opponent.board, x, y, 5.F);
                const bool targeted = opponent.id == selected_target;
                std::string heading =
                    (targeted ? std::string(">P") : std::string("P")) + number(opponent.id);
                if (heading.size() < 6)
                    heading += " " + player_label(opponent.id).substr(0, 6 - heading.size());
                text(renderer,
                     x,
                     y - 14,
                     heading,
                     targeted                   ? SDL_Color{255, 205, 92, 255}
                     : opponent.board.game_over ? muted
                                                : mint);
                ++slot;
            }
        }
        if (view.phase == net::Phase::waiting || view.phase == net::Phase::countdown ||
            view.phase == net::Phase::results) {
            rect(renderer, 0, 51, 640, 290, {18, 32, 47, 255});
            text(renderer,
                 32,
                 62,
                 "ROOM " + view.room_code + " / " + (watching ? "SPECTATOR" : "PLAYER"),
                 mint);
            if (view.phase == net::Phase::results) {
                std::string result = "DRAW";
                if (view.has_snapshot &&
                    view.authoritative.result == stackpulse::match::Result::winner)
                    for (const auto& p : view.authoritative.players)
                        if (!p.placement)
                            result = player_label(p.id).substr(0, 24) + " WINS";
                text(renderer, 32, 85, "SERVER RESULT: " + result, mint);
                if (view.member_id != view.manager_id)
                    text(renderer, 32, 106, "WAITING FOR MANAGER TO RETURN ROOM", ink);
            } else if (view.phase == net::Phase::countdown)
                text(renderer, 32, 85, "START IN " + number((view.countdown + 59) / 60), mint);
            else
                text(renderer,
                     32,
                     85,
                     watching     ? "WATCHING / WAIT FOR PLAYERS"
                     : view.ready ? "READY"
                                  : "WAITING",
                     mint);
            const auto manager =
                std::find_if(view.members.begin(), view.members.end(), [&](const auto& member) {
                    return member.id == view.manager_id;
                });
            if (manager != view.members.end())
                text(renderer,
                     32,
                     119,
                     "MANAGER: " + manager->name.substr(0, 16) +
                         (manager->role == net::Role::spectator ? " / SPECTATOR" : " / PLAYER"),
                     muted);
            unsigned row = 0, spectators = 0;
            for (const auto& member : view.members) {
                if (member.role == net::Role::spectator) {
                    ++spectators;
                    continue;
                }
                const float x = 32.F + (row / 4) * 291.F, y = 135.F + (row % 4) * 31.F;
                text(renderer,
                     x,
                     y,
                     (member.id == view.member_id ? "> " : "  ") + member.name.substr(0, 18),
                     ink);
                std::string state =
                    member.connected ? (member.ready ? "READY" : "WAITING") : "RECONNECTING";
                if (view.phase == net::Phase::results) {
                    const auto entries = client::leaderboard(view);
                    const auto entry =
                        std::find_if(entries.begin(), entries.end(), [&](const auto& e) {
                            return e.id == member.player;
                        });
                    if (entry != entries.end())
                        state =
                            number(entry->score) + " / " +
                            (entry->status == client::PlayerStatus::disconnected ? "DISCONNECTED"
                             : entry->status == client::PlayerStatus::out        ? "OUT"
                                                                                 : "WINNER");
                }
                if (member.id == view.manager_id)
                    state += " / MANAGER";
                text(renderer, x + 16, y + 13, state, muted);
                ++row;
            }
            text(renderer,
                 32,
                 278,
                 "PLAYERS " + number(row) + "/" + number(view.player_limit) + " / SPECTATORS " +
                     number(spectators),
                 muted);
            text(renderer,
                 32,
                 294,
                 view.phase == net::Phase::results  ? "MATCH COMPLETE"
                 : preset != modern::Preset::modern ? "START LEVEL " + number(view.room.start_level)
                                                    : "ALL PLAYERS READY TO START",
                 muted);
            for (int i = 0; i < static_cast<int>(room_actions.size()); ++i) {
                const auto choice = room_actions[static_cast<std::size_t>(i)];
                const std::string label = choice == client::RoomAction::leave ? "LEAVE LOBBY"
                                          : choice == client::RoomAction::return_to_lobby
                                              ? "RETURN TO LOBBY"
                                          : view.ready ? "CANCEL READY"
                                                       : "READY";
                const float x = 400.F, y = 278.F + i * 29.F;
                rect(renderer,
                     x,
                     y,
                     208,
                     24,
                     i == room_row ? SDL_Color{26, 77, 70, 255} : SDL_Color{29, 48, 65, 255});
                text(renderer,
                     x + 8,
                     y + 8,
                     std::string(i == room_row ? "> " : "  ") + label,
                     i == room_row ? mint : ink);
            }
        }
        if (view.phase == net::Phase::browsing ||
            (!smoke_lobby_stage.empty() && !smoke_room && smoke_lobby_stage != "error")) {
            rect(renderer, 0, 50, 640, 290, {9, 15, 27, 255});
            std::vector<std::string> rows;
            std::string title;
            auto mode_label = [](modern::Preset mode) {
                return mode == modern::Preset::classic  ? "CLASSIC"
                       : mode == modern::Preset::fusion ? "FUSION"
                                                        : "MODERN";
            };
            if (lobby_page == LobbyPage::choice) {
                title = "MULTIPLAYER";
                rows = {"CREATE LOBBY", "JOIN LOBBY", "BACK"};
            } else if (lobby_page == LobbyPage::create) {
                title = "CREATE LOBBY";
                rows = {"NAME: " + draft.name,
                        "PASSWORD: " + std::string(draft.password.size(), '*'),
                        "MAX PLAYERS: " + number(draft.players),
                        std::string("MODE: ") + mode_label(draft.preset),
                        draft.preset == modern::Preset::modern
                            ? "LEVEL: AUTOMATIC"
                            : "START LEVEL: " + number(draft.start_level),
                        "CREATE",
                        "BACK"};
            } else if (lobby_page == LobbyPage::password) {
                title = room_selection.role == net::Role::spectator ? "WATCH / LOCKED LOBBY"
                                                                    : "JOIN / LOCKED LOBBY";
                rows = {"PASSWORD: " + std::string(password.size(), '*'), "CONFIRM"};
            } else {
                title = "JOIN LOBBY";
                for (const auto& room : view.rooms)
                    rows.push_back(room.name.substr(0, 18) + " / " + mode_label(room.preset) +
                                   " / " + number(room.players) + "/" + number(room.capacity) +
                                   (room.state == net::Phase::waiting ? " OPEN" : " IN MATCH") +
                                   (room.password_required ? " [LOCK]" : ""));
                rows.push_back("REFRESH");
                rows.push_back("BACK");
            }
            text(renderer, 32, 60, title, mint);
            int first = std::max(0, lobby_row - 5);
            for (int i = first; i < static_cast<int>(rows.size()) && i < first + 7; ++i)
                text(renderer,
                     32,
                     92.F + (i - first) * 26.F,
                     std::string(i == lobby_row ? "> " : "  ") + rows[i],
                     i == lobby_row ? mint : ink);
            if (!view.operation_error.empty())
                text(renderer, 32, 327, view.operation_error.substr(0, 74), {255, 174, 93, 255});
        }
        if (connection_failed || view.phase == net::Phase::connecting) {
            rect(renderer, 0, 50, 640, 290, {9, 15, 27, 255});
            text(renderer,
                 32,
                 78,
                 connection_failed ? "SERVER CANNOT BE REACHED" : "CONNECTING TO SERVER...",
                 mint);
            text(renderer, 32, 106, options.host + ":" + number(options.port), muted);
            if (connection_failed) {
                text(renderer, 32, 137, view.detail.substr(0, 70), ink);
                const std::array<const char*, 2> choices{"RETRY CONNECTION", "BACK TO MAIN MENU"};
                for (int i = 0; i < 2; ++i)
                    text(renderer,
                         32,
                         186.F + i * 30.F,
                         std::string(error_row == i ? "> " : "  ") + choices[i],
                         error_row == i ? mint : ink);
            }
        }
        if (overlay && !connection_failed) {
            rect(renderer, 306, 103, 314, 130, {26, 61, 65, 255});
            text(renderer, 318, 114, "INPUT RELEASED / MATCH CONTINUES", mint);
            const std::array<const char*, 3> choices{"RESUME INPUT", "RECONNECT", "LEAVE LOBBY"};
            for (int i = 0; i < 3; ++i)
                text(renderer,
                     318,
                     139.F + i * 24.F,
                     std::string(overlay_selection == i ? "> " : "  ") + choices[i],
                     overlay_selection == i ? mint : ink);
        }
        rect(renderer, 0, 345, 640, 15, {19, 33, 49, 255});
        text(renderer, 8, 349, view.detail.substr(0, 76), muted);
        if (!smoke_lobby_stage.empty() && ++smoke_lobby_frames >= 8)
            running = false;
        if (smoke_connect_error) {
            if (connection_failed && ++connect_error_frames >= 8)
                running = false;
            if (now - started >= 10'000'000'000ULL)
                running = false;
        }
        const bool capture_live = capture_tick && !live_captured &&
                                  view.phase == net::Phase::playing &&
                                  view.server_tick >= capture_tick;
        if (!capture_path.empty() && (capture_live || (!running && !capture_tick))) {
            SDL_Surface* capture = SDL_RenderReadPixels(renderer, nullptr);
            const bool saved = capture && SDL_SaveBMP(capture, capture_path.string().c_str());
            if (!saved)
                std::fprintf(stderr, "Online capture: %s\n", SDL_GetError());
            else if (capture_live) {
                live_captured = true;
                std::printf("ONLINE_CAPTURE tick=%llu role=%s\n",
                            static_cast<unsigned long long>(view.server_tick),
                            watching ? "spectator" : "player");
            }
            if (smoke_connect_error && connection_failed && saved)
                connect_error_captured = true;
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
    if (smoke_connect_error) {
        const bool passed = connect_error_frames >= 8 && connect_error_captured;
        std::printf(
            "CONNECT_ERROR_SMOKE %s frames=%d\n", passed ? "PASS" : "FAIL", connect_error_frames);
        return passed ? 0 : 1;
    }
    return quit_application ? 2 : bot && !terminal ? 1 : 0;
}
#endif
} // namespace
int main(int argc, char** argv) {
    std::optional<std::uint32_t> initial_seed;
    int smoke_frames = 0;
    bool smoke_play = false, smoke_settings = false, force_sticks = false, muted_audio = false;
    std::filesystem::path data_directory, verify_path, capture_path;
    bool smoke_spin_debug = false;
    std::string smoke_screen;
    int smoke_replay_row = -1;
    bool smoke_replay_check = false;
    int mode = 0; // Classic, Fusion, Modern
    unsigned fixture_rows = 1;
    bool garbage_fixture = false;
    std::string online_host;
    std::uint16_t online_port = 27877;
    std::uint64_t online_exit_ticks = 0, online_capture_tick = 0, online_ready_delay_ms = 0;
    bool smoke_multiplayer_menu = false;
    bool smoke_connect_error = false;
    std::string smoke_lobby_stage;
    bool online_bot = false, online_spectate = false, online_create = false;
    std::string online_room, online_name = "Player";
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg = argv[i];
        if (arg == "--connect" && i + 1 < argc)
            online_host = argv[++i];
        else if (arg == "--smoke-connect-error")
            smoke_connect_error = true;
        else if (arg == "--smoke-lobby-stage" && i + 1 < argc) {
            smoke_lobby_stage = argv[++i];
            if (smoke_lobby_stage != "choice" && smoke_lobby_stage != "create" &&
                smoke_lobby_stage != "waiting" && smoke_lobby_stage != "countdown" &&
                smoke_lobby_stage != "results" && smoke_lobby_stage != "error" &&
                smoke_lobby_stage != "browse")
                return 2;
            if (online_host.empty())
                online_host = "127.0.0.1";
        } else if (arg == "--smoke-multiplayer-menu")
            smoke_multiplayer_menu = true;
        else if (arg == "--spectate")
            online_spectate = true;
        else if (arg == "--create")
            online_create = true;
        else if (arg == "--room" && i + 1 < argc)
            online_room = argv[++i];
        else if (arg == "--name" && i + 1 < argc)
            online_name = argv[++i];
        else if (arg == "--online-bot")
            online_bot = true;
        else if ((arg == "--port" || arg == "--online-exit-ticks" ||
                  arg == "--online-capture-tick" || arg == "--online-ready-delay-ms") &&
                 i + 1 < argc) {
            std::uint64_t value = 0;
            const std::string_view number_arg = argv[++i];
            const auto parsed =
                std::from_chars(number_arg.data(), number_arg.data() + number_arg.size(), value);
            if (parsed.ec != std::errc{} || parsed.ptr != number_arg.data() + number_arg.size() ||
                !value || (arg == "--port" && value > 65535))
                return 2;
            if (arg == "--port")
                online_port = static_cast<std::uint16_t>(value);
            else if (arg == "--online-capture-tick")
                online_capture_tick = value;
            else if (arg == "--online-ready-delay-ms")
                online_ready_delay_ms = value;
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
        } else if (arg == "--seed" && i + 1 < argc) {
            std::uint32_t value{};
            const std::string_view number_arg = argv[++i];
            const auto parsed =
                std::from_chars(number_arg.data(), number_arg.data() + number_arg.size(), value);
            if (parsed.ec != std::errc{} || parsed.ptr != number_arg.data() + number_arg.size())
                return 2;
            initial_seed = value;
        } else if (arg == "--verify" && i + 1 < argc)
            verify_path = argv[++i];
        else if (arg == "--capture" && i + 1 < argc)
            capture_path = argv[++i];
        else if (arg == "--smoke-replay-check")
            smoke_replay_check = true;
        else if (arg == "--smoke-replay-row" && i + 1 < argc) {
            const std::string_view value = argv[++i];
            const auto parsed =
                std::from_chars(value.data(), value.data() + value.size(), smoke_replay_row);
            if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size() ||
                smoke_replay_row < 0)
                return 2;
        } else if (arg == "--smoke-screen" && i + 1 < argc) {
            smoke_screen = argv[++i];
            if (smoke_screen != "home" && smoke_screen != "pause" &&
                smoke_screen != "practice-pause" && smoke_screen != "settings" &&
                smoke_screen != "level" && smoke_screen != "replays")
                return 2;
        } else if (arg == "--smoke-settings")
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
                         "[--smoke-replay-row N] [--smoke-replay-check] "
                         "[--smoke-settings] [--smoke-screen "
                         "home|pause|practice-pause|settings|level|replays] "
                         "[--spin-debug] [--capture FILE.bmp] [--smoke-connect-error] "
                         "[--seed N] [--verify FILE] [--connect HOST --port N] "
                         "[--spectate] [--room CODE] [--name NAME] [--create] "
                         "[--online-bot --online-exit-ticks N] [--online-capture-tick N] "
                         "[--online-ready-delay-ms N] [--smoke-multiplayer-menu]\n");
            return 2;
        }
    }
    if (smoke_connect_error && (online_host.empty() || !smoke_lobby_stage.empty() ||
                                online_capture_tick || smoke_multiplayer_menu)) {
        std::fprintf(stderr,
                     "--smoke-connect-error requires --connect and no other online capture mode\n");
        return 2;
    }
#ifndef STACKPULSE_NETWORK
    if (!online_host.empty() || online_bot || online_exit_ticks || online_capture_tick ||
        smoke_multiplayer_menu || smoke_connect_error) {
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
    const auto settings_path = directory / "settings.txt";
    Settings settings;
    settings.load(settings_path);
    if (force_sticks)
        settings.sticks = true;
#ifdef STACKPULSE_NETWORK
    if (smoke_multiplayer_menu) {
        bool quit = false;
        online_menu(renderer,
                    mode == 2 ? modern::Preset::modern : modern::Preset::fusion,
                    directory / "online-host.txt",
                    quit,
                    smoke_frames ? smoke_frames : 8,
                    capture_path);
        audio.shutdown();
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 0;
    }
    if (!online_host.empty()) {
        stackpulse::net::ConnectOptions room_options;
        room_options.host = online_host;
        room_options.port = online_port;
        room_options.preset = mode == 0   ? modern::Preset::classic
                              : mode == 1 ? modern::Preset::fusion
                                          : modern::Preset::modern;
        room_options.role =
            online_spectate ? stackpulse::net::Role::spectator : stackpulse::net::Role::player;
        room_options.name = online_name;
        room_options.room_code = online_room;
        room_options.create_room = online_create;
        room_options.browse_only = !smoke_lobby_stage.empty() || smoke_connect_error;
        const auto result = run_online(renderer,
                                       settings,
                                       audio,
                                       online_host,
                                       online_port,
                                       room_options.preset,
                                       online_bot,
                                       online_exit_ticks,
                                       capture_path,
                                       &room_options,
                                       online_capture_tick,
                                       online_ready_delay_ms,
                                       smoke_lobby_stage,
                                       smoke_connect_error);
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
    bool help = false;
    bool spin_debug = false;
    std::uint8_t spin_debug_rotation = 0;
    Screen screen = Screen::home, settings_return = Screen::home;
    std::uint32_t seed = initial_seed ? *initial_seed : stackpulse::seeds::random();
    std::printf("SOLO_SEED seed=%u\n", static_cast<unsigned>(seed));
    bool seed_used = false;
    game::State state = game::initial_state(seed);
    modern::State modern_state = modern::initial_state(seed);
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
    replay::Replay recording{state, {}}, loaded;
    auto recording_path = rl::new_recording(directory, mode);
    std::vector<rl::Entry> replay_entries;
    int replay_row = 0;
    std::size_t playback_index = 0;
    client::ReplaySeek<game::State> classic_seek;
    client::ReplaySeek<modern::State> modern_seek;
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
    auto playback_size = [&]() {
        return mode ? modern_loaded.inputs.size() : loaded.inputs.size();
    };
    auto finish_playback = [&]() {
        // Keep the loaded library and its selected row intact, including scroll.
        screen = Screen::replays;
        step_once = false;
        release(SDL_GetTicksNS());
        notice = "REPLAY COMPLETE / HASH " + number(current_hash());
    };
    auto seek_playback = [&](std::size_t target) {
        target = std::min(target, playback_size());
        const auto seek_started = SDL_GetTicksNS();
        const bool seeking_during_play = screen == Screen::play;
        audio.set_playing(false);
        const bool ok =
            mode
                ? modern_seek.seek(modern_loaded.inputs,
                                   target,
                                   modern_state,
                                   [](auto& value, auto mask) { return modern::step(value, mask); })
                : classic_seek.seek(loaded.inputs, target, state, [](auto& value, auto mask) {
                      return game::step(value, mask);
                  });
        // Reconstruction time is not playback debt. Paused/end-of-replay time
        // is already excluded by the regular menu clock handling.
        if (seeking_during_play && (target < playback_size() || !ok))
            epoch += SDL_GetTicksNS() - seek_started;
        if (!ok) {
            notice = "REPLAY SEEK FAILED";
            return;
        }
        playback_index = target;
        sync_display();
        timeline.clear();
        release(SDL_GetTicksNS());
        notice = "REPLAY / FRAME " + number(playback_index) + " OF " + number(playback_size());
        if (playback_index == playback_size())
            finish_playback();
    };
    auto seek_relative = [&](int delta) {
        seek_playback(client::replay_seek_target(playback_index, playback_size(), delta));
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
            saves.submit([snapshot = modern_recording, path = recording_path]() {
                std::string error;
                rl::save(path, snapshot, error);
                return error;
            });
        } else {
            saves.submit([snapshot = recording, path = recording_path]() {
                std::string error;
                rl::save(path, snapshot, error);
                return error;
            });
        }
        notice = "SAVING REPLAY";
        return true;
    };
    auto start = [&](bool lab, bool fixture, bool fresh = false) {
        if (!save_recording())
            return;
        if (fresh && seed_used)
            seed = stackpulse::seeds::random(seed);
        seed_used = true;
        recording_path = rl::new_recording(directory, mode);
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
                 : fixture ? (mode ? "SPIN FIXTURE / " + number(fixture_rows) + " LINES"
                                   : "FOUR-LINE FIXTURE")
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
        if (screen != Screen::level || selection != 0)
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
            (code == SDL_SCANCODE_TAB || code == SDL_SCANCODE_UP || code == SDL_SCANCODE_RETURN ||
             code == SDL_SCANCODE_F1 || code == SDL_SCANCODE_F2 || code == SDL_SCANCODE_F3 ||
             code == SDL_SCANCODE_F4 || code == SDL_SCANCODE_F5 || code == SDL_SCANCODE_F6 ||
             code == SDL_SCANCODE_F7 || code == SDL_SCANCODE_F8 || code == SDL_SCANCODE_F9 ||
             code == SDL_SCANCODE_F10 || code == SDL_SCANCODE_F11 || code == SDL_SCANCODE_F12)) {
            notice = "RESERVED MENU / PRACTICE KEY";
            return;
        }
        if (pad && code == SDL_GAMEPAD_BUTTON_RIGHT_STICK) {
            notice = "RESERVED ONLINE TARGET / FEATURE BUTTON";
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
        const int size = screen == Screen::home      ? 7
                         : screen == Screen::pause   ? 7
                         : screen == Screen::level   ? 3
                         : screen == Screen::replays ? static_cast<int>(replay_entries.size()) + 1
                                                     : 18;
        int& row = screen == Screen::settings  ? settings_row
                   : screen == Screen::replays ? replay_row
                                               : selection;
        row = (row + direction + size) % size;
        if (screen == Screen::pause && (!practice || playback)) {
            while (row == 3 || row == 4 || (playback && row == 2))
                row = (row + direction + size) % size;
        }
    };
    auto confirm = [&]() {
        if (screen == Screen::settings) {
            if (settings_row < 16) {
                capture = settings_row;
                notice = "AWAITING NEW BINDING";
            } else if (settings_row == 16) {
                settings.sticks = !settings.sticks;
                save_settings();
            } else
                screen = settings_return;
        } else if (screen == Screen::level) {
            if (selection == 0) {
                settings.start_level = (settings.start_level + 1) % 30;
                save_settings();
            } else if (selection == 1) {
                garbage_fixture = false;
                start(false, false, true);
            } else {
                screen = Screen::home;
                selection = 1;
            }
        } else if (screen == Screen::replays) {
            if (replay_row >= static_cast<int>(replay_entries.size())) {
                screen = Screen::home;
                selection = 3;
                return;
            }
            std::string error;
            const auto& entry = replay_entries[static_cast<std::size_t>(replay_row)];
            const bool modern_replay = entry.path.extension() == ".spmr";
            const bool ok = modern_replay ? mr::load(entry.path, modern_loaded, error)
                                          : replay::load(entry.path, loaded, error);
            if (!ok) {
                notice = "REPLAY: " + error;
                return;
            }
            if (modern_replay) {
                modern_state = modern_loaded.initial;
                modern_seek.reset(modern_loaded.initial);
                mode = modern_state.preset == modern::Preset::fusion ? 1 : 2;
                sync_display();
            } else {
                mode = 0;
                state = loaded.initial;
                classic_seek.reset(loaded.initial);
            }
            practice = false;
            playback = true;
            playback_index = 0;
            spin_debug = false;
            step_once = false;
            screen = Screen::play;
            release(SDL_GetTicksNS());
            notice = "VERIFIED REPLAY / " + entry.timestamp;
            if (playback_size() == 0)
                finish_playback();
        } else if (screen == Screen::home) {
            if (selection == 0) {
                if (save_recording()) {
                    mode = (mode + 1) % 3;
                    notice = "SELECTED " + mode_name();
                }
            } else if (selection == 1 || selection == 2) {
                garbage_fixture = false;
                if (selection == 1 && mode != 2) {
                    screen = Screen::level;
                    selection = 0;
                } else
                    start(selection == 2, false, true);
            } else if (selection == 3) {
                std::string error;
                if (!save_recording())
                    return;
                if (!saves.ready_to_load()) {
                    notice = "REPLAY SAVE IN PROGRESS / RETRY PLAY REPLAY";
                    return;
                }
                replay_entries = rl::list(directory, error);
                replay_row = 0;
                screen = Screen::replays;
                notice = error.empty() ? "SELECT A REPLAY" : "REPLAY: " + error;
            } else if (selection == 4) {
                settings_return = screen;
                screen = Screen::settings;
            } else if (selection == 5) {
#ifdef STACKPULSE_NETWORK
                if (!save_recording())
                    return;
                release(SDL_GetTicksNS());
                bool online_quit = false;
                const auto room =
                    online_menu(renderer,
                                mode == 2 ? modern::Preset::modern : modern::Preset::fusion,
                                directory / "online-host.txt",
                                online_quit);
                const auto online_result = room          ? run_online(renderer,
                                                                      settings,
                                                                      audio,
                                                                      room->host,
                                                                      room->port,
                                                                      room->preset,
                                                                      false,
                                                                      0,
                                                                      {},
                                                                      &*room)
                                           : online_quit ? 2
                                                         : 0;
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
                    seek_playback(0);
                    if (playback_size() == 0)
                        return;
                    screen = Screen::play;
                    release(SDL_GetTicksNS());
                    notice = "REPLAY RESTARTED";
                } else
                    start(practice, false);
            } else if (selection == 2) {
                if (playback)
                    return;
                if (save_recording()) {
                    seed = stackpulse::seeds::random(seed);
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
                screen = playback ? Screen::replays : Screen::home;
                if (!playback)
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
        } else if (screen == Screen::level || screen == Screen::replays) {
            selection = screen == Screen::level ? 1 : 3;
            screen = Screen::home;
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
    if (smoke_screen == "home")
        screen = Screen::home;
    else if (smoke_screen == "settings")
        screen = Screen::settings;
    else if (smoke_screen == "level")
        screen = Screen::level;
    else if (smoke_screen == "replays") {
        std::string error;
        replay_entries = rl::list(directory, error);
        screen = Screen::replays;
    } else if (smoke_screen == "pause" || smoke_screen == "practice-pause") {
        start(smoke_screen == "practice-pause", false);
        screen = Screen::pause;
        selection = 0;
    }
    bool replay_smoke_ok = true;
    std::uint64_t replay_smoke_hash = 0;
    std::vector<std::filesystem::path> replay_smoke_paths;
    if (smoke_replay_row >= 0) {
        if (!smoke_frames)
            smoke_frames = 120;
        std::string error;
        replay_entries = rl::list(directory, error);
        if (!error.empty() || smoke_replay_row >= static_cast<int>(replay_entries.size())) {
            std::fprintf(stderr, "REPLAY_SMOKE FAIL invalid library row\n");
            running = false;
            replay_smoke_ok = false;
        } else {
            for (const auto& entry : replay_entries)
                replay_smoke_paths.push_back(entry.path);
            replay_row = smoke_replay_row;
            screen = Screen::replays;
            confirm();
            replay_smoke_ok = playback;
            if (smoke_replay_check && playback) {
                if (mode) {
                    modern::State expected;
                    replay_smoke_ok = mr::verify(modern_loaded, expected, error);
                    replay_smoke_hash = modern::state_hash(expected);
                } else {
                    game::State expected;
                    replay_smoke_ok = replay::verify(loaded, expected, error);
                    replay_smoke_hash = game::state_hash(expected);
                }
                if (playback_size()) {
                    screen = Screen::pause;
                    seek_playback(playback_size() / 2);
                    replay_smoke_ok = replay_smoke_ok && screen == Screen::pause;
                    seek_relative(-300);
                    selection = 1;
                    confirm(); // Restart from pause through the actual menu action.
                    replay_smoke_ok = replay_smoke_ok && playback_index == 0;
                    seek_playback(playback_size() - 1);
                    screen = Screen::play; // Let the final frame finish naturally.
                }
            }
        }
    } else if (smoke_replay_check) {
        std::fprintf(stderr, "--smoke-replay-check requires --smoke-replay-row\n");
        running = false;
        replay_smoke_ok = false;
    }
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
                    if (playback && (screen == Screen::play || screen == Screen::pause)) {
                        if (code == SDL_SCANCODE_LEFT || code == SDL_SCANCODE_RIGHT) {
                            seek_relative(code == SDL_SCANCODE_LEFT ? -300 : 300);
                            break;
                        }
                        if (code == SDL_SCANCODE_PAGEUP || code == SDL_SCANCODE_PAGEDOWN) {
                            seek_relative(code == SDL_SCANCODE_PAGEUP ? -3600 : 3600);
                            break;
                        }
                        if (code == SDL_SCANCODE_HOME || code == SDL_SCANCODE_END) {
                            seek_playback(code == SDL_SCANCODE_HOME ? 0 : playback_size());
                            break;
                        }
                        if (code == SDL_SCANCODE_SPACE) {
                            pause();
                            break;
                        }
                    }
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
                        help = !help;
                        break;
                    }
                    if (code == SDL_SCANCODE_F2 && !playback) {
                        start(practice, false);
                        break;
                    }
                    if (code == SDL_SCANCODE_F3 && !playback) {
                        if (save_recording()) {
                            seed = stackpulse::seeds::random(seed);
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
                    if (playback && (screen == Screen::play || screen == Screen::pause)) {
                        if (code == SDL_GAMEPAD_BUTTON_DPAD_LEFT ||
                            code == SDL_GAMEPAD_BUTTON_DPAD_RIGHT) {
                            seek_relative(code == SDL_GAMEPAD_BUTTON_DPAD_LEFT ? -300 : 300);
                            break;
                        }
                        if (code == SDL_GAMEPAD_BUTTON_LEFT_SHOULDER ||
                            code == SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER) {
                            seek_relative(code == SDL_GAMEPAD_BUTTON_LEFT_SHOULDER ? -3600 : 3600);
                            break;
                        }
                        if (screen == Screen::play && code == SDL_GAMEPAD_BUTTON_SOUTH) {
                            pause();
                            break;
                        }
                    }
                    if (code == SDL_GAMEPAD_BUTTON_BACK ||
                        (code == SDL_GAMEPAD_BUTTON_EAST &&
                         (screen == Screen::level || screen == Screen::replays)) ||
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
                    mask = mode && modern_state.tick == 0
                               ? static_cast<std::uint32_t>(modern::clockwise)
                               : static_cast<std::uint32_t>(game::down);
                if (playback) {
                    if (playback_index ==
                        (mode ? modern_loaded.inputs.size() : loaded.inputs.size())) {
                        finish_playback();
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
                const bool initial_garbage =
                    mode && modern_state.tick == 0 && !modern_state.incoming.empty();
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
                    if (mode ? modern_events.locked : events.locked)
                        audio.play(ClientAudio::Cue::locked);
                    if (mode && modern_events.inserted)
                        audio.play(ClientAudio::Cue::garbage_hit);
                    if (initial_garbage)
                        audio.play(ClientAudio::Cue::garbage_warning);
                    if (const auto cue =
                            client::clear_cue(events.cleared, mode && modern_events.spin))
                        audio.play(*cue);
                    if (!playback) {
                        if (mode)
                            modern_recording.inputs.push_back(mask);
                        else
                            recording.inputs.push_back(mask);
                        saves.set_dirty(true);
                    } else {
                        if (mode)
                            modern_seek.observe(playback_index, modern_state);
                        else
                            classic_seek.observe(playback_index, state);
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
                    if (playback && playback_index == playback_size())
                        finish_playback();
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
        text(renderer, 20, 15, "STACKPULSE", mint);
        text(renderer, 20, 31, mode_name(), muted);
        if (screen == Screen::home) {
            text(renderer, 36, 77, "BUILD. CLEAR. FIND YOUR RHYTHM.", ink);
            const std::array<std::string, 7> menu{"TOGGLE MODE: " + mode_name(),
                                                  "MARATHON",
                                                  "PRACTICE LAB",
                                                  "REPLAY",
                                                  "SETTINGS",
                                                  "MULTIPLAYER",
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
            if (help) {
                text(renderer, 382, 202, "PRACTICE TOOLS", mint);
                text(renderer, 382, 225, "SEEDS / FRAME STEP");
                text(renderer, 382, 244, mode ? "SPIN / GARBAGE FIXTURES" : "FOUR-LINE FIXTURE");
            }
        } else if (screen == Screen::replays) {
            text(renderer,
                 36,
                 77,
                 "REPLAY HISTORY / " + number(replay_entries.size()) + " OF 100",
                 mint);
            text(renderer, 36, 102, "TIMESTAMP (UTC)               MODE", muted);
            if (replay_entries.empty())
                text(renderer, 52, 119, "NO SAVED REPLAYS YET", muted);
            const int first = std::max(0, replay_row - 6);
            const int replay_count = static_cast<int>(replay_entries.size());
            for (int i = first; i <= replay_count && i < first + 8; ++i) {
                std::string label = "BACK";
                if (i < replay_count) {
                    const auto& entry = replay_entries[static_cast<std::size_t>(i)];
                    label = entry.timestamp + "  " +
                            (entry.mode == 0   ? "CLASSIC"
                             : entry.mode == 1 ? "FUSION"
                             : entry.mode == 2 ? "MODERN"
                                               : "FUSION / MODERN");
                    if (entry.legacy)
                        label += " (LEGACY)";
                }
                text(renderer,
                     40,
                     137.F + (i - first) * 23.F,
                     std::string(replay_row == i ? "> " : "  ") + label,
                     replay_row == i ? mint : ink);
            }
            text(renderer, 40, 326, notice.substr(0, 70), muted);
        } else if (screen == Screen::level) {
            text(renderer, 36, 78, "MARATHON / " + mode_name(), mint);
            text(renderer, 36, 110, "CHOOSE YOUR STARTING LEVEL", ink);
            const std::array<std::string, 3> rows{
                "START LEVEL: " + number(settings.start_level), "START MARATHON", "BACK"};
            for (int i = 0; i < 3; ++i)
                text(renderer,
                     40,
                     156.F + i * 34.F,
                     std::string(selection == i ? "> " : "  ") + rows[i],
                     selection == i ? mint : ink);
        } else if (screen == Screen::settings) {
            text(renderer, 30, 65, "CONTROLS", mint);
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
                rect(renderer, 24, 316, 585, 22, {26, 61, 65, 255});
            text(renderer, 30, 323, "BACK");
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
            if (help) {
                text(renderer, 20, 225, "SEED", muted);
                text(renderer, 20, 240, playback ? "RECORDED" : number(seed));
                text(renderer, 20, 268, "TICK", muted);
                text(renderer, 20, 283, number(state.tick));
            }
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
                text(renderer, 382, 101, "ORIENTATION: " + number(spin_debug_rotation));
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
                int visible_row = 0;
                for (int i = 0; i < 7; ++i) {
                    if (playback && i == 2)
                        continue;
                    if ((i == 3 || i == 4) && (!practice || playback))
                        continue;
                    const float y = 89.F + visible_row++ * 25.F;
                    if (selection == i)
                        rect(renderer, 406, y - 4, 214, 22, {26, 61, 65, 255});
                    text(renderer,
                         414,
                         y,
                         std::string(selection == i ? "> " : "  ") +
                             (playback && i == 6 ? "REPLAY LIST"
                                                 : menu[static_cast<std::size_t>(i)]),
                         selection == i ? mint : ink);
                }
            } else if (help) {
                text(renderer, 414, 66, mode_name(), mint);
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
                }
            }
            if (playback) {
                const auto total = playback_size();
                const float progress =
                    total ? static_cast<float>(playback_index) / static_cast<float>(total) : 1.F;
                text(renderer,
                     390,
                     237,
                     "REPLAY / " + number(playback_index / 60) + "S / " + number(total / 60) + "S",
                     mint);
                rect(renderer, 390, 254, 230, 8, {47, 67, 89, 255});
                rect(renderer, 390, 254, 230 * progress, 8, mint);
            }
            if (help && practice && !mode) {
                int row = 0;
                for (const auto& item : timeline)
                    text(renderer, 320, 310.F + static_cast<float>(row++) * 9.F, item, muted);
            }
        }
        std::string status = notice;
        if (status.size() > 60)
            status.resize(60);
        if (help || screen == Screen::settings)
            text(renderer, 8, 349, status, behind ? SDL_Color{255, 154, 93, 255} : mint);
        if (help)
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
    if (smoke_replay_check) {
        replay_smoke_ok = replay_smoke_ok && screen == Screen::replays &&
                          replay_row == smoke_replay_row && current_hash() == replay_smoke_hash &&
                          replay_entries.size() == replay_smoke_paths.size();
        for (std::size_t i = 0; i < replay_smoke_paths.size(); ++i)
            replay_smoke_ok = replay_smoke_ok && replay_entries[i].path == replay_smoke_paths[i];
        std::printf("REPLAY_SMOKE %s row=%d first=%d frame=%zu hash=%llu\n",
                    replay_smoke_ok ? "PASS" : "FAIL",
                    replay_row,
                    std::max(0, replay_row - 6),
                    playback_index,
                    static_cast<unsigned long long>(current_hash()));
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
    return replay_smoke_ok ? 0 : 1;
}
