# Implementation and multi-agent work plan

Read [GAME_DESIGN.md](GAME_DESIGN.md) first. This file organizes a future build; no game implementation is included yet. Names and paths below are proposed, not existing APIs.

## 1. Repository layout

```text
CMakeLists.txt                 # Integrator owns root build/dependencies
CMakePresets.json
vcpkg.json
vcpkg-configuration.json
apps/client/                  # Composition root only
apps/server/                  # Composition root only
include/stackpulse/            # Public contracts; integrator approval required
src/core/                     # Deterministic player and match simulation
src/rules/                    # JSON validation and immutable rules construction
src/platform/                 # SDL input/window/audio adapters
src/presentation/             # Board drawing and controller-driven menus
src/protocol/                 # Bounded wire codecs and message types
src/client_net/               # Prediction, history, reconciliation, transport
src/server/                   # Rooms, deadlines, transport, spectators
src/replay/                   # Recording, reading, verification
assets/                       # Original visuals, fonts, audio plus licenses
rules/                        # Versioned built-in JSON presets
specs/                        # Frozen tables, interfaces, packet contracts
tests/core/                   # Mechanics and match fixtures
tests/protocol/               # Codec/fuzz tests
tests/integration/            # Client/server and recovery tests
tests/fixtures/               # Authored input/state traces, no ROMs
tools/                        # Replay verifier, bots, benchmarks, reference tooling
docs/                         # Design, decisions, benchmark reports
.github/workflows/            # Build/test/package CI
```

The core library must build and test without SDL, ENet, graphics/audio initialization, or network access. A headless server build must not pull in presentation dependencies.

## 2. Milestones and dependencies

| Milestone | Deliverable | Exit condition |
| --- | --- | --- |
| M0: contracts and feasibility | Native input prototype, reference plan, rules schema, frozen core API and protocol draft | Client window/controller diagnostic runs; deterministic headless example runs; required specs below reviewed |
| M1: Classic vertical slice | Playable Marathon, Practice Lab, settings, replay recording | Reference traces pass; repeatable replay; keyboard/gamepad play; fidelity gaps explicitly recorded |
| M2: Fusion/Modern | Rotation/kicks, hold/drop where enabled, spin/scoring rules, garbage simulation | Mechanic fixtures pass; authored T-spin single/double/triple scenarios are playable; Fusion handling playtest |
| M3: authoritative duel | Two native clients on dedicated server, prediction/reconciliation | Network impairment matrix passes; attacks and results generated only by server; reconnect verified |
| M4: multiplayer MVP | 2–8-player rooms, spectators, packaging, accessible menus | Capacity soak, cross-platform determinism, controller UX and clean-machine packaging pass |
| M5: release services | Public discovery/accounts, secure transport, ranking, operations | Security/release review, stable balance version, data policy and service monitoring |
| M6: scale experiments | 32/99-player rooms and spectator optimization | Measured admission/bandwidth/CPU budgets and balance approval for each capacity |

Critical path: M0 -> Classic core -> Fusion mechanics -> authoritative duel -> full room support. Rendering, diagnostics, tests, codec implementation, and server room scaffolding can develop alongside the core after M0. Do not add ranked play while the mechanics or network deadline policy is still changing.

M0 must produce these concrete specifications before dependent tasks start:

- `specs/classic-reference.md`: target revision/clock, behavior matrix, provenance, unknowns, approved trace format. Unknowns block only the dependent mechanics, not independent renderer/tool work.
- `specs/rules-v1.md`: exact board coordinates, geometry/spawn tables, tick order, gravity/delay tables, randomizer algorithm/test vectors, score and top-out rules.
- `specs/rotation-v1.md`: all orientations, pivots, ordered kick offsets, O/I behavior, successful/failed rotation and spin fixtures.
- `specs/core-api-v1.md`: complete state/event/input types, capacities, ownership, errors, serialization/hash fields.
- `specs/protocol-v1.md`: message field widths, channel/delivery choice, limits, tick mapping, missing/late input policy, version negotiation.
- `specs/replay-v1.md`: header, rules embedding, input/event records, checkpoints, corruption limits, compatibility behavior.
- `specs/battle-v1.md`: exact cancellation/dispatch/insertion order, targeting RNG, elimination and tie fixtures.

The design contains deliberate hypotheses, particularly Fusion tuning and network lead. M0 translates them into testable v1 contracts; it must not imply that unmeasured NES constants are already settled.

## 3. Work packages for parallel agents

Use one integrator and up to three implementation agents at a time. The lanes below are logical responsibilities, not a requirement to launch every agent concurrently. Each task gets its own branch/worktree when Git is initialized, its allowed paths, contract versions, fixture inputs, and acceptance criteria. This planning task did not launch agents or initialize Git.

| Package | Owner / paths | Depends on | Deliverables and acceptance |
| --- | --- | --- | --- |
| P00 Foundation | Integrator: root build, public headers, `specs/`, CI | None | Windows/Linux builds; separate core/client/server targets; pinned dependencies; minimal deterministic fixture; format/static-analysis checks |
| P01 Reference specification | Rules researcher: reference spec, `tests/fixtures/classic/`, optional local harness | Target revision identified | Authored traces for motion, DAS, locking, delays, randomization, scoring; unresolved behavior documented; no copied program/assets |
| P02 Classic core | Simulation owner: `src/core/`, Classic rules data, `tests/core/classic/` | P00, approved parts of P01 | Pure tick simulation, exact state snapshots, Classic fixtures passing; no platform calls |
| P03 Native input/render shell | Client owner: `src/platform/`, `src/presentation/`, client composition | P00 | Timestamped inputs, controller mapping/hotplug, scaled board, settings UI; runs with fake snapshots while core develops |
| P04 Replay and verification | Verification owner: `src/replay/`, `tools/replay_*`, replay tests | Frozen state/input encoding | Round-trip full state; deterministic replay verifier; corrupted input rejected; cross-platform hash runner |
| P05 Modern/Fusion mechanics | Simulation owner: same core ownership as P02 | P02, rotation and battle specs | Kick/hold/drop/spin/reset behavior, scores, cancellation, all mechanic fixtures; preset hashes stable |
| P06 Protocol codec | Network owner: `src/protocol/`, protocol tests | P00, protocol spec | Golden packets, bounds checks, decoder fuzz harness, documented channel assignments |
| P07 Server rooms and match runner | Network owner: `src/server/`, server composition | P06; stub core initially, P05 for integration | Ready/start/results lifecycle, input validation/deadlines, authoritative tick runner, reconnect and admission limits |
| P08 Client networking | Client owner: `src/client_net/` | P04, P06, P07 protocol harness | Prediction ring, input resend/ack, full snapshot resync, event deduplication; duel integration passes |
| P09 Multiplayer UI/assets | Presentation owner: `src/presentation/`, `assets/` | P03, room-view contract | Controller-driven lobby, target selection, spectator views, original audio/art, accessibility settings |
| P10 Reliability and packaging | Verification owner: integration tests, `tools/`, reports; integrator merges CI/package edits | M3 feature path | Impairment harness, soak/load results, clean-machine packages, dependency/license manifest, no bundled ROM |

Never assign P02 and P05 to separate concurrent writers of `src/core/`. Likewise schedule P03/P09 serially if both change menu infrastructure. If independent work inside a lane is needed, split exact files and interfaces first.

Suggested waves with four total agents:

1. M0: integrator sets contracts/build; reference agent gathers observations; client agent builds input diagnostic; verification agent designs trace/latency harnesses.
2. M1: core agent implements Classic; client agent builds presentation against frozen fake states; verification agent builds replays/tests; integrator reviews contracts and joins the vertical slice.
3. M2/M3: core agent implements modern/battle rules; network agent implements codec/room scaffolding; client agent prepares prediction against a server stub; integrator owns test integration and rejects protocol drift.
4. M4: network agent completes room/spectator behavior; client agent polishes menus; verification agent runs impairment/load/package checks; integrator resolves issues and owns release artifacts.

## 4. Interface rules that prevent conflicting work

- Public headers, rules schema, root build files, wire format, and golden fixtures have a named owner. Propose changes in a short design note before editing another lane's contract.
- Simulation emits semantic events; presentation never reads mutable core internals or changes gameplay state directly.
- Input adapter produces tick-addressed logical input; core knows nothing about SDL button IDs.
- Server and local practice call the same simulation API. No duplicated scoring or garbage logic in networking code.
- Snapshot views for menus/rendering are separate from resumable authoritative state. Never use a rendering view as a replay snapshot.
- Every feature PR includes the relevant contract version, tests run, and any changed fixture rationale. A failing golden fixture is investigated rather than regenerated automatically.
- Shared state uses explicit ownership; simulation and room state are mutated by one owner thread. Other threads exchange bounded messages or immutable views.
- Protocol and rules incompatibilities fail clearly. Do not add silent fallback to a different preset.
- Gameplay changes require a new rules version/hash and replay compatibility decision. Pure presentation changes do not.

## 5. Reusable agent task brief

Copy this into each future implementation assignment and fill every field:

```text
Objective: one observable deliverable from package Pxx.
Read: GAME_DESIGN.md, DEVELOPMENT_PLAN.md, listed frozen specs and headers.
Allowed writes: exact directories/files owned by this task.
Dependencies available: commit IDs, contract versions, fixtures/stubs.
Do not change: other owners' code, public contracts, unrelated dependencies.
Acceptance: named fixtures, test commands, manual checks, performance budget.
Handoff: changed files, API usage example, tests/results, limitations, follow-up issues.
If blocked: identify the missing contract/evidence; continue independent work in scope.
```

Start each agent from the same integrated contract commit. Merge small vertical changes, then rebase dependent work on the updated integration branch. Do not allow an agent to redesign rules or substitute a networking library as an incidental implementation choice.

## 6. Required test suites

Core examples: piece spawn/rotation per orientation; collisions at walls/floor/hidden rows; DAS timing through neutral/walls/entry delay; soft-drop plus gravity; frame-boundary taps; hold only once; hard-drop lock; lock-reset cap; all line-clear/spin cases; chains and perfect clears; seeded bag/generator test vectors; garbage cancellation/arrival/caps; simultaneous eliminations.

Determinism: capture complete initial state, rules, and tick inputs; run the same file in Windows MSVC and Linux Clang builds; compare canonical hashes at checkpoints. Include long runs, maximum counter values, and replay restoration mid-piece and during pending garbage. Define overflow behavior explicitly.

Network: authoritative server with two headless clients, then native clients; injected delay/loss/reordering/duplicates; late or future inputs; reconnection after history expiry; stale session; incompatible rules; malformed lengths; server/client pause stalls; simultaneous attacks/top-outs; spectators joining mid-clear; room-full handling. Verify authoritative final state and exactly-once results, not just absence of crashes.

Input/rendering: synthetic timestamp streams for boundary handling, physical controller tests for actual latency, frame-rate sweep, focus/device removal, remap conflicts and simultaneous bindings. Synthetic tests cannot establish physical rolling compatibility.

Load: fixed bots at 2, 8, 32, 99 players and realistic spectator subscriptions. Record p50/p95/p99 room tick time, bandwidth per client/server, dropped deadlines, snapshot size, memory, and correction frequency. Only the tested capacity becomes supported.

## 7. Definition of done

A milestone is complete when its executable path works end to end, required fixtures/checks pass, docs match the behavior, and material limitations are recorded. A compiled library or screenshot alone does not complete gameplay or multiplayer work.

MVP completion requires native offline and online play, controller bindings, three honestly labelled presets, working spin rewards, authoritative garbage battles, private rooms, spectators, replays, verified packaging, and the 8-player/16-spectator target. Classic must either pass its agreed reference suite and human handling check or remain explicitly labelled an approximation; the original aspiration of perfect fidelity must not disappear in a completion report.

Before starting development, the integrator should turn P00/P01/P03/P04 into the first scoped assignments. The user can revise the working name, target platforms, or Fusion defaults without invalidating this architecture.
