# Stackpulse - M1 provisional Classic vertical slice

Native C++20/SDL3 falling-block play with offline Marathon, Practice Lab, persistent input settings, replay recording/playback and an SDL-free replay verifier. **Classic-inspired provisional rules** are implemented; NES/reference fidelity is unverified. Rotation has no kicks; independent uniform piece draws, constant entry delays and several timing details remain provisional. See the [behavior and fidelity ledger](specs/classic-reference.md).

On Windows install Visual Studio 2026 with Desktop development with C++, a CMake version supporting its Visual Studio 18 generator, and Git. The tested CMake version is 4.5.0-rc2; CMakeLists.txt's 3.25 floor applies to older supported generators, not the VS2026 preset. From the repository:

```powershell
cmake --preset windows-debug
cmake --build --preset windows-debug --parallel 4
ctest --preset windows-debug
.\out\build\windows-debug\Debug\stackpulse_client.exe
```

Choose Marathon or Practice from the home menu. Default play keys are Left/Right/Down, X clockwise, Z counterclockwise and Escape pause. Menus use Up/Down or W/S and Enter; gamepad navigation and remapping are available. Practice provides restart with the same seed, a new seed, frame stepping, an authored four-line clear fixture and an event timeline. Settings persist keyboard/gamepad bindings, start level and optional left-stick input. Full controls, persistence locations and diagnostics are in the [client guide](apps/client/README.md).

Recordings save the complete initial state and accepted tick inputs. The home menu can load the last replay for playback. The independent verifier accepts an explicit file path:

```powershell
.\out\build\windows-debug\Debug\stackpulse_replay_verify.exe C:\path\to\last.spcr
.\out\build\windows-debug\Debug\stackpulse_diagnostic.exe --smoke-frames 5
.\out\build\windows-debug\Debug\stackpulse_headless.exe
```

The implemented `SPCR` v1 replay has embedded provisional rules, a complete initial state, periodic/final state hashes and whole-file CRC. Embedded rules/state version 3 corrects Classic transitions, two-state I/S/Z rotations and blocked-shift DAS; older recordings reject as incompatible. The client has original synthesized movement/rotation and distinct line-clear effects (`--mute` disables audio). SPCR is separate from the unimplemented future `SPR1` replay draft; it has no network events or snapshot seeking. See [replay formats](specs/replay-v1.md). The retained diagnostic measures input bindings and timestamps; its old cursor/headless example remains a separate M0 determinism check.

Visual Studio 2022 users can override the generator with `cmake --preset windows-debug -G "Visual Studio 17 2022"` in a fresh build directory. Linux uses `linux-debug` presets with Clang, Ninja and SDL's desktop development prerequisites (see CI); local Linux execution is pending. First configuration fetches exact SDL 3.2.28 and Catch2 3.8.1 source commits. Cached sources permit subsequent offline builds. No ENet/JSON/UI dependencies are fetched before their milestones.

Build the independent core, replay library and CLI tools without fetching dependencies:

```powershell
cmake -S . -B out/core -G "Visual Studio 18 2026" -A x64 -DSTACKPULSE_BUILD_CLIENT=OFF -DSTACKPULSE_BUILD_TESTS=OFF -DFETCHCONTENT_FULLY_DISCONNECTED=ON
cmake --build out/core --config Release
```

On Linux use Ninja instead of the Visual Studio generator. `headless` is the equivalent Ninja preset when a C++20 compiler is already available. `STACKPULSE_FETCH_DEPENDENCIES=OFF` supports installed package configs, including a vcpkg toolchain; that alternative path remains untested. Exact tested dependency revisions and licenses are recorded in CMakeLists.txt and THIRD_PARTY_NOTICES.md.

See [M1 validation and outstanding gates](docs/M1_VALIDATION.md), [historical M0 validation](docs/M0_VALIDATION.md), [design](docs/GAME_DESIGN.md), and [work plan](docs/DEVELOPMENT_PLAN.md). Future Modern/Fusion, multiplayer/server, networking and release services are not implemented. No ROM is accessed by the build/tests; no reference trace or physical controller/rolling fidelity approval is claimed.

Issue-1 fixes and validation are recorded in [the resolution](docs/issues/issue-1-resolution.md). See [formatting and opt-in pre-commit checks](docs/FORMATTING.md).
