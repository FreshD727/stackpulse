# M3 executable duel wire (SPD3 revision 1)

This private alpha protocol is intentionally smaller than the unimplemented SPN1
room/spectator draft in `specs/protocol-v1.md`. It is a separate magic/version,
so no peer can mistake it for implementation of that draft. ENet 1.3.18 is the
actual reliable UDP transport. There are two players, one fixed server preset
(Fusion or Modern rules version 5), and no public discovery, spectators, accounts,
encryption, or room-management API. Bind defaults to loopback.

All integers are unsigned little endian. Envelope is magic `SPD3` (4 bytes),
revision u8=1, tag u8, body length u16, unpredictable session u64. Exactly 16+length
bytes, maximum 1200, no trailing bytes. Unknown tags/revisions reject. Hello uses
session zero. Other client packets must match the assigned peer/session; inputs
are bound to that peer's player. The application has no board/score/attack/result
uplink. ENet channels: reliable ordered channel 0 for control and full snapshot
chunks; unreliable sequenced channel 1 for input. ENet supplies reliable control
sequence/duplicate handling; input record sequences are stable resend identity.

| Tag | Body fields |
| --- | --- |
| 1 Hello | rules version u32=5, preset u8 (Fusion0/Modern1), recovering u8 boolean |
| 2 Welcome | player u16 (0 provisional reconnect), match u64, credential[32] |
| 3 Ready | ready u8 boolean |
| 4 Lobby | phase u8 (waiting1/countdown2/playing3/results4), countdown u32<=180, match tick u64, own ready u8, opponent ready u8 |
| 5 Input | match u64, count u8 1..8, then count (tick u64, sequence u32, held u32) |
| 6 Chunk | transfer u32 nonzero, index u16, count u16, total bytes u32, offset u32, remaining body bytes |
| 7 Snapshot ack | transfer u32 |
| 8 Reconnect | match u64, player u16, credential[32] |
| 9 Reject | bounded <=128 byte ASCII detail, no secret material |
| 10 Result | match u64, terminal tick u64, canonical state FNV64 u64 |

Snapshot chunk payload is at most1120 bytes, fixed partition offset=index*1120;
count must exactly equal ceil(total/1120), count<=256, total<=256KiB. Exactly one
assembly per client; identical duplicates allowed, conflicts abort, no partial
state application. Assembly time limit300 local ticks. Complete object: match
u64, rules version u32=5, preset u8, canonical state FNV64 u64, then canonical
`SPM1` MatchState bytes containing complete modern codec v5 state, pending garbage,
future scheduled attacks, RNG, elimination/results and timers. Decoder validates
full canonical bytes, exactly players1,2, preset and state hash before replacing
client authority. FNV is a corruption/determinism check, not authentication.

The server sends full snapshots at most every3 match ticks (nominal20Hz). Only one
transfer may be unacknowledged per peer, and newer states are coalesced by reading
the newest authority after ack. No per-tick snapshot queue is retained. A peer
that fails to acknowledge in300 room ticks is disconnected. Final full state
precedes reliable Result on the same channel; only that verified Result changes
client presentation to results. Result is generated once per connection.

`State.tick`, `View.server_tick`, and `View.accepted_through` denote the next input
simulation boundary. Frames less than this boundary are past; ack is not a claim
that every future frame up to some maximum was received. Client snapshot revision
increases throughout its object lifetime, including reconnect. Input admission:
unsimulated current tick through current+120 inclusive, known action mask only,
first accepted record wins. Identical tick/sequence/held resend is idempotent;
rewrite rejects. Missing inputs carry last accepted held six ticks, then neutral.
Disconnected players are immediately neutral and shared core continues until
600-tick forfeit deadline. The room never waits for input or rolls back.

Ready players enter180 room tick countdown. Core MatchState is created at tick0
on countdown completion; room/countdown clock never enters input ticks. Preset
and roster freeze at countdown start. Results remain displayed; restart this
M3 dedicated server process for a new duel (return-to-room rematches are deferred).

Secure random uses BCryptGenRandom on Windows and OS `/dev/urandom` on Unix.
Reconnect starts a fresh ENet/Hello session, gets provisional player0, presents
old match/player credential, and succeeds only for a non-eliminated player with the current token; disconnected
players must remain before their600-tick deadline. A valid token also permits
atomic migration from a still-connected old peer (for manual reconnect before
transport failure detection); the old peer is reset and removed first. Success reconnects core state, atomically
rotates credential and rebinds player; sends private Welcome and full snapshot.
Match end invalidates all credentials. Tokens never appear in broadcasts,
snapshots, state hashes, replays or logs. Failure quarantines the client peer.

Admission: max8 transport peers for two slots/provisional handshakes; unbound
handshakes expire300 room ticks. Per peer one-second window caps120 input packets,
80 controls (including snapshot acks),256KiB application bytes. Three excess
packets in a window disconnect and release the slot. At most256 ENet events are
processed per server tick. These simple fixed windows differ from the draft's
token buckets. Public deployment/security hardening is outside M3.
