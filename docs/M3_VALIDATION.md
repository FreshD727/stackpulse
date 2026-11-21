# M3 authoritative duel validation

Date: 21 November 2026. Implementation commit: `701019b`. M3 adds a dedicated ENet server and two native SDL clients for Fusion or Modern rules v5. The user accepted the current offline Fusion handling before this milestone. Classic reference fidelity and physical input latency remain separate acceptance gates.

## Play a duel

Build with the normal Windows preset. Start `out/build/windows-debug/Debug/stackpulse_server.exe --mode fusion`, then start two instances of `stackpulse_client.exe --connect 127.0.0.1 --mode fusion`. Enter/controller South readies each player. Both ready players start a three-second countdown. Use `--mode modern` on all three processes for Modern. The home Multiplayer entry joins the default localhost server. The server defaults to UDP port 27877 and loopback; explicit `--bind` and matching client address permit LAN testing.

Escape/Start releases local input while the server continues the duel. F9 reconnects using the private session credential; F10/controller Back leaves. Results are server confirmed. Restart the dedicated server for a fresh duel. See [client controls](../apps/client/README.md) and [executable protocol](../src/protocol/README.md).

## Implemented authority and recovery

Clients send only stable tick-addressed held inputs, with an eight-frame rolling resend window. The server admits the current unsimulated tick through 120 ticks ahead, rejects rewrites, carries missing held state for six ticks, then uses neutral input. Shared `match::advance` produces scores, garbage, eliminations and results. A constructor-only validated authored state supports tests; clients cannot upload it.

Only the local board is predicted. Complete snapshots restore its timers, RNG, held state and pending garbage, then replay buffered inputs. History retains 240 frames. Reconciliation emits no repeated sounds; newly sampled movement/rotation/drop/hold sounds are tick deduplicated. Speculative clear and result cues are withheld. Opponent boards and final results use authority. Initial lead is three ticks plus measured ENet RTT allowance, bounded to the admission window.

The separately identified SPD3 revision 1 implements the duel subset of the older SPN1 draft. It uses pinned ENet 1.3.18, bounded 1200-byte packets and 256 KiB chunk assemblies. Reliable full snapshots are offered every three simulation ticks, with one unacknowledged transfer per peer and coalescing to the latest state. Missing acknowledgments time out instead of growing a queue. This reliable full-state path differs from the draft's future transient/delta snapshots.

Sessions and 256-bit reconnect credentials use OS random bytes. Recovery starts a fresh handshake, rebinds the existing player and rotates its credential. Disconnected players remain neutral and forfeit after 600 simulation ticks. Credentials never enter snapshots, hashes, logs or replay files. Wrong-session inputs, incompatible rules, malformed packets and expired/reused credentials are rejected.

## Validation

Sequential Windows MSVC Debug build and the complete five-test CTest suite pass with approved local UDP socket access. The restricted execution environment blocks local UDP, so socket tests and native impairment runs require the approved execution context. The same binaries pass there. Offline unit, client and startup tests also pass in the restricted environment.

The final suite covers 91 core/replay/input cases (725923 assertions), 14 network cases (3488 assertions), and 16 client/audio/prediction cases (174696 assertions). CTest passes 5/5 in 10.19 seconds. Additional lifecycle tests exercise quota slot release, incomplete recovery timeout, fresh-connect view reset and migration from an old active peer. Pinned clang-format 22.1.3 checks all 42 C++ files; schema, fixture geometry and source whitespace checks pass.

`tools/network_matrix.py` launches one actual dedicated-server process and two actual SDL client processes through independent UDP proxies. It checks both client terminal tick/result/hash tuples against the server's explicit authoritative result. Profiles cover baseline, 45 ms one-way delay, 35 ms delay with 30 ms jitter/reordering, 5% loss with 10% duplication, and 50 ms delay with 45 ms jitter plus 8% loss/duplication. These are localhost synthetic tests, not a WAN performance guarantee. Machine-readable results and logs live under `out/network-matrix-final-approved`.

All five profiles pass in both Fusion and Modern: ten actual server/two-native-client scenarios. Terminal simulation ticks range from 89 to 113 for Modern and 227 to 270 for Fusion; both clients agree with the server within each scenario. Different impairment profiles may produce different matches because late input is intentionally rejected. Tests do not require matches under different network conditions to have equal hashes. The combined test found an automated ready-message flood; readiness now sends once while awaiting the reliable acknowledgment. The final online renderer was captured at `out/m3-online.bmp` and inspected for local/opponent boards, previews, hold, result and connection information.

Socket tests cover reconnection beyond prediction history, deadline expiry, matching terminal states, credential rotation/reuse, wrong-session input with a valid-session control, unacknowledged snapshot bounds, incompatible rules, and server-generated spin attack delivery/eligible garbage insertion. Codec tests exercise truncation, invalid lengths/tags, chunk conflicts/partitions, bounded random malformed bytes, and input admission. Prediction tests cover complete restoration, stable resends, stale authority, history expiry and invalid snapshots.

The dependency-free Release core/tools build continues to use `STACKPULSE_BUILD_NETWORK=OFF`; the headless deterministic example retains hash `feca8ff604d809e5`. ENet is excluded from this path. Server sources avoid SDL and provide a Linux/Ninja server preset; Linux compilation/runtime has not been executed on this Windows host. CI includes the Linux build path, but no remote CI run is claimed.

## Remaining scope

M3 supports one two-player duel per dedicated server process. Rematches, larger rooms, spectators, discovery and packaging remain later milestones. Online recordings are disabled because offline input-only replays do not encode authoritative external attacks. Clear/spin outcome audio is withheld online until an authoritative presentation-event stream is added. Public accounts, encrypted transport and public deployment are outside this local/private alpha implementation. Hardware input feel, WAN soak and cross-platform deterministic execution remain unverified.

User-provided issue screenshots and the earlier review artifacts remain untouched and unstaged. Gameplay rules and offline replay formats do not change in M3.
