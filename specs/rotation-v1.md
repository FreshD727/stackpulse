# Project rotation tables v1

Status: implemented M2 Modern/Fusion table set `project-90-v1`; automated geometry/kick/spin checks pass. See `docs/M2_VALIDATION.md` for scope and remaining human checks. This is an explicit project contract, not a fidelity claim for an external game. Classic rotation remains unknown. Coordinates use y down; each tuple is (x,y) in the piece-local grid, origin added afterward. Cell order is irrelevant; orientation order is 0,R,2,L. CW advances one, CCW subtracts one modulo four. Pivots: JLSTZ (1,1); I (1.5,1.5); O (1.5,0.5). Integer geometry is authoritative; pivots explain it.

| Piece | 0 | R | 2 | L |
| --- | --- | --- | --- | --- |
| I | 0,1 1,1 2,1 3,1 | 2,0 2,1 2,2 2,3 | 0,2 1,2 2,2 3,2 | 1,0 1,1 1,2 1,3 |
| J | 0,0 0,1 1,1 2,1 | 1,0 2,0 1,1 1,2 | 0,1 1,1 2,1 2,2 | 1,0 1,1 0,2 1,2 |
| L | 2,0 0,1 1,1 2,1 | 1,0 1,1 1,2 2,2 | 0,1 1,1 2,1 0,2 | 0,0 1,0 1,1 1,2 |
| O | 1,0 2,0 1,1 2,1 | 1,0 2,0 1,1 2,1 | 1,0 2,0 1,1 2,1 | 1,0 2,0 1,1 2,1 |
| S | 1,0 2,0 0,1 1,1 | 1,0 1,1 2,1 2,2 | 1,1 2,1 0,2 1,2 | 0,0 0,1 1,1 1,2 |
| T | 1,0 0,1 1,1 2,1 | 1,0 1,1 2,1 1,2 | 0,1 1,1 2,1 1,2 | 1,0 0,1 1,1 1,2 |
| Z | 0,0 1,0 1,1 2,1 | 2,0 1,1 2,1 1,2 | 0,1 1,1 1,2 2,2 | 1,0 0,1 1,1 0,2 |

Test offsets left to right against the target orientation at old origin + offset; commit first collision-free result and record kick index 0..4. Failure preserves origin/orientation/last-rotation and consumes no lock reset. O changes orientation with only (0,0) test; it counts as successful rotation if valid, but never as T-spin. No 180 action.

| Transition | JLSTZ offsets | I offsets |
| --- | --- | --- |
| 0->R | (0,0) (-1,0) (-1,-1) (0,2) (-1,2) | (0,0) (-2,0) (1,0) (-2,1) (1,-2) |
| R->0 | (0,0) (1,0) (1,1) (0,-2) (1,-2) | (0,0) (2,0) (-1,0) (2,-1) (-1,2) |
| R->2 | (0,0) (1,0) (1,1) (0,-2) (1,-2) | (0,0) (-1,0) (2,0) (-1,-2) (2,1) |
| 2->R | (0,0) (-1,0) (-1,-1) (0,2) (-1,2) | (0,0) (1,0) (-2,0) (1,2) (-2,-1) |
| 2->L | (0,0) (1,0) (1,-1) (0,2) (1,2) | (0,0) (2,0) (-1,0) (2,-1) (-1,2) |
| L->2 | (0,0) (-1,0) (-1,1) (0,-2) (-1,-2) | (0,0) (-2,0) (1,0) (-2,1) (1,-2) |
| L->0 | (0,0) (-1,0) (-1,1) (0,-2) (-1,-2) | (0,0) (1,0) (-2,0) (1,2) (-2,-1) |
| 0->L | (0,0) (1,0) (1,-1) (0,2) (1,2) | (0,0) (-1,0) (2,0) (-1,-2) (2,1) |

Spin classification happens before merging cells. T only: last successful manipulation was rotation and >=3 occupied diagonal corners around world pivot origin+(1,1). Outside storage is occupied. Vertical movement preserves marker; successful lateral movement clears it. Failed actions preserve it. All qualifying spins are full; minis deferred. Rotation on empty board at origin(3,0) must succeed index0 for all pieces/transitions. T 0 at (3,18) CW on empty board: tests0,1 fail floor, index2 succeeds at (2,17). T R at (-1,0) CCW: index0 fails left wall, index1 succeeds at (0,0). Fill every board cell except active cells: a rotation with all candidate destinations blocked must fail with exact unchanged piece state.

Required spin test recipes (authored scenarios, not implemented fixture results): T R at origin(3,17); pivot(4,18); occupied corners (3,17),(5,17),(3,19). At lock rotation marker true=>spin, false=>not-spin; removing (3,19)=>not-spin. Complete row19 except x4 gives spin single; additionally complete row18 except x4,x5 gives double; additionally complete row17 except x4 gives triple. Active R cells fill each gap. Test a prior CW from 0 to R with these obstacles; if rotation is blocked, start from the documented R state with marker true for classification-only fixture. An independently authored input-driven triple route is specified below; full gameplay execution and every ordered kick selection remain M2 gates. Walls/corners, failed rotation, lateral invalidation, gravity preservation and hold reset each require dedicated fixtures before implementation acceptance.


## Input-driven triple fixture

`tests/fixtures/rotation/t-spin-triple-v1.json` encodes this proposed Modern scenario. Rows17,18,19 are filled except gaps [4], [4,5], [4] respectively; one additional block is at(4,15); all other cells empty. Start T0 at origin(4,15), grounded, lock elapsed0/reset0/gravity0, last-rotation false, combo-1/B2B false. Press CW on tick0, neutral on ticks1..29. The five CW candidate origins are(4,15),(3,15),(3,14),(4,17),(3,17). First four collide respectively at(5,17),(4,15),(4,15),(5,17); fifth succeeds. T R at(3,17) occupies(4,17),(4,18),(5,18),(4,19), is grounded, and has four occupied corners. At lock, all three rows clear, full spin score1600/base attack6; block(4,15) survives at(4,18), so no perfect-clear bonus. Lock is proposed tick29 under the rules-v1 timer contract. Integrator independently checked initial validity, all five candidates, groundedness, corner occupancy and row fill with a geometry-only script. This validates the route's geometry; no full gameplay tick execution is claimed. Full fixture execution remains M2 integration work.

## M2 execution record

The authored JSON board and all thirty held-input records now execute against `modern::step`: CW on tick0 selects kick4, the Modern lock occurs on tick29, three rows clear, score1600/attack6 and the extra block survives at(4,18). Equivalent authored one/two-row cavities execute as playable fixtures in both presets; Fusion locks on tick17. Table equality covers all eight directed transitions, separate I/JLSTZ offsets and O behavior. Forced-obstacle tests select every geometrically attainable candidate: 236 of 240 piece/transition/index combinations for I/J/L/S/T/Z. The four impossible candidates are T0->R and T0->L index3, T2->R and T2->L index2: preserving both old active cells and desired target prevents blocking every earlier candidate. These exceptions are pinned assertions, not skipped warnings. Full/mini distinction remains intentionally full-only.
