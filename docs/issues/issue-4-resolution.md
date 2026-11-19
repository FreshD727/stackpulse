# Issue 4 resolution

The supplied [report](issues-4.md) is retained with whitespace normalized only. This supersedes earlier Fusion preview/spawn descriptions and Modern/Fusion compatibility. Classic simulation and SPCR remain unchanged; Modern gameplay remains unchanged.

## Storage audio

An accepted first hold into empty storage plays an original descending two-note storage cue. A hold with an occupied slot retrieves its piece while storing the outgoing piece, and plays a separate rising retrieval cue. The swap receives one cue rather than both. Rejected holds are silent. Dispatch reads accepted simulation events and the previous storage occupancy, including replay playback. Both presets use these cues; Classic has no hold. Deterministic samples have opposing pitch order, distinct waveforms, bounded gain and tapered envelopes. The immutable PCM bank is allocated once on the heap before playback, avoiding exhaustion of the Windows default thread stack as the bank grows; the callback does not allocate or resize it. Muted and deliberately unavailable-device smoke runs also pass with zero late input/overload. Human listening is still needed to judge subjective sound preference.

## Fusion queue and top spawn

Fusion fills and displays all five next slots like Modern. Queue consumption shifts all slots and draws one replacement. It retains Classic's uniform rejection draw, seed normalization and piece mapping, so the active/next piece stream is preserved. Filling the additional four previews advances the stored RNG four draws ahead of the former one-preview state; this does not replace the generator with a bag or change the piece order. Empty hold consumes one next piece; occupied swaps and rejected holds consume none. All five slots and the advanced RNG are serialized.

Every ordinary Fusion spawn, including hold retrieval, places the top occupied cell at visible row zero. The vertical translation is computed from the selected piece's actual cells. This removes the empty top row from I and flipped T/J/L without hiding any cells. T/J/L keep orientation 2, I/O/S/Z orientation 0. Tests compare every piece's occupied geometry against Classic translated to visible row zero, covering Classic's shifted two-state I/S/Z shapes. This intentionally uses visible occupied geometry rather than copying Classic's origin coordinate blindly. Authored spin fixtures keep their explicit entry positions; Modern keeps its hidden spawn and existing geometry.

## Spin inspector

Fusion/Modern Practice now opens the current-board inspector with F11. Simulation and audio freeze, preserving the prior play/pause state on close (F11 or Escape). F12 selects candidate orientation; Shift+F12 switches the assumed last successful manipulation between rotation and no rotation. F10 still loads the garbage demo. F11/F12 are reserved during remapping.

The inspector enumerates every legal T origin for that orientation, including hidden storage rows, using the actual shared core spin predicate. The gameplay boolean delegates to the same diagnostic result. Colors identify a completed grounded three-corner spin with rotation history, or a CW/CCW first-successful-kick route into such a pose; white indicates multiple reasons. Colored cell outlines show candidate geometry, small squares mark origins and crosses mark pivots. Solid fill shows board blockers; the live active/ghost fill is hidden so it cannot be confused with occupied board cells. ACTUAL ACTIVE separately reports the real current piece/history and reasons. HIDDEN counts wholly invisible accepted poses; partly visible candidates are drawn.

This visualizes the existing local check; it does not change its acceptance or prove a path from spawn. It does not search future translations or multiple rotations. Tests exhaustively compare enumeration against the gameplay predicate for both presets, every orientation/history assumption, original/mirrored single/double/triple fixtures and a wider origin range; they cover collisions, flat ground, CW/CCW first-success routes and completed-spin history differences. Inspection copies state and leaves live board, RNG, timers and history unchanged. `--spin-debug` opens the authored single-spin inspector at tick zero for bounded captures.

## Compatibility and validation

Modern/Fusion rules, canonical state and SPMR are version 4 with an exact embedded identity for five Fusion previews and occupied-top-row spawn. Older v1/v2/v3 snapshots and recordings are rejected explicitly without replacing decoder output. Match snapshots inherit the new board codec. Modern state hashes change with the shared version even though its gameplay is unchanged. Classic SPCR is preserved.

- Sequential MSVC Debug build passes with warnings as errors; all four CTest registrations pass (2.42 seconds), including the normal native executable.
- Core/replay/input: 89 test cases / 725431 assertions. Client/audio: 13 cases / 173748 assertions. Queue coverage compares five future Classic draws across 100 locks per seed and exercises empty/rejected/occupied holds and full-state restoration.
- Native dummy video/audio captures are retained locally at `out/issue4-spin-debug.bmp`, `out/issue4-fusion.bmp`, and `out/issue4-modern.bmp`. Inspector capture is frozen at tick zero with accepted colored poses and a separate live CW reason. Fusion/Modern 90-frame spin smoke runs each clear one line, with zero late input and overload. Captures were visually reviewed for readable inspector labels, blockers, markers, five previews and visible top spawn.
- Dependency-free offline Release build passes; the 100000-tick headless example retains `feca8ff604d809e5`.
- Pinned clang-format 22.1.3, structural schema/geometry/whitespace contracts and Git whitespace checks pass. Commits retain FreshD727 and the normal formatting hook; no push and no user game process termination.

Native smoke initially exposed Windows stack exhaustion after expanding the inline tone bank. Moving only that preallocated bank to the heap restored native startup and all smoke checks; no stack-limit change was needed. No physical-controller, subjective-audio or reference-fidelity acceptance is inferred from automated checks.
