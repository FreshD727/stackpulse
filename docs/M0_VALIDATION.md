# M0 validation — 17 November 2026

The executable foundation is implemented and locally validated. This is an input/window and deterministic-core feasibility milestone, not playable Classic or multiplayer. The seven required specs are reviewable project contracts; Classic reference measurements remain unknown. Proposed Modern/Fusion values need gameplay fixture execution and tuning at their later milestones. No ROM was opened, executed, copied or required.

## Local evidence

Environment: Windows x64 10.0.26200, AMD Ryzen 7 5800X3D, MSVC 19.51.36260.0 (VS18), CMake 4.5.0-rc2, SDL 3.2.28 pinned source, Catch2 3.8.1 pinned source. Repository has no commit yet, so results refer to this initial working tree. No benchmark/latency numbers were measured.

| Check | Result |
| --- | --- |
| MSVC Debug configure/build, warnings as errors for project code | Passed |
| CTest `windows-debug` | 3/3 passed: unit, deterministic example, native smoke |
| Catch2 | 11 cases, 300367 assertions passed |
| Offline MSVC Release configure/build with client/tests off and fully disconnected FetchContent | Passed, no presentation/network dependency fetched |
| 100000-tick headless example | Debug/Release both `feca8ff604d809e5` |
| SDL real-window `--smoke-frames 5` | Passed; reported pads=0, late=0, skipped=0 |
| Native visual/keyboard check | Window rendered unclipped; A produced LEFT press/release timeline; window closed normally |
| JSON schema/example validation | Passed; unknown field, Classic preset, invalid lock and mismatched Modern hold rejected |
| Source whitespace check | Passed |
| Authored T-spin triple fixture | Geometry-only check passed: CW candidate4 succeeds, grounded, four corners, three full rows |

Synthetic input tests cover timestamp boundaries, sub-tick taps, overlap within/across devices, OS repeat/duplicate suppression, focus/device clears, stable ordering, stick hysteresis and safe delayed releases. Core tests cover PRNG vectors, explicit canonical fields, movement bounds, neutral ties, unchanged rejected frames, deterministic restoration and unsigned counter wrap. The native UI fix aligning column labels was rebuilt and CTest rerun successfully.

The triple JSON specifies a complete board, initial active T, CW input then neutral ticks, and expected lock/score/attack. `tools/check_contracts.py` independently verifies the geometry and JSON fixture, not the proposed lock timer or battle engine. Those full tick results become required mechanics tests when gameplay exists.

## Remaining acceptance gates

- Identify the Classic target revision/clock and obtain independently authored approved observation traces. No Classic constants or fidelity are certified.
- Review and accept proposed future gameplay/protocol/replay/battle contracts before dependent module work; implementation, full binary round trips and golden fixtures are not supplied by M0.
- Run CI on Windows/Linux to establish cross-platform builds/hashes. CI is configured but has not run remotely. Linux/macOS execution is untested here.
- Test a physical USB gamepad and NES USB adapter, hotplug/focus behavior, rapid tapping/rolling and end-to-end latency. Only keyboard A received a local visual check; controller support is implemented and synthetically tested, not hardware certified.
- Perform reference timing, refresh-rate sweep and performance measurements on declared display/controller hardware. Diagnostic overload collapse is visibly counted; it is not suitable as authoritative game scheduling.

## Reproduction and scope

Use the README presets, then `python -m pip install --target out/python-deps jsonschema==4.23.0` and `python tools/check_contracts.py`. The Python validator is a development check, not a runtime dependency. Upstream SDL/Catch exact revisions and licenses are in THIRD_PARTY_NOTICES.md. The alternate vcpkg-baseline path is present but untested.

No server composition is added before M3: `stackpulse_headless` proves a pure reusable core can run without SDL. There are no network, replay, gameplay, audio or menu implementations. Public gameplay contracts are proposed documents; only the explicitly named M0 diagnostic C++ API/encoding is implemented and frozen. This distinction prevents a diagnostic cursor from being mistaken for a completed gameplay state contract.
