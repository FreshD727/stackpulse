# M1 validation — 18 November 2026

## Spawn and sound follow-up — 18 November 2026

T/J/L now spawn and preview at orientation 2, preserving the existing rotation matrices. Provisional rules/state version 2 rejects older recordings explicitly. Original synthesized shift, soft-drop and rotation cues use successful simulation events; gravity and blocked attempts are silent. Controller input handling is unchanged; the user reports controller support works.

The updated Windows build passed CTest 3/3 and 29 Catch2 cases (311760 assertions). Contract/schema/whitespace checks passed. Three 80-frame Practice smoke runs with dummy audio, `--mute`, and an unavailable audio driver all exited successfully, cleared four lines in 17 gameplay ticks, and produced identical state hash `7124344215189651081` with zero late events/overload. The unavailable-driver run continued silently as intended. These checks exercise initialization, event-driven audio and failure handling; speaker output/listening quality was not manually verified. Earlier M1 evidence below remains historical.

M1 supplies an offline playable Classic-inspired provisional vertical slice: Marathon, Practice Lab, persistent input settings, replay capture/playback and a headless verifier. The deterministic simulation and replay modules build independently of SDL. This is implementation progress with explicit fidelity gaps; the original reference-fidelity acceptance gate is still open.

## Scope and evidence

The core implements seven pieces, generic matrix rotations without kicks, collision, gravity, soft drop, immediate failed-down locking, DAS, line clears, scoring, level progression, entry delays and top-out. See [exact provisional behavior](specs/classic-reference.md). These authored constants and tests are not reference observations. No ROM or extracted assets are required.

Practice includes deterministic restart/new seed, frame stepping, the authored four-line I clear fixture, and event inspection. Keyboard/gamepad settings and optional stick input persist locally. The existing native input diagnostic remains available separately. See [client controls](../apps/client/README.md) for actual navigation and storage paths.

The `SPCR` replay module embeds exact provisional rules and a full 268-byte initial state, records one held mask per tick and verifies initial, 600-frame and final FNV hashes plus a whole-file CRC. It re-simulates from the initial state; hash entries are not seek snapshots. This is separate from the unimplemented future `SPR1` draft. See [format and limits](specs/replay-v1.md).

## Validation record

Results refer to the current working tree on Windows x64 using MSVC 19.51.36260.0 (VS18), CMake 4.5.0-rc2, SDL 3.2.28 and Catch2 3.8.1. Build/test results below were supplied by the integrating agent from actual runs; historical M0 results are not substituted for M1 passes.

| Check | Result |
| --- | --- |
| Windows Debug configure/build with project /W4 /WX | Passed |
| CTest windows-debug | 3/3 passed: unit, deterministic example, native smoke |
| Catch2 direct compact run | 26 cases / 310765 assertions passed |
| Contract validator | Passed schema/examples, whitespace and authored kick-4 geometry checks |
| Automated fixture smoke (`--smoke-frames 80 --smoke-play`) | Passed: 17 ticks, 4 lines, late=0, overload=0; smoke persistence disabled |
| Client headless verifier missing-path rejection | Passed: exit 1 |
| SDL native visual/menu/practice/replay checks | Pending final run |
| SDL-free offline Release configure/build in out/offline with client/tests disabled and FetchContent fully disconnected | Passed: core, replay, headless and standalone verifier built |
| Linux Clang build and cross-platform Classic/replay hashes | Pending; no local Linux execution |
| Physical USB controller/NES adapter, hotplug and rolling | Pending hardware checks |
| Approved reference traces and human handling sign-off | Unavailable; fidelity unclaimed |

Implemented test coverage includes seven spawn/rotation geometries, wall/floor/board collisions and failed rotations, gravity and soft-drop scoring, DAS cadence/opposites, failed-down locks, entry and clear delays, one-through-four-line scoring and levels, both top-out routes, complete state restoration and deterministic continuation. Regression tests cover terminal top-out state serialization, malformed full-row/DAS/mask rejection without mutation, canonical hash equivalence, and valid-state closure through large/saturating line counters. Replay tests exercise marathon/practice round trips, empty/file round trips, every truncated prefix and one-byte corruption of a sample, resealed malformed header/rules/state/input/hash data, intermediate hashes, frame limits and forbidden tick wrap. Synthetic adapter tests remain separate from physical-device validation.

## Remaining acceptance gates

- Identify the exact reference release/revision and rational clock; gather independently authored approved traces covering the behavior matrix and complete hidden state. Generic matrix rotation, uniform draws, constant delays and simplified level transitions remain declared divergences until evidence supports replacements.
- Run Linux/Windows shared fixtures and compare canonical state/hash results. CI configuration is not evidence that remote CI has run.
- Exercise supported physical controllers/adapters, remapping, hotplug/focus, hypertapping/rolling and monitor refresh rates. Synthetic tests and a successful keyboard check do not establish device compatibility or end-to-end latency.
- Supply independent replay golden bytes and broader robustness fixtures. Snapshot seeking, future SPR1 record streams/SHA-256/external events, Modern/Fusion mechanics and multiplayer remain later work.
- Measure timing/performance on declared hardware and complete clean-machine packaging/release provenance review.

Use the root README commands to reproduce builds/tests; run `python tools/check_contracts.py` for the existing JSON contracts when its documented jsonschema dependency is available. The headless M0 hash is a regression for the diagnostic core, not a Classic fidelity hash. Results must not imply unperformed platform or hardware tests.
