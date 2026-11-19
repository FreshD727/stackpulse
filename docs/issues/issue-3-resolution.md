# Issue 3 resolution

Historical record: [spin candidate resolution](spin-candidate-resolution.md) supersedes completed-spin delay eligibility and compatibility; [issue-4 resolution](issue-4-resolution.md) supersedes Fusion preview/spawn and Modern/Fusion compatibility, and adds hold audio and spin visualization.
The supplied [report](issue-3.md) is retained with whitespace normalized. This supersedes conflicting issue-2 Fusion handling and audio defaults. Classic simulation and SPCR are unchanged; Modern gameplay is unchanged.

## Fusion handling

Fusion horizontal handling now follows Classic: opposite directions are neutral; a blocked shift saturates DAS at 16 and retries the next active tick. A successful charged repeat resets to 10, giving six ticks until the next repeat. The screenshot-inspired J tuck compares both cores tick by tick with identical occupied geometry, shift events, positions, timer charge and lock timing. Fusion retains its existing project SRS rotation; Classic rotation itself is unchanged.

All pieces, including ordinary T pieces, lock on the next blocked scheduled downward attempt. A T gets the existing 30-tick grounded spin window and at most fifteen grounded manipulation resets only when its current geometry permits a spin. The deterministic local predicate is true if the current grounded pose already has three occupied pivot corners and the last successful manipulation was rotation, or one CW/CCW rotation using the actual first successful SRS kick reaches a grounded three-corner pose. Each direction stops on its first noncolliding kick; unreachable later candidates cannot grant time. Walls count as occupied. The predicate is recomputed after movement/gravity each tick, so moving out of a potential cavity restores ordinary locking immediately on a blocked descent. It does not search routes involving future translations or multiple rotations. Rotation history alone, empty space, flat ground and an unrotated fully blocked wedge cannot grant the exception. Spin classification at lock retains the existing last-rotation plus three-corner rule.

T, J and L spawn in orientation 2, matching Classic geometry; next and hold rendering share that orientation. Hold resets gravity, spin markers, elapsed lock time and reset count. Authored practice spin fixtures deliberately retain their orientation-0 entry and existing CW kick-4 route, rather than being normal spawns. Their single/double/triple clearing behavior remains playable; mirrored CCW entry is also tested.

Hard drop is disabled in Fusion at both client input translation and core simulation. Direct hard-drop bits are accepted and ignored, with no hard-drop movement, points or event; ordinary gravity continues. The Fusion HUD no longer advertises it. Hold remains enabled. Modern keeps hard drop and its existing handling.

## Audio

Spin single/double/triple now use falling bass impacts, deterministic noise cracks and inharmonic metallic tails instead of the four-line melody. Extra delayed impacts and longer tails distinguish the ascending tiers at the same bounded gain. The four-line cue is unchanged. Samples remain below .12 peak per voice, including tests for finite samples, envelope taper, deterministic output, bass distinction and escalating duration/energy. No audio assets were imported. These checks establish structural timbre differences and bounded output; human listening remains the acceptance check for perceived impact.

## Compatibility

Modern/Fusion rules, canonical state and SPMR are version 3 with an exact new embedded rules identity covering local-spin locking, flipped spawns, Classic wall charge and Modern-only hard drop. Earlier v1/v2 snapshots and recordings are rejected explicitly, preserving decoder output on failure. Shared Modern state hashes change with the codec identity even though Modern gameplay is unchanged. Match snapshots inherit the versioned board codec. Classic SPCR remains intact.

## Validation

- Sequential MSVC Debug compilation passed for all changed sources and tests with warnings as errors. The running user client initially locked the normal executable at link time (LNK1168); it was preserved and an alternate `stackpulse_client_issue3.exe` was built. Once the user process exited, the normal `out/build/windows-debug/Debug/stackpulse_client.exe` relinked successfully.
- All four CTest registrations pass, including native smoke against the updated normal executable (2.31 seconds). Core/replay/input tests pass 85 cases / 600091 assertions; client tests pass 11 cases / 107484 assertions including the new PCM checks.
- The updated alternate executable ran dummy video/audio Fusion and Modern practice spin smoke runs, 90 frames each, clearing one spin line with zero late input and zero overload. A 2000-frame Fusion run also completed with zero overload. Captures are local under `out/issue3-*.bmp`; the regenerated Fusion capture was visually checked for readable controls and absence of hard-drop hints.
- Disconnected dependency-free Release build passed; headless 100000-tick example retained hash `feca8ff604d809e5`.
- Pinned clang-format 22.1.3, schema/geometry/whitespace contracts and Git whitespace checks passed for this change. The newly supplied out-of-scope `docs/issues/issues-4.md` has no final newline, so the unfiltered global checker now flags that user file; the same checker excluding only that file passes. The file was preserved untouched and uncommitted. Commits retain FreshD727 and use the normal formatting hook.

The usual client executable now contains the updated build. No game process was stopped. No network or reference-fidelity claim is added.
