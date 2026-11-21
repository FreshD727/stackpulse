# M4 executable room wire (SPD3 revision 3)

Revision 3 adds server lobby discovery, passwords and per-room configuration to the separate SPD3 protocol. It rejects
revisions 1 and 2 explicitly; it does not implement the SPN1 design draft. ENet reliable
UDP channel 0 carries controls and full snapshots. Unreliable sequenced channel 1
carries player input. Rules v6, canonical SPM1/SPCR bytes, simulation and replay
formats are unchanged. This private alpha has no account service or
transport encryption. Bind defaults to loopback.

All integers are unsigned little endian. The envelope is magic `SPD3`, revision
u8=3, tag u8, body length u16, session u64, then exactly body length bytes.
Maximum application packet is 1200 bytes; unknown tags/revisions reject. Hello
uses session zero. Other client packets must match the assigned peer/session.

| Tag | Body fields |
| --- | --- |
| 1 Hello | rules u32=6, preset hint u8 (Fusion0/Modern1/Classic2), recovering u8; optional role u8 (player0/spectator1), create u8, name length u8 and printable ASCII name1..24, code length u8 and code0..16, browse-only u8 |
| 2 Welcome | member u16 (0 provisional recovery or outside room), match u64, private credential[32], room u64, role u8, room preset u8 |
| 3 Ready | ready u8 boolean |
| 4 Lobby | phase u8 (waiting1/countdown2/playing3/results4), countdown u32<=180, match tick u64, room u64, match u64, manager member u16, player capacity u8, spectator capacity u8, code length u8 + code, public room options, member count u8<=24, member records |
| 5 Input | match u64, count u8 1..8, count records (tick u64, sequence u32, held u32) |
| 6 Chunk | transfer u32 nonzero, index u16, count u16, total bytes u32, offset u32, remaining body bytes |
| 7 Snapshot ack | transfer u32 |
| 8 Reconnect | match u64, player u16, credential[32] |
| 9 Reject | <=128 byte ASCII detail |
| 10 Result | match u64, terminal tick u64, canonical state FNV64 u64 |
| 11 Create room | room options including password |
| 12 Join room | role u8, code length u8, code0..16, password length u8, password0..32 |
| 13 Leave room | empty |
| 14 Return to lobby | empty; manager only, Results only |
| 15 Featured | player u16; reserved subscription hint, full match delivery unchanged |

| 16 List rooms | empty; requests fresh directory |
| 17 Directory | room count u8<=8; bounded room records |
| 18 Configure room | room options including password; waiting manager only |
| 19 Operation error | <=128 byte detail; recoverable, connection retained |

Room options: name length u8 + printable ASCII name1..24, player capacity u8
2..8, preset u8 (Fusion0/Modern1/Classic2), start level u8 (0..29 for
Classic/Fusion, exactly0 for Modern), optionally password length u8 + printable
ASCII password0..32. Public options exclude the password field entirely.
Directory record: room ID u64, code length u8 + code16, public options, password
required u8 boolean, player count u8, spectator count u8, room phase u8. Eight
maximum-length records fit one reliable1200-byte packet. Directory queries do
not join rooms. Browse-only registration returns member0/room0 Welcome and the
directory. The old connect overload still autojoins the default lobby. Online
preset hints never constrain room selection; Welcome and Lobby settings determine
snapshot validation. Configure resets every ready flag and rejects outside
Waiting, for anyone except the manager, or capacity below current player count.
An empty configure password retains the current password; a nonempty value
replaces it. This permits updates based on redacted public settings.

Passwords are retained privately on the server, excluded from public View room
settings, directory, snapshots, hashes and logs. This private-alpha transport is
unencrypted: create/configure/join passwords travel in plaintext. Passwords are
access gates, not account credentials; do not reuse sensitive passwords.

Member record: member u16 nonzero in ascending room join order, player u16
(member ID for players, zero for spectators), role u8, ready u8 boolean,
connected u8 boolean, name length u8 and printable ASCII name1..24. Membership
IDs are never reused within a room. Core player IDs are the participating member
IDs and need not be contiguous. Credentials never appear in these broadcasts,
snapshots, core hashes, replays or logs. Room codes contain16 uppercase hex digits.
An empty code on initial connect joins the server's persistent default room;
create requests allocate a separate private room. Empty private rooms are reaped.
Deserted playing rooms retain reconnect slots until the match terminates; then
private rooms are reaped and the default room resets to an empty Waiting epoch.

Server options bound each room to2..8 players and0..16 spectators, and the server
to1..8 rooms. Transport capacity is room capacity times room count plus16
provisional connections. Before countdown, requested player roles consume player
capacity. After countdown starts, requested player joins are rejected with a recoverable
operation error; only an explicit spectator request consumes spectator capacity.
Full rooms and wrong passwords likewise retain registration and directory access. Spectators never enter the core roster and their inputs and
ready controls have no authority. A room starts with at least2 players when all
players are ready. The roster freezes at180-tick countdown; a participant leaving
countdown cancels it and clears ready flags. Manager transfers to the earliest
connected room member. Waiting -> Countdown -> Playing -> Results -> Waiting is
supported; the manager returns a completed match to the lobby. Connected player
roles persist, ready flags clear, disconnected members are removed, credentials
rotate, and the match epoch changes before tick-zero authority of the next round.

Chunk partition is fixed at1120 bytes, offset=index*1120, count exactly
ceil(total/1120), count<=1024, total<=1MiB. Assembly is bounded to one object per
client with a300-tick timeout, identical duplicates accepted and conflicting
parts rejected. Complete object: match u64, rules u32=6, preset u8, FNV64 u64,
then complete canonical SPM1 match bytes. Classic rooms use the Classic core
through its canonical facade; Classic scoring/survival does not apply Modern
garbage attacks. The decoder verifies2..8 sorted players,
canonical state validity, preset and hash before replacing authority. FNV detects
corruption and deterministic divergence; it is not authentication.

Player full snapshots are attempted every3 match ticks (20Hz); spectator full
match snapshots every12 ticks (5Hz), including on late join. Featured selection
is local presentation; a separate20Hz featured-board codec is deferred. One
transfer may be unacknowledged per peer. No snapshot queue is retained; the next
send samples newest authority after ack. An unacknowledged transfer expires after
300 room ticks. The final full state precedes reliable Result on channel0, which
alone confirms results. A new room/match epoch clears client authority and
assembly; snapshot_revision remains monotonic for the Client lifetime.

Tick is the next unsimulated input boundary. Accept current through current+120,
known action mask, first accepted record wins; identical resend is idempotent.
Missing held input persists six ticks and then neutralizes. Disconnect neutralizes
input immediately while core simulation continues through the600-tick forfeit
deadline. Fresh recovery performs Hello, provisional Welcome, then presents a
match/player token. Tokens bind to the unique match epoch and its room/player.
Only a current token for a live, non-eliminated player may recover before expiry;
success atomically replaces an existing peer, reconnects core and rotates the
256-bit OS-random credential. Explicit leave revokes recovery for that slot, including an all-zero token.
Match end invalidates tokens. Old tokens, stale
sessions and cross-room IDs cannot control players.

Per-peer one-second fixed windows cap120 input packets,80 controls (including
acks),256KiB uplink. Three excess packets disconnect. Handshake/recovery timeout
is300 room ticks. A server tick processes at most4096 ENet events. ServerMetrics
exposes cumulative transport bytes, connections, room count and unacknowledged
snapshot count for capacity measurement; it contains no credentials.
