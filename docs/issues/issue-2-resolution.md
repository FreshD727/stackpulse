# Issue 2 resolution

The supplied [report](issue-2.md) is retained, with trailing whitespace normalized. Classic simulation, its controls, and SPCR remain unchanged. Presentation colors now distinguish all seven pieces.

## Gameplay

Fusion uses the exact Classic xorshift32 uniform rejection draw, including seed-zero normalization and piece-ID mapping. It consumes one active and one next draw, with one once-per-piece hold slot. Pieces spawn visibly at y=0; its next panel shows one piece. Start levels 0..29 use Classic transition thresholds and gravity intervals, including speed plateaus. Rotation and full T-spin detection retain the project SRS tables. Non-T pieces lock on the first blocked downward attempt, matching Classic timing; T pieces have Modern's 30-tick grounded delay and fifteen manipulation resets. Both soft drop (one point/cell) and hard drop (two points/cell, immediate lock) work.

Fusion normal clear points are Classic's 40/100/300/1200 times the post-clear level plus one. T-spin single/double/triple award 1600/2400/3200 times that multiplier; a zero-line spin awards zero. These deliberate defaults put every clearing spin above a four-line clear and increase each tier. Fusion does not multiply scoring for B2B; existing battle attack bookkeeping remains. Both modes now award an additional 2000 times level plus one for an all-clear, alongside existing all-clear attack. Modern otherwise retains battle scoring.

Modern's overly sensitive horizontal repeat is reduced from 6/2 to 10/3 ticks (167ms initial repeat, 50ms repeat at 60Hz). Fusion keeps 16/6 and Classic's controls are preserved.

## Root causes and presentation

Modern soft drop spent its accumulated slow-gravity timer in a while loop after switching to a two-tick interval. Pressing Down near tick59 could therefore move almost a board in one simulation tick. The timer now clamps to the new cadence and performs at most one downward attempt per tick; phase-by-phase press/release regression tests exercise every level.

Hard drops now emit their own sound; T-spin single/double/triple have distinct escalating synthesized cues. Palettes distinguish seven pieces and change only when the actual gravity interval changes, including plateau levels.

## Compatibility

Modern/Fusion rules and canonical state are version2, with start level serialized explicitly. SPMR version2 has an exact new rules identity and clearly rejects incompatible version1 recordings. Classic SPCR is unchanged. Match snapshots embed the versioned board codec, so old board snapshots cannot silently resume under changed gameplay.

## Validation

Windows Debug build and all four CTest registrations pass (unit, deterministic example, client unit, native smoke; 2.04 seconds). Core suite: 78 cases / 599508 assertions; client suite: 9 cases / 1855 assertions. New coverage includes every soft-drop phase at every Modern level, Fusion seed-zero and exact Classic RNG draws, all start-level thresholds, floor/landing lock timing matched to Classic, T-only reset cap, spin/normal/all-clear scores, distinct palettes and speed plateaus, complete v2 state/replay restoration and v1 rejection, and controlled asynchronous save pending/failure/retry/newer-input cases. Existing full-state, match, maximum capacity and Classic replay tests pass.

Pinned clang-format 22.1.3 checks all 30 owned C++ files; structural contracts and whitespace checks pass. The dependency-free offline Release build passes. Native dummy video/audio runs in both modes execute 90 frames / 88 simulation ticks, clear one spin line and report zero overload. Captures are retained locally at `out/issue2-fusion.bmp` and `out/issue2-modern.bmp`; Fusion capture was visually inspected for readable hold/controls/preview and visible active piece. These runs exercise the audio callback but do not establish subjective sound satisfaction.

The slow/hang investigation identified two concrete sources: synchronous replay encoding simulated the complete recording twice on the main thread, and paused/menu wallclock could remain in catch-up debt. Modern encoding now validates/checkpoints in one simulation pass. The client uses one background save worker with an immutable snapshot, serialized retries, exception reporting, and a replay-load gate preventing concurrent truncation/read. New inputs remain dirty after older snapshot completion. Failed snapshots remain retryable across run/mode changes; exit drains/retries and reports errors, but still exits on persistent disk failure. The scheduler excludes paused/menu time while retaining active tick debt.

A local untracked Debug harness measured a 1000000-frame recording: verification 618.51ms and new encode 914.67ms, 4021076 output bytes. The former double-pass encoding cost is estimated as their sum, 1533.18ms; this is an algorithmic same-run estimate rather than a historical executable benchmark. Background saving removes this encoding work from rendering; snapshot copies remain bounded by the million-frame limit. Replay loading still validates synchronously, and physical sound/input latency, human handling, and a broad hardware performance matrix remain manual acceptance checks.
