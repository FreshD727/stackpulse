# Offline Classic, Fusion and Modern SDL client

`stackpulse_client` provides local Classic/Fusion/Modern Marathon, Practice Lab, keyboard/gamepad settings, and deterministic record/replay. This is an independently authored **Classic-inspired approximation**, not a reference-validated reproduction. The SDL-free mechanics and replay verifier use the same simulation as the client.

Build with `cmake --build --preset windows-debug`. Run `out/build/windows-debug/Debug/stackpulse_client.exe` from this repository. The 640x360 logical canvas scales continuously to the largest viewport that fits the window, including when maximized. Letterboxing preserves its proportions on other aspect ratios. SDL bitmap debug text and original primitive artwork need no external font or asset files.

All three modes use ten original retro color palettes, selected by the current level modulo ten. I/O/T share a light color, J/L share the first accent, and S/Z share the second. Locked blocks, the active piece and the next preview all use the current level palette, including selected start levels and replay playback. The whole board recolors immediately on level transitions. These colors are NES-inspired, independently authored, and need no ROM assets.

## Controls

- Menus: Up/Down or W/S and Enter; controller D-pad and South. Escape or controller Back always backs out, cancels a remap, or toggles pause.
- Gameplay defaults: Left/Right move, Down soft drops, X rotates clockwise, Z counterclockwise, Escape pauses. Controller D-pad moves, South rotates clockwise, West counterclockwise, Start pauses.
- F1 pauses; F2 restarts the current seed; F3 starts the next seed; F6 saves the current replay.
- Practice: F4 advances exactly one simulation tick and pauses; F5 loads an authored four-line clear fixture. Hold Down to clear it. The pause menu also exposes frame step and the fixture.
- Settings: select a keyboard/controller action and press its replacement; conflicting and reserved shortcuts are rejected. Escape/Back cancels capture. On the start-level row, Left/Right or controller D-pad Left/Right decreases/increases the level within 0–29. Enter/controller South also cycles through the levels. Left stick is optional, off by default, with 0.55 engagement / 0.35 release hysteresis.

Classic has no hold, hard drop, ghost or kicks. Fusion and Modern add project-90-v1 kicks and full spin/reward rules; Modern enables hold, hard drop and ghost. Networking remains absent. Repeated OS keydown events do not retrigger rotation or menus. Rotation uses sampled press edges; taps completed entirely between simulation boundaries are visible in the practice timeline but do not create an extra simulation action.

T, J and L pieces spawn and preview flipped 180 degrees from their original provisional orientation. Successful horizontal shifts (including DAS), soft-drop cells and rotations play short, distinct original synthesized effects. One-to-three-line clears play a rising sweep; four-line clears play a longer four-note ascending cue. Automatic gravity and blocked attempts stay silent; playback uses the same simulation events. Launch with `--mute` to disable audio for the session. Audio-device failure leaves gameplay available.

## Persistence and replay

The SDL preference directory for `Stackpulse/Classic-v1` holds `settings.txt`, Classic `last.spcr` and Fusion/Modern `last.spmr`; the console prints its resolved path. `--data-dir PATH` overrides it. Keyboard/controller bindings, stick preference, and starting level persist. Settings format v3 accepts existing v1/v2 settings and preserves the six old bindings, assigning nonconflicting defaults to added actions. Missing or malformed settings fall back to defaults.

Every run records its complete initial state and one held-input mask per executed simulation tick. F6, pause, restart, main-menu return, and exit save the latest recording. Failed saves appear in the status bar and console; reset/menu/replay-load operations stop before discarding unsaved inputs. Closing the app still exits after reporting any final save error. The 1,000,000-frame recording limit pauses the run and saves it. Only the latest recording per replay format is retained; copy it elsewhere to keep older runs.

Select the desired MODE first (Classic loads last.spcr; Fusion/Modern load last.spmr), then choose **Play Last Replay** to validate the format, rule identity, CRC, checkpoints and final hash before playback. Practice fixtures restore their full initial board and timers. Playback ignores gameplay input, supports pause and restart from the recorded initial state, and reports completion with a final hash. It does not overwrite the recording. The seed HUD says RECORDED because the original user-facing seed is not stored separately from the full RNG state.

The corrected transitions, two-state I/S/Z geometry and charged DAS use provisional rules/state version 3. Older recordings reject as incompatible; new recordings retain the SPCR v1 container and `.spcr` suffix.

`stackpulse_client --verify FILE` verifies and exits without initializing SDL, printing frames, final tick and hash; rejected files return status 1. `stackpulse_replay_verify FILE` is the standalone Classic verifier; `stackpulse_modern_replay_verify FILE` verifies Fusion/Modern SPMR.

## Timing and validation

The client samples timestamped physical transitions on rational 60 Hz boundaries. It runs at most eight overdue simulation ticks per rendered frame and retains all remaining backlog; no simulation tick is skipped. The status bar reports overload while behind. Late SDL events move to the next unsampled boundary and are counted in the console. Focus loss pauses and releases all bindings; controller removal releases that device and pauses. Resume requires a fresh physical press for held controls.

`stackpulse_client --smoke-frames 8` bounds startup/rendering. Add `--smoke-play` for automated fixture soft drop. Bounded smoke runs do not write replay or settings files. `--sticks` enables the optional stick mapping for that process. Automated checks cannot establish physical input latency, controller compatibility, reference fidelity, or monitor-rate behavior; those still require hands-on measurements.

The prior M0 input-only shell remains available as `stackpulse_diagnostic` and has its own overload policy. Its behavior is not the playable client's simulation policy.

## Fusion/Modern additions

The home MODE row cycles Classic, Fusion and Modern with Enter/controller confirm. `--mode classic|fusion|modern` sets startup selection. Fusion shows one preview, no hold/drop/ghost, 16/6 DAS/repeat and18-tick locking with eight resets. Modern shows five previews, a ghost and hold slot, 6/2 handling and30-tick locking with fifteen resets. Default hold is C/controller left shoulder, hard drop Space/controller right shoulder; Settings contains separate keyboard/gamepad rows for both. Start level applies to Classic only; Fusion/Modern level follows cleared lines.

In Fusion/Modern Practice, F7/F8/F9 select authored spin single/double/triple cavities. Press clockwise once and wait to lock (Modern can hard drop after rotation). F10 restarts with pending garbage in the initial state; it becomes eligible after60 ticks in this authored demonstration and inserts at the following spawn boundary. Fixture/restart commands save prior recording before replacing it. The HUD shows clear/spin attack, canceled/inserted rows, pending garbage, combo and B2B.

`--smoke-play` uses a spin single in Fusion/Modern and injects clockwise on the first simulation tick; Classic retains its four-line soft-drop fixture. `--capture FILE.bmp --smoke-frames N` exports the actual renderer final frame. `--smoke-settings` selects Settings for screenshot inspection. Captures are diagnostic, without physical input/latency validation.
