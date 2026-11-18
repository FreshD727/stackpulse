# Issue 1 resolution

Source: user report `../../../../issues/issue-1.md` and `successful-tuck.png`, copied unchanged into this directory on 2026-11-18 for a durable record. The originals outside the repository were not edited. [Report snapshot](issue-1-original.md), [placement reference](successful-tuck.png).

- Clear cues: one original synthesized cue for 1–3 lines and a distinct four-line cue, selected from clear events.
- Levels: initial level-15 transition is 100 lines, followed by ten-line transitions. Score does not trigger a transition. Clearing across the threshold uses the resulting level for score.
- Tucks: I/S/Z now toggle between two stable pivot states in both rotation directions. A blocked horizontal attempt charges DAS, and retries on the next active tick; entry delay preserves a saturated charge. Screenshot-inspired fixtures cover both the Z rotation/slide/drop route and the J wall-charge slide. Failed scheduled descent still locks immediately.
- Window: fractional aspect-preserving logical scaling fills the available 16:9 area, including maximized windows; other aspect ratios retain necessary bars.
- Settings: Left/Right or D-pad adjusts start level down/up within 0–29; the visible hint explains the controls. Enter/South retains cycling.
- Formatting: checked-in C++ style, pinned formatter wrapper, CI check and explicit opt-in pre-commit hook. See [formatting](../FORMATTING.md).

Rules/state version 3 rejects earlier recordings explicitly; the SPCR container stays v1. Controller gameplay bindings are unchanged. [Reference scope and limitations](../../specs/classic-reference.md) distinguish researched corrections from remaining provisional behavior. There is no claim of complete NES fidelity or new reference-trace approval, and no ROM or extracted assets were used.

## Validation

Pending final integrated build, CTest, contract and formatter checks; results will be recorded before the final documentation commit. Automated layout/audio mapping checks do not replace a human listening or maximized-window handling check.
