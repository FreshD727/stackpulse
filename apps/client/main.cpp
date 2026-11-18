#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include "audio.hpp"
#include <stackpulse/classic.hpp>
#include <stackpulse/input.hpp>
#include <stackpulse/replay.hpp>
#include <algorithm>
#include <array>
#include <charconv>
#include <cstdio>
#include <deque>
#include <filesystem>
#include <fstream>
#include <map>
#include <optional>
#include <string>
#include <string_view>

namespace {
namespace game = stackpulse::classic;
namespace in = stackpulse::input;
namespace replay = stackpulse::replay;
namespace client = stackpulse::client;
constexpr in::Device keyboard = 1ULL << 63;
constexpr std::array<const char*,6> names{"LEFT", "RIGHT", "SOFT DOWN", "ROTATE CW", "ROTATE CCW", "PAUSE"};
constexpr std::array<in::Action,6> actions{in::Action::left,in::Action::right,in::Action::down,in::Action::clockwise,in::Action::counterclockwise,in::Action::pause};
struct Settings {
    std::array<int,6> keys{SDL_SCANCODE_LEFT,SDL_SCANCODE_RIGHT,SDL_SCANCODE_DOWN,SDL_SCANCODE_X,SDL_SCANCODE_Z,SDL_SCANCODE_ESCAPE};
    std::array<int,6> buttons{SDL_GAMEPAD_BUTTON_DPAD_LEFT,SDL_GAMEPAD_BUTTON_DPAD_RIGHT,SDL_GAMEPAD_BUTTON_DPAD_DOWN,SDL_GAMEPAD_BUTTON_SOUTH,SDL_GAMEPAD_BUTTON_WEST,SDL_GAMEPAD_BUTTON_START};
    bool sticks{};
    std::uint32_t start_level{};
    void load(const std::filesystem::path& path) {
        std::ifstream file(path); int version=0, stick=0; Settings candidate;
        if (!(file>>version) || (version!=1 && version!=2)) return;
        for (int& key:candidate.keys) if (!(file>>key) || key<=SDL_SCANCODE_UNKNOWN || key>=SDL_SCANCODE_COUNT) return;
        for (int& button:candidate.buttons) if (!(file>>button) || button<0 || button>=SDL_GAMEPAD_BUTTON_COUNT) return;
        if (!(file>>stick) || (stick!=0 && stick!=1)) return;
        for (std::size_t a=0;a<6;++a) for (std::size_t b=a+1;b<6;++b)
            if (candidate.keys[a]==candidate.keys[b] || candidate.buttons[a]==candidate.buttons[b]) return;
        if (version==2 && (!(file>>candidate.start_level) || candidate.start_level>29)) return;
        candidate.sticks=stick!=0; *this=candidate;
    }
    bool save(const std::filesystem::path& path) const {
        std::ofstream file(path); file<<"2\n";
        for (int value:keys) file<<value<<' ';
        file<<'\n'; for (int value:buttons) file<<value<<' ';
        file<<'\n'<<sticks<<'\n'<<start_level<<'\n'; file.close(); return static_cast<bool>(file);
    }
};
struct Pad { SDL_Gamepad* handle{}; std::array<in::AxisHysteresis,2> axes; };
enum class Screen { home, play, pause, settings };
void color(SDL_Renderer* renderer, SDL_Color c) { SDL_SetRenderDrawColor(renderer,c.r,c.g,c.b,c.a); }
constexpr SDL_Color ink{214,228,244,255}, muted{125,149,178,255}, mint{82,235,185,255};
void rect(SDL_Renderer* r,float x,float y,float w,float h,SDL_Color c) { color(r,c); SDL_FRect box{x,y,w,h}; SDL_RenderFillRect(r,&box); }
void text(SDL_Renderer* r,float x,float y,const std::string& value,SDL_Color c=ink) { color(r,c); SDL_RenderDebugText(r,x,y,value.c_str()); }
constexpr std::array<SDL_Color,7> palette{{{75,204,226,255},{245,204,86,255},{185,125,239,255},{91,211,155,255},{240,112,126,255},{102,150,244,255},{245,165,86,255}}};
void block(SDL_Renderer* r,float x,float y,std::size_t piece,float size=14) {
    const auto c=palette[piece%7]; rect(r,x+1,y+1,size-2,size-2,c);
    rect(r,x+2,y+2,size-4,2,{static_cast<Uint8>(std::min(255,c.r+35)),static_cast<Uint8>(std::min(255,c.g+35)),static_cast<Uint8>(std::min(255,c.b+35)),255});
    rect(r,x+3,y+size-4,2,2,{35,50,69,255});
}
std::string number(std::uint64_t value) { return std::to_string(value); }
}
int main(int argc,char** argv) {
    int smoke_frames=0; bool smoke_play=false, force_sticks=false, muted_audio=false;
    std::filesystem::path data_directory, verify_path;
    for (int i=1;i<argc;++i) {
        const std::string_view arg=argv[i];
        if (arg=="--verify" && i+1<argc) verify_path=argv[++i];
        else if (arg=="--data-dir" && i+1<argc) data_directory=argv[++i];
        else if (arg=="--smoke-play") smoke_play=true;
        else if (arg=="--sticks") force_sticks=true;
        else if (arg=="--mute") muted_audio=true;
        else if (arg=="--smoke-frames" && i+1<argc) {
            const std::string_view value=argv[++i]; const auto result=std::from_chars(value.data(),value.data()+value.size(),smoke_frames);
            if (result.ec!=std::errc{} || result.ptr!=value.data()+value.size() || smoke_frames<1) return 2;
        } else { std::fprintf(stderr,"Usage: stackpulse_client [--sticks] [--mute] [--data-dir PATH] [--smoke-frames N] [--smoke-play] [--verify FILE]\n"); return 2; }
    }
    if (!verify_path.empty()) {
        replay::Replay value; game::State final_state; std::string error;
        if (!replay::load(verify_path,value,error) || !replay::verify(value,final_state,error)) { std::fprintf(stderr,"Replay rejected: %s\n",error.c_str()); return 1; }
        std::printf("Replay verified: frames=%zu tick=%llu hash=%llu\n",value.inputs.size(),static_cast<unsigned long long>(final_state.tick),static_cast<unsigned long long>(game::state_hash(final_state))); return 0;
    }
    if (!SDL_Init(SDL_INIT_VIDEO|SDL_INIT_GAMEPAD)) { std::fprintf(stderr,"%s\n",SDL_GetError()); return 1; }
    SDL_Window* window=nullptr; SDL_Renderer* renderer=nullptr;
    if (!SDL_CreateWindowAndRenderer("Stackpulse / Classic independent v1",1280,720,SDL_WINDOW_RESIZABLE,&window,&renderer)) { SDL_Quit(); return 1; }
    SDL_SetWindowMinimumSize(window,640,360);
    if (!client::configure_presentation(renderer)) {
        std::fprintf(stderr,"Logical presentation: %s\n",SDL_GetError());
        SDL_DestroyRenderer(renderer); SDL_DestroyWindow(window); SDL_Quit(); return 1;
    }
    SDL_SetRenderVSync(renderer,1);
    ClientAudio audio; audio.initialize(muted_audio);
    std::filesystem::path directory=".";
    if (char* pref=SDL_GetPrefPath("Stackpulse","Classic-v1")) { directory=pref; SDL_free(pref); }
    if (!data_directory.empty()) directory=data_directory;
    std::error_code directory_error; std::filesystem::create_directories(directory,directory_error);
    const auto settings_path=directory/"settings.txt", replay_path=directory/"last.spcr";
    Settings settings; settings.load(settings_path); if (force_sticks) settings.sticks=true;
    std::map<SDL_JoystickID,Pad> pads;
    auto open_pad=[&](SDL_JoystickID id) { if (!pads.contains(id)) if (auto* handle=SDL_OpenGamepad(id)) pads.emplace(id,Pad{handle,{}}); };
    int count=0; auto* ids=SDL_GetGamepads(&count); for (int i=0;i<count;++i) open_pad(ids[i]); SDL_free(ids);
    in::Adapter adapter; in::Timestamp boundary=0, epoch=SDL_GetTicksNS(), ticks=0;
    auto tick_boundary=[&](in::Timestamp index) { return epoch+(index/60)*1'000'000'000+(index%60)*1'000'000'000/60; };
    std::uint64_t late=0, overload=0; int frames=0, selection=0, settings_row=0, capture=-1;
    bool running=true, focused=true, practice=false, playback=false, step_once=false, dirty=false;
    Screen screen=Screen::home, settings_return=Screen::home;
    game::State state=game::initial_state(1); std::uint32_t seed=1;
    replay::Replay recording{state,{}}, loaded; std::size_t playback_index=0;
    std::string notice="READY / OFFLINE"; std::deque<std::string> timeline;
    auto release=[&](in::Timestamp when) { adapter.release_all(std::max(when,boundary+1)); for (auto& [id,pad]:pads) { (void)id; for (auto& axis:pad.axes) axis.reset(); } };
    auto save_recording=[&]() {
        if (!dirty || playback || smoke_frames>0) return true;
        std::string error;
        if (replay::save(replay_path,recording,error)) { dirty=false; notice="REPLAY SAVED: last.spcr"; }
        else { notice="SAVE FAILED: "+error; std::fprintf(stderr,"Replay save: %s\n",error.c_str()); return false; }
        return true;
    };
    auto start=[&](bool lab,bool fixture) {
        if (!save_recording()) return;
        practice=lab; playback=false; step_once=false; state=fixture?game::clear_fixture(seed):game::initial_state(seed,settings.start_level);
        recording={state,{}}; dirty=false; screen=Screen::play; timeline.clear(); release(SDL_GetTicksNS()); notice=fixture?"FOUR-LINE FIXTURE / HOLD DOWN":"NEW RUN / SEED "+number(seed);
    };
    auto push=[&](in::Transition value,bool repeat=false) {
        if (repeat) return; value.source_timestamp=value.timestamp;
        if (value.timestamp<=boundary) { value.timestamp=boundary+1; ++late; }
        adapter.push(value);
    };
    auto map_action=[&](int code,bool pad)->std::optional<in::Action> {
        const auto& bindings=pad?settings.buttons:settings.keys;
        for (std::size_t i=0;i<bindings.size();++i) if (bindings[i]==code) return actions[i];
        return {};
    };
    auto save_settings=[&]() { notice=(smoke_frames>0 || settings.save(settings_path))?"SETTINGS SAVED":"SETTINGS SAVE FAILED"; };
    auto adjust_level=[&](int code,bool pad) {
        if (screen!=Screen::settings || settings_row!=13) return;
        const auto next=client::adjusted_start_level(settings.start_level,client::level_direction(code,pad));
        if (next!=settings.start_level) { settings.start_level=next; save_settings(); }
    };
    auto remap=[&](int code,bool pad) {
        const bool wants_pad=capture>=6; if (pad!=wants_pad) return;
        auto& values=pad?settings.buttons:settings.keys; const auto slot=static_cast<std::size_t>(capture%6);
        if (!pad && (code==SDL_SCANCODE_UP || code==SDL_SCANCODE_RETURN || code==SDL_SCANCODE_F1 || code==SDL_SCANCODE_F2 || code==SDL_SCANCODE_F3 || code==SDL_SCANCODE_F4 || code==SDL_SCANCODE_F5 || code==SDL_SCANCODE_F6)) { notice="RESERVED MENU / PRACTICE KEY"; return; }
        if (pad && slot==5 && (code==SDL_GAMEPAD_BUTTON_SOUTH || code==SDL_GAMEPAD_BUTTON_DPAD_UP || code==SDL_GAMEPAD_BUTTON_DPAD_DOWN || code==SDL_GAMEPAD_BUTTON_BACK)) { notice="RESERVED MENU KEY FOR PAUSE"; return; }
        if (!pad && slot==5 && (code==SDL_SCANCODE_DOWN || code==SDL_SCANCODE_W || code==SDL_SCANCODE_S)) { notice="RESERVED MENU KEY FOR PAUSE"; return; }
        for (std::size_t i=0;i<values.size();++i) if (i!=slot && values[i]==code) { notice="CONFLICT: ALREADY BOUND TO "+std::string(names[i]); return; }
        values[slot]=code; capture=-1; save_settings();
    };
    auto navigate=[&](int direction) { const int size=screen==Screen::home?5:screen==Screen::pause?7:15; int& row=screen==Screen::settings?settings_row:selection; row=(row+direction+size)%size; };
    auto confirm=[&]() {
        if (screen==Screen::settings) {
            if (settings_row<12) { capture=settings_row; notice="PRESS A NEW BINDING / ESC CANCELS"; }
            else if (settings_row==12) { settings.sticks=!settings.sticks; save_settings(); }
            else if (settings_row==13) { settings.start_level=(settings.start_level+1)%30; save_settings(); }
            else screen=settings_return;
        } else if (screen==Screen::home) {
            if (selection==0 || selection==1) start(selection==1,false);
            else if (selection==2) {
                std::string error; if (!save_recording()) return;
                if (replay::load(replay_path,loaded,error)) { state=loaded.initial; practice=false; playback=true; playback_index=0; screen=Screen::play; release(SDL_GetTicksNS()); notice="VERIFIED REPLAY / last.spcr"; }
                else notice="REPLAY: "+error;
            } else if (selection==3) { settings_return=screen; screen=Screen::settings; }
            else running=false;
        } else if (screen==Screen::pause) {
            if (selection==0) { screen=Screen::play; release(SDL_GetTicksNS()); }
            else if (selection==1) { if (playback) { state=loaded.initial; playback_index=0; screen=Screen::play; release(SDL_GetTicksNS()); notice="REPLAY RESTARTED"; } else start(practice,false); }
            else if (selection==2) { if (save_recording()) { ++seed; start(practice,false); } }
            else if (selection==3) { if (practice && !playback) { screen=Screen::play; step_once=true; } else notice="FRAME STEP IS A PRACTICE TOOL"; }
            else if (selection==4) { if (practice && !playback) start(true,true); else notice="FIXTURE IS A PRACTICE TOOL"; }
            else if (selection==5) { settings_return=screen; screen=Screen::settings; }
            else { if (!save_recording()) return; screen=Screen::home; selection=0; release(SDL_GetTicksNS()); }
        }
    };
    auto pause=[&]() { if (screen==Screen::play) { screen=Screen::pause; selection=0; release(SDL_GetTicksNS()); save_recording(); } else if (screen==Screen::pause) { screen=Screen::play; release(SDL_GetTicksNS()); } else if (screen==Screen::settings) { capture=-1; screen=settings_return; } };
    if (smoke_play) start(true,true);
    std::printf("Stackpulse Classic independent provisional v1; prefs=%s\n",directory.string().c_str());
    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            switch (event.type) {
            case SDL_EVENT_QUIT: running=false; break;
            case SDL_EVENT_WINDOW_FOCUS_LOST: focused=false; release(event.common.timestamp); if (screen==Screen::play) { screen=Screen::pause; selection=0; notice="FOCUS LOST / INPUT RELEASED"; } break;
            case SDL_EVENT_WINDOW_FOCUS_GAINED: focused=true; break;
            case SDL_EVENT_GAMEPAD_ADDED: open_pad(event.gdevice.which); notice="CONTROLLER CONNECTED"; break;
            case SDL_EVENT_GAMEPAD_REMOVED:
                adapter.release_device(std::max(event.gdevice.timestamp,boundary+1),event.gdevice.which);
                if (auto found=pads.find(event.gdevice.which);found!=pads.end()) { SDL_CloseGamepad(found->second.handle); pads.erase(found); }
                if (screen==Screen::play) { screen=Screen::pause; selection=0; } notice="CONTROLLER REMOVED / PAUSED"; break;
            case SDL_EVENT_KEYBOARD_REMOVED: adapter.release_device(std::max(event.kdevice.timestamp,boundary+1),keyboard|event.kdevice.which); break;
            case SDL_EVENT_KEY_DOWN: case SDL_EVENT_KEY_UP: {
                if (!focused || event.key.repeat) break;
                const auto code=event.key.scancode;
                if (event.key.down && capture>=0) { if (code==SDL_SCANCODE_ESCAPE) { capture=-1; notice="REMAP CANCELLED"; } else remap(code,false); break; }
                if (event.key.down) {
                    if (code==SDL_SCANCODE_ESCAPE || (map_action(code,false)==in::Action::pause && (screen==Screen::play || (screen==Screen::pause && code!=SDL_SCANCODE_UP && code!=SDL_SCANCODE_DOWN && code!=SDL_SCANCODE_W && code!=SDL_SCANCODE_S && code!=SDL_SCANCODE_RETURN)))) { pause(); break; }
                    if (screen!=Screen::play) {
                        if (code==SDL_SCANCODE_UP || code==SDL_SCANCODE_W) navigate(-1);
                        else if (code==SDL_SCANCODE_DOWN || code==SDL_SCANCODE_S) navigate(1);
                        else if (code==SDL_SCANCODE_LEFT || code==SDL_SCANCODE_RIGHT) adjust_level(code,false);
                        else if (code==SDL_SCANCODE_RETURN) confirm();
                        else if (practice && screen==Screen::pause && code==SDL_SCANCODE_F4) { screen=Screen::play; step_once=true; }
                        break;
                    }
                    if (code==SDL_SCANCODE_F1) { pause(); break; }
                    if (code==SDL_SCANCODE_F2 && !playback) { start(practice,false); break; }
                    if (code==SDL_SCANCODE_F3 && !playback) { if (save_recording()) { ++seed; start(practice,false); } break; }
                    if (code==SDL_SCANCODE_F4 && practice && !playback) { step_once=true; break; }
                    if (code==SDL_SCANCODE_F5 && practice && !playback) { start(true,true); break; }
                    if (code==SDL_SCANCODE_F6) { save_recording(); break; }
                }
                if (screen==Screen::play && !playback) if (auto action=map_action(code,false)) push({event.key.timestamp,keyboard|event.key.which,static_cast<std::uint32_t>(code),*action,event.key.down});
                break;
            }
            case SDL_EVENT_GAMEPAD_BUTTON_DOWN: case SDL_EVENT_GAMEPAD_BUTTON_UP: {
                if (!focused || !pads.contains(event.gbutton.which)) break;
                const int code=event.gbutton.button;
                if (event.gbutton.down && capture>=0) { if (code==SDL_GAMEPAD_BUTTON_BACK) { capture=-1; notice="REMAP CANCELLED"; } else remap(code,true); break; }
                if (event.gbutton.down) {
                    if (code==SDL_GAMEPAD_BUTTON_BACK || (map_action(code,true)==in::Action::pause && (screen==Screen::play || (screen==Screen::pause && code!=SDL_GAMEPAD_BUTTON_SOUTH && code!=SDL_GAMEPAD_BUTTON_DPAD_UP && code!=SDL_GAMEPAD_BUTTON_DPAD_DOWN)))) { pause(); break; }
                    if (screen!=Screen::play) {
                        if (code==SDL_GAMEPAD_BUTTON_DPAD_UP) navigate(-1);
                        else if (code==SDL_GAMEPAD_BUTTON_DPAD_DOWN) navigate(1);
                        else if (code==SDL_GAMEPAD_BUTTON_DPAD_LEFT || code==SDL_GAMEPAD_BUTTON_DPAD_RIGHT) adjust_level(code,true);
                        else if (code==SDL_GAMEPAD_BUTTON_SOUTH) confirm();
                        break;
                    }
                }
                if (screen==Screen::play && !playback) if (auto action=map_action(code,true)) push({event.gbutton.timestamp,event.gbutton.which,static_cast<std::uint32_t>(code),*action,event.gbutton.down});
                break;
            }
            case SDL_EVENT_GAMEPAD_AXIS_MOTION:
                if (screen==Screen::play && !playback && focused && settings.sticks && event.gaxis.axis<=SDL_GAMEPAD_AXIS_LEFTY) if (auto found=pads.find(event.gaxis.which);found!=pads.end()) {
                    const int direction=found->second.axes[event.gaxis.axis].update(static_cast<float>(event.gaxis.value)/32768.F);
                    if (event.gaxis.axis==SDL_GAMEPAD_AXIS_LEFTX) { push({event.gaxis.timestamp,event.gaxis.which,100,in::Action::left,direction==-1}); push({event.gaxis.timestamp,event.gaxis.which,101,in::Action::right,direction==1}); }
                    else push({event.gaxis.timestamp,event.gaxis.which,102,in::Action::down,direction==1});
                }
                break;
            default: break;
            }
        }
        audio.set_playing(screen==Screen::play);
        const auto now=SDL_GetTicksNS(); int catchup=0;
        while (tick_boundary(ticks+1)<=now && catchup<8) {
            ++catchup; boundary=tick_boundary(++ticks); const auto sample=adapter.sample(boundary);
            for (const auto& transition:sample.transitions) {
                std::string label=number(state.tick)+" "+(transition.down?"+":"-");
                for (std::size_t i=0;i<actions.size();++i) if (transition.action==actions[i]) label+=names[i];
                if (transition.synthetic) label+=" CLEAR";
                timeline.push_front(label); if (timeline.size()>4) timeline.pop_back();
            }
            if (screen==Screen::play && (!state.game_over || playback)) {
                std::uint32_t mask=0;
                for (std::size_t i=0;i<5;++i) if (sample.held[static_cast<std::size_t>(actions[i])]) mask|=1U<<i;
                if (smoke_play && !playback) mask=game::down;
                if (playback) {
                    if (playback_index==loaded.inputs.size()) { screen=Screen::pause; selection=0; notice="REPLAY COMPLETE / HASH "+number(game::state_hash(state)); continue; }
                    mask=loaded.inputs[playback_index++];
                }
                if (!playback && recording.inputs.size()>=replay::max_frames) { screen=Screen::pause; selection=1; if (save_recording()) notice="RECORDING LIMIT / RUN PAUSED"; continue; }
                game::Events events;
                if (!game::step(state,mask,&events)) { screen=Screen::pause; notice="INVALID STATE / SIMULATION STOPPED"; }
                else {
                    if (events.shifted) audio.play(ClientAudio::Cue::shift);
                    if (events.soft_dropped) audio.play(ClientAudio::Cue::soft_drop);
                    if (events.rotated) audio.play(ClientAudio::Cue::rotate);
                    if (const auto cue=client::clear_cue(events.cleared)) audio.play(*cue);
                    if (!playback) { recording.inputs.push_back(mask); dirty=true; }
                    if (events.cleared) notice=number(events.cleared)+" LINES CLEARED";
                    if (state.game_over && !playback) { notice="GAME OVER / RESTART FROM PAUSE"; save_recording(); screen=Screen::pause; selection=1; }
                }
                if (step_once) { step_once=false; screen=Screen::pause; selection=3; }
            }
        }
        audio.set_playing(screen==Screen::play);
        const bool behind=tick_boundary(ticks+1)<=now; if (behind) ++overload;
        color(renderer,{9,15,27,255}); SDL_RenderClear(renderer);
        for (int line=0;line<12;++line) rect(renderer,0,static_cast<float>(line*32),640,1,{13,23,38,255});
        text(renderer,20,15,"STACKPULSE",mint); text(renderer,20,31,"CLASSIC-INSPIRED / APPROXIMATION",muted);
        text(renderer,368,15,"PROVISIONAL RULES / 60 Hz",muted);
        text(renderer,368,31,"NO REFERENCE FIDELITY CLAIM",muted);
        rect(renderer,18,50,604,1,{48,66,88,255});
        if (screen==Screen::home) {
            text(renderer,36,77,"BUILD. CLEAR. FIND YOUR RHYTHM.",ink);
            constexpr std::array<const char*,5> menu{"CLASSIC MARATHON","PRACTICE LAB","PLAY LAST REPLAY","SETTINGS / CONTROLS","QUIT"};
            for (int i=0;i<5;++i) { if (selection==i) rect(renderer,28,108.F+i*33.F,320,28,{26,61,65,255}); text(renderer,40,118.F+i*33.F,std::string(selection==i?"> ":"  ")+menu[static_cast<std::size_t>(i)],selection==i?mint:ink); }
            text(renderer,382,119,"ONE NEXT PIECE",mint); text(renderer,382,142,"NO HOLD / GHOST"); text(renderer,382,161,"NO HARD DROP / KICKS");
            text(renderer,382,202,"PRACTICE TOOLS",mint); text(renderer,382,225,"SEEDS / FRAME STEP"); text(renderer,382,244,"FOUR-LINE FIXTURE");
            text(renderer,36,294,"ARROWS OR W/S + ENTER / D-PAD + SOUTH",muted);
        } else if (screen==Screen::settings) {
            text(renderer,30,65,"CONTROLS / SELECT TO REMAP",mint);
            for (int i=0;i<12;++i) {
                const bool pad=i>=6; const auto slot=static_cast<std::size_t>(i%6); const float x=pad?330.F:30.F,y=92.F+static_cast<float>(i%6)*28.F;
                if (settings_row==i) rect(renderer,x-6,y-4,278,23,{26,61,65,255});
                const char* value=pad?SDL_GetGamepadStringForButton(static_cast<SDL_GamepadButton>(settings.buttons[slot])):SDL_GetScancodeName(static_cast<SDL_Scancode>(settings.keys[slot]));
                text(renderer,x,y,std::string(pad?"PAD ":"KEY ")+names[slot],settings_row==i?mint:ink);
                text(renderer,x+144,y,capture==i?"[PRESS...]":value?value:"?",muted);
            }
            if (settings_row==12) rect(renderer,24,262,585,22,{26,61,65,255});
            text(renderer,30,269,std::string("OPTIONAL LEFT STICK: ")+(settings.sticks?"ON (.55/.35 HYSTERESIS)":"OFF"));
            if (settings_row==13) rect(renderer,24,290,585,22,{26,61,65,255});
            text(renderer,30,297,"START LEVEL: "+number(settings.start_level)+" / LEFT - RIGHT + / ENTER CYCLES");
            if (settings_row==14) rect(renderer,24,316,585,22,{26,61,65,255});
            text(renderer,30,323,"BACK / ESC OR PAD BACK");
        } else {
            constexpr float bx=158,by=61,cell=14;
            rect(renderer,bx-3,by-3,146,286,{47,67,89,255}); rect(renderer,bx,by,140,280,{12,22,35,255});
            for (int y=0;y<20;++y) for (int x=0;x<10;++x) {
                rect(renderer,bx+x*cell,by+y*cell,13,13,{17,29,44,255});
                const auto value=state.board[static_cast<std::size_t>(y*10+x)]; if (value) block(renderer,bx+x*cell,by+y*cell,value-1);
            }
            if (!state.game_over && state.entry_timer==0) for (const auto cell_position:game::cells(state.active)) if (cell_position.y>=0) block(renderer,bx+cell_position.x*cell,by+cell_position.y*cell,static_cast<std::size_t>(state.active.piece));
            text(renderer,20,68,playback?"REPLAY":practice?"PRACTICE":"MARATHON",mint);
            text(renderer,20,96,"SCORE",muted); text(renderer,20,111,number(state.score));
            text(renderer,20,139,"LINES",muted); text(renderer,20,154,number(state.lines));
            text(renderer,20,182,"LEVEL",muted); text(renderer,20,197,number(state.level));
            text(renderer,20,225,"SEED",muted); text(renderer,20,240,playback?"RECORDED":number(seed));
            text(renderer,20,268,"TICK",muted); text(renderer,20,283,number(state.tick));
            text(renderer,320,66,"NEXT",mint);
            game::Active preview{state.next,game::spawn_rotation(state.next),0,0}; for (auto c:game::cells(preview)) block(renderer,320+c.x*14.F,85+c.y*14.F,static_cast<std::size_t>(state.next));
            if (screen==Screen::pause) {
                text(renderer,414,65,state.game_over?"GAME OVER":"PAUSED",mint);
                constexpr std::array<const char*,7> menu{"RESUME","RESTART SEED","NEW SEED","STEP ONE TICK","CLEAR FIXTURE","SETTINGS","MAIN MENU"};
                for (int i=0;i<7;++i) { const float y=89.F+i*25.F; if (selection==i) rect(renderer,406,y-4,214,22,{26,61,65,255}); text(renderer,414,y,std::string(selection==i?"> ":"  ")+menu[static_cast<std::size_t>(i)],selection==i?mint:ink); }
            } else {
                text(renderer,320,137,"CONTROLS",mint);
                for (std::size_t i=0;i<6;++i) text(renderer,320,157.F+static_cast<float>(i)*16.F,std::string(names[i])+": "+SDL_GetScancodeName(static_cast<SDL_Scancode>(settings.keys[i])),muted);
                text(renderer,320,264,"F2 RESTART  F3 NEW SEED",muted);
                text(renderer,320,280,practice?"F4 STEP  F5 CLEAR FIXTURE":"ESC / START: PAUSE",muted);
                text(renderer,320,296,"F6 SAVE REPLAY",muted);
            }
            if (practice) { int row=0; for (const auto& item:timeline) text(renderer,320,310.F+static_cast<float>(row++)*9.F,item,muted); }
        }
        rect(renderer,0,345,640,15,{19,33,49,255});
        std::string status=notice; if (status.size()>60) status.resize(60);
        text(renderer,8,349,status,behind?SDL_Color{255,154,93,255}:mint);
        text(renderer,500,349,behind?"OVERLOAD "+number(overload):"PADS "+number(pads.size()),muted);
        SDL_RenderPresent(renderer); ++frames;
        if (smoke_frames>0 && frames>=smoke_frames) running=false;
        SDL_Delay(1);
    }
    save_recording();
    std::printf("Client finished: frames=%d ticks=%llu game_ticks=%llu lines=%u late=%llu overload=%llu hash=%llu\n",frames,static_cast<unsigned long long>(ticks),static_cast<unsigned long long>(state.tick),state.lines,static_cast<unsigned long long>(late),static_cast<unsigned long long>(overload),static_cast<unsigned long long>(game::state_hash(state)));
    for (auto& [id,pad]:pads) { (void)id; SDL_CloseGamepad(pad.handle); }
    audio.shutdown();
    SDL_DestroyRenderer(renderer); SDL_DestroyWindow(window); SDL_Quit(); return 0;
}
