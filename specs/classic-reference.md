# Classic reference contract

Status: reference measurement plan, updated 2026-11-18. No fidelity approval or reference observations exist. M1 supplies a playable **Classic-inspired provisional ruleset**, not reference-accurate Classic. The unknowns below block fidelity approval; the provisional implementation is a separately identified design choice.

## Implemented provisional behavior

`include/stackpulse/classic.hpp` and `src/core/classic.cpp` define provisional rules version 2. These values are authored behavior, not measurements or approved reference constants:

| Mechanic | Implemented choice |
| --- | --- |
| Clock/board | Logical 60/1 ticks per second; 10x20 stored visible cells; active cells may occupy y=-4..-1 but no hidden board rows are stored |
| Spawn/rotation | All pieces spawn at x=3,y=-1; T/J/L start at orientation 2 (180 degrees from the base matrix), I/O/S/Z at 0; next preview uses the same orientation; generic clockwise matrix rotation (I uses 4x4, others 3x3); O cells fixed; no kicks |
| Horizontal | Immediate move on a direction change; first repeat after 16 further held ticks, then every 6 ticks; neutral/opposite directions reset charge; charge advances during entry delay |
| Input order | Derive press edges and DAS; consume entry delay/spawn and return if delayed; rotate; horizontal move; gravity/lock. Opposite rotation press edges cancel. Rotation uses press edges, not held repeats |
| Gravity | Frames per cell for levels 0..29: `48,43,38,33,28,23,18,13,8,6,5,5,5,4,4,4,3,3,3,2,2,2,2,2,2,2,2,2,2,1`; levels above 29 use 1 |
| Lock/drop | A failed downward attempt locks immediately; soft drop uses min(2,gravity) ticks and gives one point per successful downward cell while down is held; no hard drop or hold |
| Delays | Constant 10 ticks after an ordinary lock, 20 after a clear; no separate clear animation timer; spawning consumes the final delay tick |
| Randomizer | Wall-clock-independent xorshift32 (13,17,5); zero seed becomes 1; reject output values 0..3, then modulo 7; independent draws permit repeats; no bag or reference retry algorithm |
| Score/level | Clears award 40/100/300/1200 times pre-clear (level+1); start level clamps to 0..29; level=start_level+floor(total_lines/10); total lines saturates at UINT32_MAX, preserving resumable states at the counter boundary |
| Top-out | Any locked cell above visible row 0 ends play before clear processing; blocked spawn also ends play |

State serialization includes board, active/next pieces, RNG, held inputs, DAS direction/timer, gravity/entry timers, tick, score, lines, levels and game-over flag. This is a distinct 268-byte gameplay codec; the M0 diagnostic codec remains separate. Version 2 flips the T/J/L spawn orientation as requested during playtesting; prior version 1 states and replays reject rather than silently playing under changed spawn rules. The authored Practice clear fixture and implementation tests establish consistency with these choices only. Replay identity embeds their provisional description (see [replay contract](replay-v1.md)). Reference-trace execution and human handling approval remain outstanding.

## Target and provenance

Proposed scope is one NTSC release/revision. Exact release identifier, revision, reference clock numerator/denominator, capture setup, controller, adapter, display and measurement uncertainty are **unknown**. PAL is out of scope. Do not fill unknowns with familiar constants or inherit Modern rules. No ROM has been opened, executed, disassembled, copied or used by this work. No distributed test/build may depend on a ROM, extracted asset, or program code.

Maintain a measurement ledger with observation ID, author, date, lawful reference source description, revision, hardware/software configuration, raw timing uncertainty, procedure, independent trace path, reviewer and approval state. A reference observation must be distinguished from a proposed design choice. Optional private reference tooling belongs outside distributable artifacts. Public release additionally needs provenance/branding review; this specification makes no legal clearance claim.

## Behavior matrix

Every row is currently UNKNOWN / UNAPPROVED:

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
