# Issue 7 resolution

Implemented from `issues-7.md` on 2026-11-21.

## Connection and menus

Unreachable servers now show a recoverable error screen with Retry Connection and Back to Main Menu. Escape/controller Back works during connection attempts and after errors; connection attempts time out after five seconds. Immediate hostname/socket failures also remain in the recoverable screen. Ready and Leave Lobby actions are stacked vertically. Decorative horizontal background lines and the header separator were removed.

Classic/Fusion Marathon now opens a starting-level screen (0-29) before launching. The last choice remains saved in the existing settings format, but the level row is removed from Settings. Modern keeps its automatic progression. Practice continues using the last selected level.

## Replay history

The client keeps up to 100 timestamped recordings across Classic, Fusion and Modern. Replay opens a mixed-mode browser sorted newest first with UTC timestamps. Selecting an entry verifies its format, rules, CRC and hashes before playback, then selects its actual ruleset. Corrupt or incompatible recordings report an error without closing the browser.

Each run receives one stable file path, so repeated autosaves update its entry instead of consuming history slots. New recordings use unique timestamped filenames under the preference directory's `replays` folder. Writes validate and commit through a temporary file before pruning the oldest managed entries. Failed commits preserve existing history. Legacy `last.spcr` and `last.spmr` remain selectable in place and count toward the total limit; unrelated files are never pruned. Replay encodings and rules versions are unchanged.

## Audio

All offline modes and playback gain a quiet synthesized lock thud. Multiplayer now plays lock, ordinary line-clear and spin-clear sounds from server-confirmed events. Incoming attacks produce a warning; actual garbage insertion produces a separate impact sound. Canceled garbage does not produce an insertion impact.

SPD3 wire revision 4 carries cumulative presentation counters alongside full snapshots. Clients validate roster identity, bounds and monotonicity. An observer plays only counter increases for the local player or spectator's featured player; duplicate snapshots and prediction corrections remain silent. Joining, reconnecting, changing matches or switching featured players establishes a silent baseline. Catch-up occurrences are coalesced rather than replaying an audio backlog. Movement, rotation, drop and hold retain their existing deduplicated prediction cues.

Use matching updated clients and servers: older wire revisions reject. Core rules and replay formats remain compatible with issue 6.

## Verification

- Windows Release and Linux server/network builds completed with warnings treated as errors.
- Core, protocol/network, client, deterministic example and native smoke tests passed after correcting a Windows timestamp portability issue and making the PCM tests stack-safe.
- Replay library tests cover same-timestamp uniqueness, per-run updates, mixed-mode retention at 100, legacy files, corrupt entries, ignored unrelated files and failed commits without pruning.
- Audio tests check distinct, bounded, finite and tapered PCM cues. Observer tests cover cumulative counts, duplicate snapshots, match changes, reconnection and spectator focus changes. Actual UDP fixtures verify confirmed lock/spin-clear counters, incoming warnings and garbage insertion.
- Native captures cover home, settings, Marathon level selection, populated mixed-mode replay history, vertical lobby actions and connection errors. A real connection attempt to an unbound local port timed out and rendered the recoverable error screen. Generated recordings from all three modes passed native verification.
- Eight native players and one spectator agreed with the server's terminal tick, result and state hash.

Generated captures/reports are in `out/issue7/`. Updated executables and refreshed packages use Release; existing user-running Debug processes were left intact. Package checks use fresh temporary directories on the existing hosts, not clean-machine VMs. Physical controller input and speaker playback were not manually re-tested.
