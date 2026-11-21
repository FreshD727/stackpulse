# Issue 6 resolution

Implemented from `issues-6.md` and both annotated UI streamline screenshots on 2026-11-21.

## Seeds

Solo startup chooses a random nonzero 32-bit seed. Subsequent fresh Marathon/Practice starts and New Seed actions draw another random seed, rejecting the immediately previous value. Restart Seed preserves the current seed; replay restart restores the recorded initial state. The deterministic simulation and replay formats are unchanged.

The server now chooses a fresh random seed for each room match and rematch, rather than reusing 727. Randomness stays at the application boundary. Both executables accept `--seed N` for reproducible checks, including zero and the maximum unsigned 32-bit value. The client override selects its initial run; later fresh/new-seed actions remain random. The server override deliberately fixes every match/rematch.

## UI

- Home labels are Toggle Mode, Replay, Settings and Multiplayer, without the crossed-out explanatory suffixes.
- Removed button-mapping help prompts from home, gameplay, pause, settings, spin inspector, online name/lobby forms and multiplayer overlays. Settings still displays editable key/controller binding values; status and error messages remain available.
- Marathon pause omits Step One Tick and Clear Fixture. Practice Lab retains both; navigation skips unavailable rows outside practice and during replay.
- Waiting/countdown/results screens expose selectable Leave Lobby buttons. Players can select Ready/Cancel Ready while waiting; managers can select Return to Lobby after results. Keyboard/controller navigation remains operational. Leaving returns to the server directory without disconnecting. Existing session-exit shortcuts remain available.

## Verification

- Windows Release build and all 5 CTest targets passed. Linux server/network build and all 3 CTest targets passed.
- Deterministic chooser tests cover rejection of zero/repeated draws. Actual UDP tests verify explicit server seeds 0, 727 and 4294967295 against the exact initial simulation state. Lobby action tests cover roles, manager permissions and room phases.
- Native startup checks observed three different randomly chosen seeds. Two launches with `--seed 123` produced the same state hash. An out-of-range client seed rejected with exit code 2.
- Captured native home, Marathon pause, Practice pause, Settings, name entry, choice, create, waiting, countdown and results screens. Screenshots confirm the annotated menu changes and Leave Lobby placement.
- Eight native players and one spectator reached the same server-confirmed terminal tick, result and hash using the server's random default seed.

Generated capture/report artifacts are in `out/issue6/`. Windows and Linux packages were refreshed and checked from fresh temporary directories on the existing hosts; this does not claim clean-machine validation. No rules, replay or wire version change was needed. Existing running Debug processes were left intact; updated binaries are in the Release directory. Physical controller input was not re-tested in this automated pass.
