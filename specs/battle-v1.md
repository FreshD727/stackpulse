# Battle v1

Status: v1 proposed project balance/order contract; acceptance pending per M0 report, unimplemented and untested in M0. Applies to Modern/Fusion only; Classic Marathon has no garbage. No account, ranked or secure public service is implied.

| Clear | Base score | Base attack |
| --- | ---: | ---: |
| Single | 100 | 0 |
| Double | 300 | 1 |
| Triple | 500 | 2 |
| Four | 800 | 4 |
| T-spin zero | 400 | 0 |
| T-spin single | 800 | 2 |
| T-spin double | 1200 | 4 |
| T-spin triple | 1600 | 6 |

Ordinary zero clear gives 0/0. Score has no level multiplier. Qualifying B2B: four or line-clearing T-spin. If prior B2B true, qualifying clear gives base score*3/2 floored and +1 attack, then sets B2B true. Ordinary line clear sets false; zero clear preserves. Combo initially -1; line clear increments (first=0), adds min(4,floor(combo/2)) attack; zero clear resets -1. No combo score bonus. Perfect clear adds 6 attack if board is empty after a line clear; zero-clear empty boards do not qualify. No perfect-clear score bonus. Add hard/soft drop points separately. All bonuses precede cancellation.

## Packet lifecycle and ordering

Packet=(stable event ID,source u16,recipient u16,creation tick u64,eligible tick u64,remaining rows u16,hole u8). One emitted residual attack makes one packet; hole bounded(10) from source's garbage RNG, drawn only if there is a live target and positive residual attack. All its rows share hole, repeats across packets allowed. Eligibility=creation+30; enqueue recipient at creation+1. Counter overflow is an error. Existing and newly scheduled packets are distinguishable: only packets already incorporated at start of a recipient tick may cancel that tick. Never globally net simultaneous outgoing attacks.

At tick t incorporate arrivals scheduled for t sorted by event ID. For each player in ascending stable ID, simulate actions and lock/clear. Compute attack, cancel oldest pending packets ordered (creation tick,event ID), including ineligible ones; reduce remaining rows and remove zero packets. Emit residual attack as a tentative dispatch. Evaluate hidden-lock loss immediately after clearing and skip spawn boundary if eliminated. At spawn boundary insert up to8 eligible rows from oldest eligible packets, partially consuming a packet when needed; ineligible older packets do not block eligible ones. Each inserted row shifts all stored rows up one, losing if any occupied top row is expelled; append bottom row full except hole. Stop insertion if eliminated. After insertion, spawn and evaluate spawn collision as in rules spec. Preserve canceled packets' IDs in deduplication history. Inputs cannot directly add packets.

After every board's tick is evaluated, resolve eliminations together, then dispatch tentative attacks only from surviving players to surviving opponents. A player eliminated on this tick sends no attack. Validate chosen target at dispatch; fallback among ascending-ID live opponents using source target RNG bounded(count). Duel always targets the sole opponent without an RNG draw. Initial FFA target uses same bounded selection. Manual target-next press cycles ascending IDs among live opponents; if no opponent remains, do not draw any RNG or make a packet. Dispatch order ascending source ID, then source event ordinal; recipient incorporation is next tick. This avoids board iteration affecting same-tick cancellation.

At least one survivor: eliminated players receive shared placement equal to survivor count+1. One survivor wins. Zero survivors: all players alive at tick start share placement1 and match result is draw. Never choose winner by iteration order. Result is generated exactly once, server authority only.

Disconnect sets neutral input immediately and reconnect deadline=t+600 ticks; simulation continues and may eliminate normally. Reconnect with fresh authenticated room-issued credential restores authority via full snapshot; credentials are opaque random transport secrets, not core RNG values. If absent at deadline, forfeit in same simultaneous elimination phase. Pause is offline only.

## Required authored test cases (expected arithmetic; execution pending)

1. First spin double combo0: score1200, attack4, B2B true. Next spin double combo1: score1800, attack5. Ordinary single combo2: score100, attack1, B2B false. Zero clear resets combo while preserving false.
2. Attack6 vs packets A3 eligible and B5 ineligible: A removed, B becomes2, residual0. Attack10 instead leaves residual2 and one new packet. Cancellation before insertion can prevent an otherwise due garbage overflow.
3. Eligible packets A6/B6: insert A6+B2, retain B4; all inserted A rows use A hole and all B rows use B hole.
4. Both players emit4 at t: each receives4 at t+1; neither cancels against other tentative outgoing attack at t. Earliest insertion is a spawn boundary at t+30.
5. Target dies at t: surviving source retargets among surviving opponents; dead source dispatch is suppressed. All remaining players top out together: draw/shared first. One dies: survivor wins.
6. Packet received twice by transport: stable event ID causes one incorporation. Same pending state restored/replayed: no second score/attack/result side effect.

Additional gates: spin single/triple, every B2B/no-clear break, combo cap, perfect-clear stacking, partial packet cancellation, 8-line cap, hidden-row clear rescue, spawn collision, overflow, reconnect expiry and full-capacity error rollback. These are acceptance requirements, not passing test claims.
