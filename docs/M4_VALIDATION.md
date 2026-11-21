# M4 rooms, spectators and distribution validation

Date: 21 November 2026. Implementation: `2ad13f5`; packaging/verification tooling: `cc2e752`. M4 extends the authoritative ENet runtime to bounded private rooms, 2–8 players, up to 16 spectators, manager transfer and rematches. Gameplay remains rules v5; Classic SPCR and offline Modern/Fusion SPMR compatibility do not change. The room protocol is SPD3 revision 2 and explicitly rejects revision 1 peers.

## Playing and watching

Start `stackpulse_server --mode fusion`, then use Multiplayer on the native home screen. The controller/keyboard menu selects server, port, room, role and display name. A blank room code joins the default room. Create Private Room shows a code to share; Watch Room joins without player authority. CLI clients accept `--connect HOST --port N --mode fusion|modern --name NAME`, optionally `--create`, `--room CODE` and `--spectate`.

The server defaults to eight player slots, sixteen spectator slots and eight bounded rooms. `--players 2..8 --spectators 0..16 --rooms 1..8` reduces these limits. At least two players, all ready, starts a three-second countdown. Late joins during countdown/play/results become spectators. The earliest connected room member manages the room; management transfers when that member leaves. After a result, the manager returns the room to the lobby for a rematch without restarting the process. Disconnected players retain a 600-tick recovery window; explicit leaving revokes recovery. Empty abandoned private rooms are recycled after the match ends.

Players see their predicted board and seven authoritative opponent thumbnails. Spectators see a featured authoritative board and the other seven thumbnails. Tab/right-stick click cycles player targeting or spectator focus. Escape/Start opens a controller-navigable resume/reconnect/leave overlay while server play continues. F9 reconnects; F10/Back leaves. Labels, names and status supplement color. Address/code/name editing uses keyboard text; default-room create/join/watch and connection presets work with a controller. Physical-controller usability remains a hands-on acceptance check.

## Authority and limits

Members have stable IDs within a room. Spectators cannot ready a player or submit authoritative gameplay, including raw forged input messages. Sessions and credentials remain private and bound to a match/member. Recovery rotates credentials; a token from another room or an explicitly abandoned slot cannot recover a player. Room/match epochs clear client prediction, held inputs, sounds and old authority before a rematch.

Full canonical match snapshots include every board, hidden timer, RNG and garbage packet. Players receive at most 20 Hz, spectators at most 5 Hz. Selecting a spectator feature is local because each update includes all boards. Thus spectator featured updates remain 5 Hz; separate 20 Hz subscriptions/delta thumbnails are a later bandwidth optimization. Each peer has at most one unacknowledged transfer, coalesces to the latest state and times out boundedly. Reassembly is capped at 1 MiB/1024 fixed-partition chunks, sufficient for the tested eight-board maximum canonical state. Room/member and transport quotas are bounded.

The capacity claim applies to one full 8-player/16-spectator room. Multiple isolated rooms are functionally tested; eight simultaneous full rooms have not been load qualified. Public discovery, accounts, encrypted transport, matchmaking/ranking and release operations remain later milestones. Online event replays and authoritative clear/spin outcome audio are still deferred; existing offline replays and audio remain available.

## Automated validation

Windows MSVC Debug/Release builds and the SDL-free Linux GCC build are exercised on this host. Real UDP tests require approved socket access outside the restricted execution environment. Socket tests cover capacity/overflow, late join, spectators without authority, private-room isolation, manager transfer, rematch epochs, room recycling, abandoned-room cleanup, explicit-leave credential revocation, cross-room credentials, reconnect migration and deadlines, malformed packets and maximum chunk bounds. Client tests cover epoch resets and spectator prediction prohibition.

Final Windows CTest passes 5/5. The suite contains 91 core/replay/input cases (725923 assertions), 23 protocol/network cases (4738 assertions), and 18 client/audio/prediction cases (174712 assertions): 132 cases total. Linux SDL-free CTest passes 3/3, including the same 91 core cases and 23 actual-UDP network cases. Pinned clang-format 22.1.3 checks all 44 C++ files. The unchanged global contract checker flags the missing final newline in the new untracked user `issues-5.md`; the same checker passes with only that report excluded from traversal. Its contents remain untouched. Schema/fixture geometry and Git whitespace checks pass for the M4 tree.

`tools/native_room_smoke.py` launches one server, eight actual native clients and one actual native spectator. It compares every terminal tick/result/hash with the server and captures the menu, live player grid, live spectator grid and results roster. Final captures under `out/m4-native-final` were inspected for readable labels, all seven thumbnails, manager/readiness information, target highlighting and clean overlays. All nine clients matched authoritative tick 192, winner, hash `c0ac365912d22c4c`. This exercises SDL rendering and the real transport; it does not establish physical input latency or human controller feel.

`tools/network_matrix.py` exercises actual native clients through independent UDP impairment proxies. M4 adds a 150 ms RTT profile with 20 ms jitter and 1% loss to baseline, latency, jitter/reordering, 5% loss/10% duplication, and combined 8% loss/duplication cases. Results and logs live under `out/m4-network-matrix`. Different profiles may produce different matches because the authoritative server rejects late input; convergence is checked within each match.

All six profiles pass in both presets, twelve native-process scenarios total. Within every scenario, both clients agree with the server on the reliable terminal tick/result/hash.

## Cross-platform determinism

Four full trace-v1 matrices pass: Windows MSVC Debug/Release and Linux GCC 15.2 Debug/Release. Each covers 100 seeds in both Fusion and Modern, 100000 ticks per seed/mode: 20 million steps per configuration, 80 million total. Every canonical state contributes to a chained hash. Terminal states are hashed before restarting an episode to keep the long run exercising live play. Every 997 ticks, complete state roundtrip and subsequent step equality are checked. This is an authored deterministic trace, not proof for every possible input.

All four 200-row CSV files are byte-identical (13616 bytes), SHA-256 `b9fdf925811374bf6aa5bf105351e9b4dbf8fed382f333ad7c7904ab9645afe0`. Reproduce using `stackpulse_determinism_matrix` and `tools/determinism_matrix.py`; the local comparison report is `out/determinism/comparison.json`. The original 100000-tick diagnostic hash remains `feca8ff604d809e5`.

## Capacity timing and soak

Reference host: AMD Ryzen 7 5800X3D, 8 cores/16 logical processors, Windows 11 build 26200, MSVC 19.51, Release. Linux checks use existing Ubuntu 26.04.1 under WSL, GCC 15.2 and glibc 2.43 on the same physical host. Builds and other validation tasks share the host during testing.

The actual-UDP load harness owns a server and 24 ENet client endpoints in one process, keeping eight players and sixteen spectators connected across repeated matches/rematches. Clients receive complete snapshots; every completed match must converge on the server terminal hash for all 24 peers. This measures transport/simulation capacity without 24 SDL renderers. `server_tick` times encompass socket service, room simulation, encoding, sends and flush; process peak memory includes all load clients. Bandwidth counts ENet bytes. Snapshot transfer count is bounded per peer. The Python wrapper measures real monotonic wall time, never equating accelerated ticks with thirty minutes.

The final Windows run passed 1800.0 native seconds (1800.078 wrapper wall seconds), 108000 server ticks, 385 matches and 9240 terminal convergence checks. Server p50/p95/p99 were 5.0/1038.2/2832.1 microseconds; maximum was 5792.4 microseconds. Release p99 meets the provisional 4 ms budget. Average server outbound traffic was 455412 bytes/s, with 819741438 bytes sent and 59992859 received. Peak combined process working set was 11522048 bytes. Connections and simultaneous snapshot transfers never exceeded 24. Exact results: `out/soak/m4-final-modern-1800.json`.

The separate Linux 60-second run also passed: 12 matches, 288 terminal checks, 3600 ticks, p99 815.483 microseconds, maximum 2675.76 microseconds, 447642 outbound bytes/s and 5881856 bytes peak combined working set. It is a short cross-platform check, not a second thirty-minute claim. Debug timing is slower and is not the release performance claim.

## Packages and remaining manual gates

`tools/package_release.py` produces an explicit-allowlist Windows x64 ZIP, app-local required Visual C++ runtime DLLs, dependency licenses, startup/client/protocol guides and SHA-256 manifest. `tools/package_linux_server.py` produces the corresponding SDL-free Linux server/tools archive, manifest, normalized dependencies and required ELF runtime symbol versions. Packaging excludes ROMs, settings, recordings, credentials, test binaries and source build trees. See [PACKAGING.md](PACKAGING.md).

Both archives are extracted into fresh temporary directories and launched with stripped environments; Windows runtime imports are recursively checked. Repeated packaging of identical payloads must produce identical archive hashes. These tests run on the existing Windows/Ubuntu hosts. A separate clean-machine VM, physical controller/accessibility review, monitor-rate/input-latency measurements, and independent WAN soak remain unperformed. Those manual/release gates are recorded rather than inferred from automated captures. The new user `issues-5.md`, screenshots and earlier review artifacts are preserved outside the M4 commits.

Final artifact pairs are byte-identical and pass relocated launch tests. Windows ZIP: 27 files, SHA-256 `22c61b9c54cc677761451e68e9155c089d887732a4e435d5aaaf3a072afff395`; all eight EXEs and every manifest digest match the final Release inputs. Linux server archive: 13 files, SHA-256 `0d8fd2051d55cfbb79f3ec84ce687934ee735257ad6aa7fa1d9c31986d77d034`; all six ELF executables and manifest digests match the Linux build. Artifacts and validation reports are under `out/packages`. A direct Windows-to-WSL UDP smoke could not connect using this host's virtual networking; separate Windows/Linux socket suites and the full cross-platform core hash matrix passed. No firewall or WSL networking configuration was changed to force that optional test.
