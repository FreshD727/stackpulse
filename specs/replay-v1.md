# Replay formats: implemented provisional and future draft

Modern/Fusion current implementation note: the issue-3 v3 rules/state/replay contract supersedes conflicting M2/v1 behavior in this document. See [the current issue-3 resolution](../docs/issues/issue-3-resolution.md) for implemented defaults and compatibility. Classic/SPCR contracts remain unchanged. Proposed formats below are not all implemented.

Status: updated 2026-11-18. M1 implements the separate **SPCR provisional Classic v1** format described first. The later **SPR1 draft** remains unimplemented. The two are not compatible, and SPCR is not fulfillment of the future multiplayer/Modern replay contract.

## Implemented SPCR v1

Implementation: `include/stackpulse/replay.hpp`, `src/replay/replay.cpp`, and `tools/replay_verify.cpp`. File suffix is conventionally `.spcr`; the decoder recognizes bytes, not the filename. Rules are the exact ASCII `rules` literal in `src/replay/replay.cpp`, beginning `classic-provisional-v3;`. The literal embeds tick rate, geometry, per-piece spawn orientation (T/J/L=2, I/O/S/Z=0), rotation, RNG, gravity, DAS, delays, scoring, levels, top-out, action bits and tick order. Exact equality is required; no local preset substitution is allowed. The SPCR container remains v1; its embedded rules and canonical state version are now 3. Older provisional rules/states reject as incompatible because geometry, DAS, transition thresholds and scoring differ. This provisional identity does not certify reference fidelity.

All integers are little endian with explicit widths and no native padding:

| Order | Field |
| --- | --- |
| 1 | Magic: four ASCII bytes `SPCR` |
| 2 | Major u16=1; minor u16=0 |
| 3 | Rules byte length u32; initial-state byte length u32; input frame count u32; hash count u32 |
| 4 | Exact rules bytes, then complete initial canonical state bytes |
| 5 | One held action mask u32 per frame, in simulation order |
| 6 | Hash entries: relative frame index u32 followed by canonical state FNV-1a hash u64 |
| 7 | CRC-32/ISO-HDLC u32 over every preceding byte |

The initial canonical state is exactly 268 bytes: rules version u32; tick u64; score u64; RNG, held, lines, level, start level (five u32); board (200 u8); active piece u8; rotation u8; x i32; y i32; next piece u8; gravity, DAS and entry timers (three u32); direction i32; game-over u8 (0 or 1). Piece IDs are I/O/T/S/Z/J/L = 0..6; board 0 is empty and 1..7 identifies the piece. Signed fields use their 32-bit representation. Decode validates state ranges and resumability before publishing the result. This codec is distinct from all codecs in the future draft below.

Action bits are left=1, right=2, down=4, clockwise=8, counterclockwise=16. Unknown bits reject the replay. Input frame i advances initial-state tick+i to tick+i+1. Initial state need not be tick zero, so practice and mid-piece states can be recorded with all hidden timers and held context intact. Tick wrapping is forbidden. The total-lines counter saturates at UINT32_MAX rather than wrapping; the rules identity includes this policy. Replays with the earlier provisional identity lacking that policy reject as incompatible.

Hash entries occur at relative frame 0, every 600 frames, and the final frame if it is not already a multiple of 600. Thus hash_count=floor(frame_count/600)+1+(frame_count%600!=0). An empty replay contains just the frame-0 hash. Entries hold hashes, **not resumable snapshots**. Decode simulates from the initial state and verifies every index/hash; arbitrary checkpoint seeking is not implemented. FNV-1a uses offset 14695981039346656037 and prime 1099511628211 over canonical state bytes. CRC uses reflected polynomial 0xedb88320, init/xorout 0xffffffff. These checks detect corruption and divergence; they provide no authentication.

Limits: at most 1,000,000 input frames and 8 MiB per file. Header lengths, exact total size, rules/version identity, initial codec, masks, hash indexes/hashes, and whole-file CRC must validate. Truncation and trailing data reject. `decode` publishes the replay only after all verification succeeds. `encode` validates by simulating the recording first. The headless verifier exits 0 on success, 1 for a rejected replay, or 2 for invalid CLI usage. Run `stackpulse_replay_verify FILE.spcr`.

M1 tests cover marathon/practice round trips, full-state restoration, file I/O, empty recordings, every truncated prefix and single-byte corruption of a sample, resealed malformed headers/rules/state/input/hash data, limits and tick wrap. A fixed independent golden file, cross-platform fixture hashes, seek support, SHA-256 identity, external events and the future record stream are not established by SPCR.

## Future SPR1 draft (unimplemented)

The following is the original proposed container contract; acceptance remains pending per the M0 report. Codec1 describes the M0 diagnostic only and is not a Classic/gameplay codec. Proposed complete Modern/Fusion codec2 is specified in `core-api-v1.md`; review, implementation and golden bytes remain prerequisites. References below to gameplay, seeking and validation are requirements for that future format, not implemented SPCR features.

## Binary container

Little endian, exact widths, no native padding. Header fields in order: magic[4] ASCII `SPR1`; major u16=1; minor u16=0; engineCompatibility u32=1; stateCodec u16 (1 diagnostic, 2 proposed Modern/Fusion, unsupported until implemented); flags u16=0; tickRateNumerator u32; tickRateDenominator u32; seed u32; rulesLength u32; rulesDigest[32]; rosterCount u8; roster IDs[rosterCount] u16; exact canonical UTF-8 rules bytes[rulesLength]; initialStateLength u32; initialState bytes; initialStateDigest[32]. Digests SHA-256. Header numeric rate fields nonzero; diagnostic60/1 and rosterCount1, ID1. Gameplay rate from embedded rules. Rules<=64KiB; initial state<=256KiB. Diagnostic rules literal is `{"diagnostic_version":1}` (24 bytes), codec1 initial state exactly the36-byte canonical diagnostic state. No dependency on local preset filenames or ROM. Header digest checks rules bytes and initial state independently.

Records: type u8,flags u8=0,reserved u16=0,payloadLength u32,tick u64,payload bytes,CRC u32. CRC is CRC-32/ISO-HDLC over the16-byte record prefix plus payload: reflected polynomial0xedb88320, init0xffffffff, xorout0xffffffff. Check vector ASCII123456789 =>0xcbf43926. Max payload256KiB. Unknown type/flag, impossible lengths or bad CRC rejects file before use; parser must not allocate from unchecked lengths. No compression in v1.

| Type | Payload |
| --- | --- |
| 1 Input | player u16,sequence u32,held u32; diagnostic additionally pressed u32,released u32 |
| 2 External | gameplay only: source u16,event ID(match u64,tick u64,player u16,ordinal u16),eventTag u16,data blob(u32 length+bytes)<=4096; eventTag1=packet arrival (data is canonical Packet),2=disconnect (data player u16),3=reconnect (data player u16), per core scheduled-record contract |
| 3 Checkpoint | stateCodec u16,stateLength u32,canonical state bytes,stateDigest[32] |
| 4 StateHash | stateDigest[32] (SHA-256 canonical state) |
| 5 End | lastStateDigest[32],reason u8(0complete/1userStop/2error) |

Record tick is pre-tick for Input/External, post-tick state index for Checkpoint/StateHash/End. Initial state tick is0. For N steps, Input ticks0..N-1, resulting state tickN. Sort records by tick, then Checkpoint,StateHash,External,Input,End. Checkpoint at tick t represents the pre-input state for tick t; streaming verification checks it before advancing tick t. Exactly one input per roster player per simulated tick, ascending ID; record authoritative accepted held inputs including carried/neutral missing frames rather than relying on network arrival timing. Diagnostic stores exact validated edge masks. External events sort by stable ID. End terminates stream; trailing bytes forbidden. End is emitted only after all inputs tick<End.tick. A session error records last unchanged state with reason2.

Checkpoints every600 ticks, plus optional start/end; a duplicate checkpoint tick is invalid. Hash at least every60 ticks. Checkpoint contains complete state and accepted-input context necessary to resume; board-only snapshots are invalid. Full replay seeking verifies embedded rules, codec, checkpoint SHA and resumes at matching tick; verification from tick0 additionally checks checkpoint equality against recomputation. StateHash does not substitute for an input or state snapshot. Diagnostic FNV hashes may be reported separately but are not replay's SHA digest field.

## Limits and compatibility

Maximum file256MiB, records10000000, roster8, tick span10000000, monotonic record ticks, no duplicate inputs/player/tick. Diagnostic tick wrapping is supported by core arithmetic but excluded from this bounded replay format. Gameplay events/checkpoints require a reviewed implemented codec before use; proposed codec2 is not accepted just because engine version matches. Major/minor/engine mismatch rejects with an explanatory error; no silent rules substitution, unknown field skipping, or approximate playback. A future migration tool must emit a new replay and preserve source identity. Corruption/truncation never produces a verified result. Diagnostic codec1 state uses valid version/range/mask checks and seed normalization from core API.

Required P04 tests: golden header/record bytes; input/checkpoint round-trip including hidden timers and garbage for gameplay; seek vs replay-from-start equality; Windows/Linux hashes; malformed length, CRC, unknown enum/codec, oversized rules, duplicate ticks, invalid edge masks, truncated records, missing End and trailing bytes. M0 supplies no passing replay claim.

For gameplay External records, record.tick is deliveryTick; source must equal event ID source. Reconstruct the exact Scheduled record from deliveryTick,event ID,eventTag,data. Only these authoritative lifecycle/arrival causes are recorded as inputs; semantic lock/clear/attack events are recomputed outputs. Codec2 initial-state bytes embed match/roster/tick/rulesDigest and must agree with header and tick0. A diagnostic header has rosterCount1; codec2 has rosterCount2..8.

## Implemented M2 SPMR v1 (separate from future SPR1)

`modern_replay.hpp` / `modern_replay.cpp` implement single-board Fusion/Modern recordings. Existing SPCR Classic encoding/decoding is unchanged. SPMR header: ASCII SPMR, u16 major1, u16 minor0, u32 rulesLength, u32 stateLength, u32 frameCount, u32 hashCount. Then exact UTF-8 embedded project rules identity bytes, full canonical Modern initial state, frameCount u32 held masks, hashCount records (u32 relative-frame index,u64 FNV1a state hash), final u32 IEEE CRC32 of all preceding bytes. Every integer is little endian. Hash records are initial index0, each600 frames and final index if different. Relative frame indices support nonzero initial ticks. Rules identity includes preset constants/table versions; canonical JSON SHA256/rules loading remains deferred.

Limits: 8MiB file, 4096 rules bytes, 262144 initial-state field (Modern decoder further caps50000), one million frames. Reject wrong magic/version/rules, oversized or inconsistent lengths, trailing/truncated bytes, invalid state/input, counter exhaustion, CRC or intermediate/final hash mismatch. Failed decode preserves output. Topout frames continue advancing simulation tick/accepted held state consistently; no new pieces or rewards. SPMR has no lifecycle/external-event records or seek index: practice garbage is part of an initial-state restart. Headless match snapshots are resumable, but native match replays and future SPR1 networking records remain M3 work.
