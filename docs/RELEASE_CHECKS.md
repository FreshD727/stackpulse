# Private alpha release preparation

Validated on 2026-11-21 for the first private prerelease, `v0.1.0-alpha.1`.

## Changes

Replay completion returns to the existing replay list and exact selected row. The derived scroll position remains unchanged. Playback displays elapsed/total seconds and a progress bar. Left/Right seeks five seconds, Page Up/Down or controller shoulders seeks one minute, Home returns to the start and End finishes the recording. Space/controller South pauses or resumes. Seeking works during play or pause, reconstructs complete Classic/Fusion/Modern state from lazy 600-frame checkpoints, emits no reconstruction sounds and does not write replay files. Empty recordings immediately return to their selected list position.

Join Lobby includes Back after Refresh. Selection follows the room code across directory reordering and preserves Refresh/Back when room counts change. Empty directories still offer both actions.

## Validation

- Windows builds compile with warnings treated as errors. Updated client tests passed: 33 cases, including replay seeking and lobby selection. Core/replay/input tests passed: 98 cases. Actual UDP/protocol tests passed: 28 cases. The deterministic example and native startup checks passed.
- Linux server/network CTest passed all 3 targets. Core rules, replay formats and wire revision remain unchanged.
- Native checks covered forward/backward seeks, restart, final-state verification and natural completion from scrolled rows 7, 8 and 9 across all three modes. Selected row, list order and final replay hash were preserved. Empty terminal recordings passed in all modes.
- Eight native players and one spectator agreed with the server's terminal tick, result and state hash.
- Native screenshots were inspected for Classic, Fusion, Modern, replay history, playback progress and the Join Lobby Back action. The gameplay screenshots show authored Practice Lab fixtures.
- Structural schema/whitespace checks and local Markdown-link checks passed. The retained pre-commit formatting hook was used for commits.
- Both package generators passed dependency inspection and startup checks from fresh temporary directories on the existing Windows/Ubuntu WSL hosts. These are not clean-machine VM or physical-controller validation claims.

## Local layout

The latest extracted Windows build is in `out/release`, including its runtime DLLs, licenses and hash manifest. `out/packages` retains the latest Windows ZIP, Linux server archive and their validation records. Older builds, dependency caches and temporary QA output were removed after validation. A future source build downloads its pinned dependencies again.

Issue reports, their images and historical review probes were copied and hash-verified in the project's top-level `issues` folder outside the Git repository before their repository copies were removed. Git history was not rewritten. Specifications moved from `specs` to `docs/specs`; the empty `cmake` directory was removed. Dependency notices and licenses remain intact.

Commits retain the configured Git author. These checks were completed before creating the private GitHub repository and publishing the first prerelease; repository privacy and uploaded asset hashes are verified separately during publication.
