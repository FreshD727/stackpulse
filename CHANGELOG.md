# Changelog

## v0.1.0-alpha.1 — 2026-11-21

First private alpha release, with Windows x64 client/server and Linux x64 server packages.

- Classic, Fusion and Modern modes, Practice Lab, configurable keyboard/controller input and original synthesized sound effects.
- Server-authoritative rooms for 2–8 players, spectators, passwords and rematches.
- Timestamped replay history with deterministic verification and playback.

- Replay playback shows elapsed time and progress, supports forward/backward seeking, and returns to the same browser selection and scroll position when finished.
- Seeking restores the complete deterministic state from cached checkpoints. Reconstruction is silent and does not change recordings.
- Join Lobby now lists Back after Refresh and preserves the selected lobby or footer action when the directory changes.
- Specifications live under `docs/specs`. Local issue reports and review evidence have been archived outside the repository.
- The README includes native screenshots, release launch instructions, source-build instructions and compatibility information.

See [release checks](docs/RELEASE_CHECKS.md) for validation and artifact details.
