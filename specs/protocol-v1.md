# Protocol v1 draft

Status: M0 design draft, no codec, ENet server, prediction, replay/network interoperability or security implementation. M0 headless example only demonstrates shared diagnostic core. Proposed Modern/Fusion codec2 has exact fields/encoding in `core-api-v1.md`; P06 must review it and add golden bytes before implementing snapshots. This document freezes envelope/limits for review; version1 is not advertised by M0.

## Encoding, channels and validation

All integers unsigned little-endian unless explicitly signed. Booleans u8 are exactly0/1. Fixed arrays have no implicit length. `blob`=u32 byte count then bytes; `text`=u16 byte count then UTF-8 without NUL, invalid UTF-8 rejected. No padding or trailing bytes. IDs: player u16 (0 reserved), room/match/session u64 (0 reserved except handshake session). Rules identity is 32 SHA-256 bytes. Every enum rejects unknown tags.

Envelope: magic 4 ASCII `SPN1`, major u16=1, minor u16=0, type u16, payload length u16, session u64, packet sequence u32. Header24 bytes. Total ENet application datagram <=1200 bytes (payload<=1176). Client handshake Hello uses session0. Handshake Reject also uses session0 and is accepted only while awaiting Welcome. Welcome envelope carries the newly assigned session, matching its assignedSession payload; server assigns a fresh unpredictable session. Reconnect requires a successful Hello/Welcome first and uses the new session. Sequence comparison uses modular u32, forward distance<2^31. New sessions reset sequence. Unknown major/minor rejects before readying; no silent fallback. Duplicate accepted message is idempotent; unknown session drops. Reliable control channel0; sequenced-unreliable channel1 inputs/acks/ping; sequenced-unreliable channel2 partial snapshots. Do not send reliable traffic on transient channels.

Logical records >1176 bytes use only reliable FullSnapshotChunk or RoomStateChunk below. Max reassembled object256KiB; max256 chunks; timeout300 ticks; max2 concurrent assemblies per connection. Validate total size/count/offset before allocating; chunk offsets must partition complete object without overlap or gaps. Identical duplicates allowed; conflicting duplicates abort assembly. All failures produce bounded error/close; no partial-state mutation. Compression absent in v1.

## Message tags and payload fields in order

| Tag / message | Channel | Fields / limits |
| --- | --- | --- |
| 1 Hello | 0 | buildCompatibility u32, rulesDigest[32], displayName text<=32 bytes, role u8(player0/spectator1) |
| 2 Welcome | 0 | assignedSession u64, player u16, serverTick u64, maxPlayers u8<=8, maxSpectators u8<=16 |
| 3 Reject | 0 | reason u16, detail text<=128 |
| 4 CreateRoom | 0 | requestID u32, rulesDigest[32], playerCapacity u8 2..8, spectatorCapacity u8 0..16 |
| 5 JoinRoom | 0 | requestID u32, room u64, roomCode text<=16, role u8 |
| 6 LeaveRoom | 0 | room u64 |
| 7 SetReady | 0 | room u64, ready u8 |
| 8 RoomStateChunk | 0 | transfer u32,index u16,count u16,totalBytes u32,offset u32,data blob<=1156 |
| 9 StartMatch | 0 | room u64,match u64,startServerTick u64,seed u32,rulesDigest[32],rosterCount u8<=8, rosterCount player IDs u16 |
| 10 InputBatch | 1 | match u64,player u16,count u8 1..8, then count (tick u64,sequence u32,held u32) |
| 11 InputAck | 1 | match u64,player u16,simulatedThrough u64,lastAcceptedSequence u32,missingStreak u8<=7,rejectedTick u64,rejection u8(0none/1late/2future/3rewrite/4invalid) |
| 12 StateSnapshot | 2 | match u64,tick u64,player u16,baseTick u64,codec u16,stateDigest[32],data blob<=1104 |
| 13 FullSnapshotRequest | 0 | match u64,knownTick u64,reason u8(0join/1mismatch/2historyExpired/3reconnect) |
| 14 FullSnapshotChunk | 0 | transfer u32,index u16,count u16,totalBytes u32,offset u32,data blob<=1156 |
| 15 Ping | 1 | nonce u32,clientMonotonicUs u64 |
| 16 Pong | 1 | nonce u32,clientMonotonicUs u64,serverMonotonicUs u64,serverTick u64 |
| 17 MatchResult | 0 | match u64,endTick u64,draw u8,count u8<=8, then count (player u16,placement u8,reason u8),stateDigest[32] |
| 18 Reconnect | 0 | match u64,player u16,credential[32],knownTick u64 |
| 19 Error | 0 | requestID u32,reason u16,detail text<=128 |
| 20 ReconnectCredential | 0 | match u64,player u16,credential[32],expiresTick u64 |

Chunk data max1156 includes the blob's four-byte length inside payload limit: chunk metadata16+blob prefix4+data1156=1176. Snapshot fixed metadata60+blob prefix4 permits1112; this draft deliberately caps data at1104 for eight bytes of reserved headroom (not transmitted). Snapshot codec0 is reserved and rejected. Proposed codec2 is the complete resumable Modern/Fusion encoding in the core spec; executables must reject it until implemented and reviewed. Other unregistered codecs reject. Native memory and board-only snapshots are prohibited. Codec2 encodes a complete MatchState only; it has no partial/player/delta subtype. StateSnapshot can carry it only when the complete canonical bytes fit this cap; otherwise use reliable FullSnapshotChunk, never truncation. For codec2 set player=0 and baseTick=tick to denote the complete match. A future compact player snapshot needs a separately registered codec with complete context and golden fixtures before the proposed 20Hz transient path is enabled.

RoomState reassembly object: room u64,revision u32,phase u8(0Waiting/1Countdown/2Playing/3Results),manager u16,rulesDigest[32],count u8<=24, followed by count entries (player u16,role u8,ready u8,connected u8,name text<=32). FullSnapshot object: match u64,tick u64,codec u16,rulesDigest[32],stateDigest[32],state blob<=250000,ackCount u8<=8, followed by ack tuples (player u16,tick u64,sequence u32). Decoder rejects unsupported codec before applying. Match reasons:0survival,1spawn,2hiddenLock,3garbageOverflow,4forfeit. Reject/Error reasons:1version,2rules,3full,4malformed,5state,6quota,7credential,8unavailable.

## Tick admission and recovery

Server owns room clock and runs60 battle ticks/s at a fixed playout deadline without waiting for players. Client initial lead3 ticks; only accepted input affects simulation. Accept frames for unsimulated tick T through T+120 inclusive; reject earlier as late and larger as future. Deduplicate per player/tick; first accepted frame wins; conflicting rewrite rejected even with larger sequence. Bind player to session. A batch may contain independent accepted/rejected frames; ack first rejected tick in batch order. Inputs contain held state only; derive gameplay edges server-side. Missing frame carries last accepted held for up to6 consecutive missing ticks, then neutral on the seventh until new input. Do not globally rollback server. Client sends rolling8 frames per tick; stable tick data cannot be rewritten by resends.

Snapshots20Hz (every3 ticks), thumbnails5Hz (every12). Client retains>=120 ticks complete history, restores full authoritative state at snapshot tick, then replays later local inputs; mismatch outside history requests full resync. Stable event IDs suppress repeated presentation effects; final results only on authoritative reliable message. Ping samples estimate tick offset from RTT midpoint; no wall-clock timestamps enter simulation. On StartMatch issue ReconnectCredential privately to each participating session. Credential is256 random transport bits, bound to match/player. expiresTick is UINT64_MAX while connected; match end always invalidates it, and disconnection caps server-side validity at the600-tick deadline without requiring another delivery. On successful Reconnect atomically rebind the old player to the new session (remove the provisional handshake identity), invalidate the old credential, issue a replacement ReconnectCredential and send a full snapshot; never include credentials in room broadcasts, replays, hashes or simulation state. Reconnect limit600 ticks; credential and full snapshot required. This private alpha transport design does not provide account authentication/encryption; public service requires a separate reviewed transport design.

Room phase WaitingÃƒÂ¢Ã¢â‚¬Â Ã¢â‚¬â„¢Countdown(180 ticks)ÃƒÂ¢Ã¢â‚¬Â Ã¢â‚¬â„¢PlayingÃƒÂ¢Ã¢â‚¬Â Ã¢â‚¬â„¢ResultsÃƒÂ¢Ã¢â‚¬Â Ã¢â‚¬â„¢Waiting; rule/roster changes clear ready and are rejected after Countdown begins. Late joins spectate. Manager transfers to earliest still-connected room join-order. Admission max8 players/16 spectators, initial supported sizes2..8. Handshake timeout300 ticks. Per-peer quotas: InputBatch<=120/s, Ping<=4/s, other control<=20/s, full snapshot request<=2/s, application bytes<=256KiB/s; token buckets with one second burst. Reject persistent quota violation and disconnect after3 violations in60 ticks. Server never trusts client board/score/attack/outcome. Malformed lengths and bounds require decoder/fuzz tests before any network milestone claim.
