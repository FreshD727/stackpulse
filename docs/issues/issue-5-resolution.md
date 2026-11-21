# Issue 5 resolution

Implemented from `issues-5.md` on 2026-11-21.

## Gameplay

Holding Down through a lock no longer soft-drops the next piece. Release and press Down again to enable soft drop for that piece. This applies to Classic, Fusion and Modern, including hold swaps, Classic entry delay, prediction, snapshots and replay restoration. Normal gravity continues while soft drop is disarmed. Disconnect clears held input and rearms the release gate.

Classic multiplayer runs the existing Classic simulation as its authoritative source, with a validated projection for shared rendering. It retains Classic rotation, RNG, entry delay, scoring and level progression; it has one preview and no hold, hard drop, ghost or garbage attacks. The last surviving player wins.

## Lobbies and controls

The native multiplayer flow accepts a generated or saved name, then offers Create Lobby or Join Lobby. Creation configures name, optional password, capacity, mode and start level. The directory shows server lobbies; selecting a locked lobby prompts for its password. Joining requests a player seat explicitly; Watch is a separate action. Full or running lobbies return an error instead of silently converting a player request into a spectator.

One server supports Classic, Fusion and Modern lobbies together. The lobby supplies the client mode. The manager can change the Classic/Fusion start level with Left/Right while waiting; changes reset readiness and preserve the lobby password. Start, South or Enter toggles readiness. Start opens the input-release overlay during play.

## Presentation

Normal gameplay hides diagnostic counters and extended control hints; F1 reveals them. NEXT is directly right of the main board, followed by opponents. The left leaderboard sorts scores descending, with stable player IDs breaking ties. OUT and DISCONNECTED use distinct text and colors. Opponent headings include stable player IDs so truncated names remain distinguishable. Lobby forms omit irrelevant gameplay headers.

## Compatibility

Use matching updated clients and servers: SPD3 wire revision 3 rejects older revisions. Classic rules/state identity is 4 (269 bytes); its SPCR container remains version 1 and rejects older embedded rules. Shared multiplayer state and Fusion/Modern SPMR identity are version 6 and reject older versions. Old recordings are not migrated.

## Verification

- Windows Release build succeeded; CTest passed 5/5 targets (98 core/replay/input cases, 25 protocol/network cases, 21 client cases, deterministic example and native smoke).
- Linux headless/network build succeeded; CTest passed 3/3 targets, including actual UDP lobby tests.
- Added regressions cover release gating across spawn/hold/entry delay/reconnect, Classic source/projection consistency and snapshot restoration, lobby directory and secrets, mixed modes, level changes, explicit player versus spectator roles, leaderboard ordering and prediction epochs.
- Windows and Linux produced identical traces across all three modes: 10 seeds each, 10,000 ticks per seed, 600,000 total simulated ticks, with state restoration checkpoints.
- Eight native Modern players and one spectator agreed with the server's terminal tick/result/hash. Native gameplay, result, spectator and lobby form captures were inspected.
- Native Classic and Fusion pairs converged under combined latency, jitter, packet loss and duplication.
- Separate 15-second real-UDP capacity checks passed for Classic and Fusion with 8 players and 16 spectators, each completing a match and 24 terminal convergence checks. These are short regressions, not a repeat of the historical M4 30-minute soak.

Generated captures, traces and reports are under `out/issue5/`. The final binaries and refreshed distributable packages use Release; existing running Debug processes were left intact. Physical controller input was not re-tested in this automated pass.
