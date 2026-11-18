# Classic reference contract

Status: reference measurement plan, updated 2026-11-18. No fidelity approval or captured reference traces exist. Public research informs the issue-1 corrections below. M1 supplies a playable **Classic-inspired provisional ruleset**, not reference-accurate Classic. The unknowns below block fidelity approval; the provisional implementation is a separately identified design choice.

## Implemented provisional behavior

`include/stackpulse/classic.hpp` and `src/core/classic.cpp` define provisional rules version 3. Remaining behavior is provisional; see the issue-1 research note below:

| Mechanic | Implemented choice |
| --- | --- |
| Clock/board | Logical 60/1 ticks per second; 10x20 stored visible cells; active cells may occupy y=-4..-1 but no hidden board rows are stored |
| Spawn/rotation | All pieces spawn at x=3,y=-1; T/J/L start at orientation 2 (180 degrees from the base matrix), I/O/S/Z at 0; next preview uses the same orientation; T/J/L rotate in a 3x3 matrix; I/S/Z have two states, their horizontal state is one row below the old base matrix; O cells fixed; no kicks |
| Horizontal | Immediate move on a direction change; first repeat after 16 further held ticks, then every 6 ticks; neutral/opposite directions reset charge; blocked attempts fully charge DAS and retry each active tick; charge saturates during entry delay until a successful repeat |
| Input order | Derive press edges and DAS; consume entry delay/spawn and return if delayed; rotate; horizontal move; gravity/lock. Opposite rotation press edges cancel. Rotation uses press edges, not held repeats |
| Gravity | Frames per cell for levels 0..29: `48,43,38,33,28,23,18,13,8,6,5,5,5,4,4,4,3,3,3,2,2,2,2,2,2,2,2,2,2,1`; levels above 29 use 1 |
| Lock/drop | A failed downward attempt locks immediately; soft drop uses min(2,gravity) ticks and gives one point per successful downward cell while down is held; no hard drop or hold |
| Delays | Constant 10 ticks after an ordinary lock, 20 after a clear; no separate clear animation timer; spawning consumes the final delay tick |
| Randomizer | Wall-clock-independent xorshift32 (13,17,5); zero seed becomes 1; reject output values 0..3, then modulo 7; independent draws permit repeats; no bag or reference retry algorithm |
| Score/level | Clears award 40/100/300/1200 times post-transition (level+1); start level clamps to 0..29; first transition uses `min(start*10+10,max(100,start*10-50))` lines, then every 10; total lines saturates at UINT32_MAX, preserving resumable states at the counter boundary |
| Top-out | Any locked cell above visible row 0 ends play before clear processing; blocked spawn also ends play |

State serialization includes board, active/next pieces, RNG, held inputs, DAS direction/timer, gravity/entry timers, tick, score, lines, levels and game-over flag. This is a distinct 268-byte gameplay codec; the M0 diagnostic codec remains separate. Version 3 changes transition/scoring, two-state geometry and charged blocked shifts; prior version 1/2 states and replays reject rather than silently playing under changed rules. The authored Practice clear fixture and implementation tests establish consistency with these choices only. Replay identity embeds their provisional description (see [replay contract](replay-v1.md)). Reference-trace execution and human handling approval remain outstanding.

## Target and provenance

Proposed scope is one NTSC release/revision. Exact release identifier, revision, reference clock numerator/denominator, capture setup, controller, adapter, display and measurement uncertainty are **unknown**. PAL is out of scope. Do not fill unknowns with familiar constants or inherit Modern rules. No ROM has been opened, executed, disassembled, copied or used by this work. No distributed test/build may depend on a ROM, extracted asset, or program code.

Maintain a measurement ledger with observation ID, author, date, lawful reference source description, revision, hardware/software configuration, raw timing uncertainty, procedure, independent trace path, reviewer and approval state. A reference observation must be distinguished from a proposed design choice. Optional private reference tooling belongs outside distributable artifacts. Public release additionally needs provenance/branding review; this specification makes no legal clearance claim.

## Behavior matrix

Every row still requires independent trace and handling approval:

| Mechanic | Required observations / boundary cases |
| --- | --- |
| Clock/input | Exact rational frame rate, sample boundary, press-release within one frame, simultaneous keys |
| Geometry | Spawn coordinates per piece, orientations/pivots, hidden rows, collision boundaries |
| Rotation | Both directions, wall/floor obstruction, simultaneous gravity/rotation, no added kicks |
| Horizontal | Initial press, DAS charge/reset/repeat, wall charge, neutral/repress, opposite directions, carry through delays |
| Vertical/lock | Gravity table, soft drop cadence/score, gravity remainder, contact lock ordering |
| Delays | Entry and line-clear duration, sampled/ignored actions, ordering on first active frame |
| Randomizer | Startup/seed state, recurrence, retry behavior, dependencies on elapsed time, repeated pieces |
| Score/level | Each clear, drop points, level transition thresholds, starting levels, integer overflow policy |
| Top-out | Spawn collision, hidden locking, clear rescue, extreme-level deliberate divergences |

## Approved trace format

Use independently authored UTF-8 JSON, `trace_version: 1`, `status: "proposed"` or `"approved"`, `observation_ids: [string]`, `target: {region, revision, clock_num, clock_den}`, `initial_state`, and `frames`. Each frame has `tick` (decimal integer string u64), `held` (u32 action mask), `expected_state` and `expected_events`. Once a Classic state codec exists, add its schema version and canonical state/hash bytes; do not call M0 cursor states Classic states. Unknown values are null, never guessed expectations. Approved fixtures require no nulls in relevant fields, independent provenance review, exact initial hidden/timer/RNG state, consecutive ticks and reviewer/date. Reject duplicate ticks, unknown action bits and missing expected outcomes. No approved trace is supplied in M0.

Required suites: every matrix row, long holds through delays, wall/repress sequences, rapid tapping and same-frame transitions. Tick comparisons must include hidden state, not only the visible board. Human blind handling checks must separately cover DAS, hypertapping and rolling on actual supported devices. Synthetic sequences establish logical behavior only. Fidelity remains unclaimed until approved traces pass and human sign-off records remaining deviations.

## Issue-1 research note

[Michael Birken’s original Nintendo Tetris investigation](https://meatfighter.com/nintendotetrisai/), sections Representing Tetriminos, Shifting Tetriminos, Dropping Tetriminos, Slides and Spins, and Lines and Statistics, informed these corrections: I/S/Z use two orientations; blocked horizontal attempts charge DAS; failed scheduled descent locks; the first transition depends on the starting level and later transitions occur every ten lines; clear scoring follows transition. Level 15 first transitions at 100 lines.

Our tests independently reconstruct the supplied Z/J tuck and exercise rotation, blocked shift, descent, retry and lock. They are synthetic regressions, not NES traces. Starts 20–29 extend the project’s formula beyond the source’s native start selection. Neutral reset, simultaneous/down inputs, tick rate, entry/clear delay, randomizer and unsupported original overflow quirks remain provisional. No modern resettable lock delay was added. The original user image supplies placement intent, not timing evidence.
