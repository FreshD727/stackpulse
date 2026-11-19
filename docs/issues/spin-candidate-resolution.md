# Fusion spin candidate resolution

This supersedes the completed-spin eligibility and history-toggle descriptions in the earlier issue-3/issue-4 resolutions. Only Fusion conditional lock timing changes; Modern keeps its unconditional 30-tick delay. Classic simulation and SPCR remain unchanged.

## Candidate eligibility and scoring

Fusion receives extra lock time only when one actual clockwise or counterclockwise input, using the first successful legal project kick, reaches a grounded T pose with at least three occupied pivot corners. A later kick is not considered after an earlier legal kick succeeds. The source pose must be legal. The check is local to the current pose and board; it does not establish reachability from spawn or search translations or multiple inputs.

The candidate boolean is strictly CW-or-CCW. Prior `last_rotation` and the current completed-spin geometry cannot grant eligibility. `SpinPossibility::already_spin` remains a separate diagnostic for the current grounded three-corner pose with scoring history, and is excluded from `possible()`. Enumeration takes only the board state and orientation, always returns route-only reasons, and ignores history. The inspector removes its history assumption toggle and completed-spin candidate category, showing live candidate eligibility and scoring readiness separately. Scoring readiness still requires a lock.

Eligibility is reevaluated after actual actions and gravity. Rotating into a scoring pose can remove extra lock time when neither subsequent rotation has a qualifying route. Fusion then locks on the next blocked downward attempt using Classic gravity timing, while preserving actual rotation history for spin scoring. Successful lateral movement still invalidates that history; failed inputs do not replace it. A qualifying route still receives 30 ticks and up to fifteen resets.

## Review cases and regression coverage

The existing untracked `docs/review/spin-cases` files and user images are retained as historical v4 evidence. CASE0001 remains eligible: its grounded T at orientation 0, origin (0,12), has a qualifying CW route. It survives a blocked gravity step with one delay tick regardless of prior history.

CASE0002 changes intentionally: its grounded T at orientation 1, origin (3,17), has three occupied corners but neither actual first-success rotation ends in a grounded three-corner pose. It receives no Fusion delay with either history value. At a blocked gravity step it locks and clears one line; the snapshot with rotation history still scores the 1600-point spin single, while the snapshot without that history scores the 40-point normal single. Modern continues its delay on the same geometry.

Tests cover these exact boards, every orientation and history value over the mirrored authored single/double/triple boards, legal candidate enumeration, actual CW/CCW input sequences, failed rotation and failed lateral movement, leaving a cavity, and single/double/triple spin scores. The authored scoring routes use real rotation input rather than fabricated history; the historical snapshot comparison explicitly supplies the recorded history solely to isolate timing from scoring.

## Compatibility

Shared Modern/Fusion rules, canonical state, and SPMR are version 5. The exact replay identity records the first-success CW/CCW grounded-three-corner lock rule. Older v1/v2/v3/v4 recordings and state snapshots are rejected explicitly, preserving decoder output. Modern also uses this shared codec identity despite retaining its gameplay timing. Classic SPCR is preserved.

## Validation

- Sequential MSVC Debug build passes with warnings as errors. CTest passes all four registrations (2.51 seconds); core/replay/input has 91 cases and 725923 assertions, client/audio has 13 cases and 173748 assertions.
- Dependency-free offline Release build passes; the 100000-tick headless example retains `feca8ff604d809e5`.
- Native dummy video/audio inspector capture `out/spin-candidate-v5-debug.bmp` is frozen at tick zero and was visually reviewed: route legend, live candidate and scoring readiness labels are readable above the footer. Fusion/Modern 90-frame spin smokes each clear one line, with zero late input and overload; captures are retained locally at `out/spin-candidate-v5-fusion.bmp` and `out/spin-candidate-v5-modern.bmp`.
- Pinned clang-format 22.1.3, structural schema/geometry/whitespace contracts and Git whitespace checks pass. The normal formatting hook is retained, author is FreshD727, no push is performed, and no user game process is terminated.
