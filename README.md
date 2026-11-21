# Stackpulse - offline play and authoritative duels

Native C++20/SDL3 falling-block play with Classic, Fusion and Modern offline Marathon, Practice Lab, persistent input settings, replay recording/playback and an SDL-free replay verifier. **Classic-inspired provisional rules** are implemented; NES/reference fidelity is unverified. Classic rotation has no kicks; independent uniform piece draws, constant entry delays and several timing details remain provisional. See the [behavior and fidelity ledger](specs/classic-reference.md).

On Windows install Visual Studio 2026 with Desktop development with C++, a CMake version supporting its Visual Studio 18 generator, and Git. The tested CMake version is 4.5.0-rc2; CMakeLists.txt's 3.25 floor applies to older supported generators, not the VS2026 preset. From the repository:

```powershell
cmake --preset windows-debug
cmake --build --preset windows-debug -- /m:1 /p:BuildInParallel=false
ctest --preset windows-debug
.\out\build\windows-debug\Debug\stackpulse_client.exe
```

Choose the MODE row (Enter/confirm cycles Classic, Fusion, Modern), then Marathon or Practice from the home menu. Both Fusion and Modern support C hold; only Modern supports Space hard drop. Fusion uses five previews, Classic uniform RNG/gravity/start-level transitions and wall-charged 16/6 handling; every piece locks on a blocked downward attempt unless the current T position permits a spin, which gets a 30-tick delay with fifteen resets. Fusion spawns each shape with its top occupied cell in row zero; T/J/L spawn and preview flipped like Classic. Modern uses five previews, a ghost, 10/3 handling and a 30-tick lock delay with fifteen resets. Default play keys are Left/Right/Down, X clockwise, Z counterclockwise and Escape pause. Menus use Up/Down or W/S and Enter; gamepad navigation and remapping are available. Practice provides restart with the same seed, a new seed, frame stepping, an authored four-line clear fixture and an event timeline. Fusion/Modern Practice uses F7/F8/F9 for playable spin single/double/triple fixtures (press CW then wait to lock), and F10 for an initial-state garbage demo. Practice frame step records the accepted input; fixture/demo restarts record the full initial state. Settings persist keyboard/gamepad bindings, start level and optional left-stick input. Full controls, persistence locations and diagnostics are in the [client guide](apps/client/README.md).

Recordings save the complete initial state and accepted tick inputs. The home menu can load the last replay for playback. The independent verifier accepts an explicit file path:

```powershell
.\out\build\windows-debug\Debug\stackpulse_replay_verify.exe C:\path\to\last.spcr
.\out\build\windows-debug\Debug\stackpulse_diagnostic.exe --smoke-frames 5
.\out\build\windows-debug\Debug\stackpulse_headless.exe
```

The implemented `SPCR` v1 replay has embedded provisional rules, a complete initial state, periodic/final state hashes and whole-file CRC. Embedded rules/state version 3 corrects Classic transitions, two-state I/S/Z rotations and blocked-shift DAS; older recordings reject as incompatible. The client has original synthesized movement/rotation and distinct line-clear effects (`--mute` disables audio). SPCR is separate from the unimplemented future `SPR1` replay draft; it has no network events or snapshot seeking. See [replay formats](specs/replay-v1.md). The retained diagnostic measures input bindings and timestamps; its old cursor/headless example remains a separate M0 determinism check.

Visual Studio 2022 users can override the generator with `cmake --preset windows-debug -G "Visual Studio 17 2022"` in a fresh build directory. Linux uses `linux-debug` presets with Clang, Ninja and SDL's desktop development prerequisites (see CI); local Linux execution is pending. First configuration fetches exact SDL 3.2.28, Catch2 3.8.1 and ENet 1.3.18 source commits. Cached sources permit subsequent offline builds. Set `STACKPULSE_BUILD_NETWORK=OFF` to omit ENet and online play.

Build the independent core, replay library and CLI tools without fetching dependencies:

```powershell
cmake -S . -B out/core -G "Visual Studio 18 2026" -A x64 -DSTACKPULSE_BUILD_CLIENT=OFF -DSTACKPULSE_BUILD_TESTS=OFF -DSTACKPULSE_BUILD_NETWORK=OFF -DFETCHCONTENT_FULLY_DISCONNECTED=ON
cmake --build out/core --config Release
```

On Linux use Ninja instead of the Visual Studio generator. `headless` is the equivalent Ninja preset when a C++20 compiler is already available. `STACKPULSE_FETCH_DEPENDENCIES=OFF` supports installed package configs, including a vcpkg toolchain; that alternative path remains untested. Exact tested dependency revisions and licenses are recorded in CMakeLists.txt and THIRD_PARTY_NOTICES.md.

See [M1 validation and outstanding gates](docs/M1_VALIDATION.md), [historical M0 validation](docs/M0_VALIDATION.md), [design](docs/GAME_DESIGN.md), and [work plan](docs/DEVELOPMENT_PLAN.md). See [M2 validation](docs/M2_VALIDATION.md) for offline mechanics and [historical M3 validation](docs/M3_VALIDATION.md) for the original duel. [M4 validation](docs/M4_VALIDATION.md) covers 2–8-player rooms, spectators, cross-platform determinism and release packaging. Public release services remain future work. No ROM is accessed by the build/tests; no reference trace or physical controller/rolling fidelity approval is claimed.

For local multiplayer start the dedicated server, then run clients in separate terminals. Enter or gamepad South readies each player; all players ready starts a three-second countdown once at least two players have joined. Fusion is the server default; the client preset must match.

```powershell
.\out\build\windows-debug\Debug\stackpulse_server.exe --mode fusion
.\out\build\windows-debug\Debug\stackpulse_client.exe --connect 127.0.0.1 --mode fusion
```

Home Multiplayer opens a controller/keyboard connection menu for server, port, room code, role and display name. Join the default room without a code, create a private room and share its displayed code, or watch as a spectator. CLI equivalents are `--connect HOST --room CODE --create --spectate --name NAME`; choose `--create` or `--room CODE` for the desired action. The server defaults to eight players, sixteen spectators and up to eight rooms, with `--players 2..8 --spectators 0..16 --rooms 1..8` to reduce limits. Late joins during a match spectate. The connected room manager can return everyone to the lobby after results for a rematch.

Use `--port 27877` on both programs to select a port. For LAN play the server explicitly opts into a LAN bind with `--bind 192.168.1.10`; clients use that address. The default listener stays on loopback. F9 reconnects, Escape/Start opens an input-release overlay while the match continues, and F10/Back leaves. Tab/controller right-stick click cycles the target for players or featured board for spectators. Online replay saving is disabled because an offline input-only recording cannot reproduce authoritative opponent attacks. See the [client guide](apps/client/README.md) and [implemented SPD3 revision 2 protocol](src/protocol/README.md). Revision 1 clients reject rather than silently joining under incompatible messages.

A server build uses no SDL: configure with `STACKPULSE_BUILD_CLIENT=OFF`, or use `cmake --preset server` and `cmake --build --preset server` on a Ninja/C++20 environment. ENet remains its only external runtime dependency. The `headless` preset continues to build dependency-free offline tools.

For a distributable Windows build, build the Release configuration and run the allowlisted [packaging tool](tools/package_release.py) as described in [PACKAGING.md](docs/PACKAGING.md). The ZIP includes the required app-local Visual C++ runtime and dependency licenses, with SHA-256 file hashes. `stackpulse_determinism_matrix` and [its comparison runner](tools/determinism_matrix.py) reproduce the Windows/Linux trace checks. [room_soak.py](tools/room_soak.py) runs the actual-UDP 8-player/16-spectator capacity harness for a measured wall-clock interval.

Issue-2 gameplay, compatibility and validation are recorded in [the resolution](docs/issues/issue-2-resolution.md).

Issue-1 fixes and validation are recorded in [the resolution](docs/issues/issue-1-resolution.md). See [formatting and opt-in pre-commit checks](docs/FORMATTING.md).

Fusion/Modern recordings use separate `SPMR` v5 files (older v1/v2/v3/v4 recordings reject), with full initial state and exact embedded preset/table identity; Classic `SPCR` playback is preserved. Verify them with `stackpulse_modern_replay_verify.exe FILE` or client `--verify FILE`. The native home replay action reads the latest compatible recording. Neither codec claims future SPR1 network-event/seek support. `--mode fusion|modern` selects a mode at startup; `--capture FILE.bmp --smoke-frames 5` supports automated renderer inspection.
