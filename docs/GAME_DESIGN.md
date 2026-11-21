# Game design and technical plan

Status: design baseline, 17 November 2026; updated 20 November 2026. Offline Classic/Fusion/Modern run. The user accepted the current Fusion handling on 20 November 2026; this is a gameplay acceptance, without a hardware latency or NES fidelity claim. See M2_VALIDATION.md and M3_VALIDATION.md for executed milestone checks and remaining gates. Based on `../idea.md`.
Current implemented Fusion handling and Modern/Fusion codec compatibility are defined by [issue-3 resolution](issues/issue-3-resolution.md), superseding conflicting design defaults below.

Companion: [development work packages](DEVELOPMENT_PLAN.md).

## 1. Product direction

A native desktop falling-block game that preserves the demanding, deliberate handling of classic NES play while offering optional modern rotation, spin rewards, and competitive multiplayer. Retro presentation, excellent controller support, and predictable input response are the priorities.

The central promise is **classic handling you can trust, with modern competition you can choose**. Authenticity must be measured, not inferred from how the graphics look.

Decisions proposed by this plan:

- Windows x64 client first; Linux headless dedicated server from the first network milestone. Linux desktop and macOS follow once the input/rendering path is validated.
- Three versioned presets: Classic, Fusion, and Modern. Fusion is the eventual flagship, but Classic is the first implementation and validation target.
- One deterministic simulation library shared by client, server, replay player, and tests.
- Private online rooms and spectators before accounts, public matchmaking, or ranks.
- Initial supported room sizes: 2–8 active players and 16 spectators. Scale toward 32 and then 99 only after load tests. “Any amount” becomes configurable capacity with measured limits, not an unlimited-player promise.
- Original branding, artwork, music, sound effects, and interface composition. No ROM dependency in builds or downloads.

These are planning defaults, not statements that the user already chose every value. Values marked provisional require the milestone checks below.

## 2. Name suggestions

| Name | Positioning |
| --- | --- |
| **Stackpulse** | Preferred working title: rhythmic inputs, readable, works for solo and versus |
| **Lockshift** | Technical, competitive, emphasizes movement and placement |
| **Stack Circuit** | Arcade and tournament character |
| **Drop Relay** | Friendly, multiplayer-oriented |
| **Wellstorm** | Competitive pressure and garbage battles |
| **Block Signal** | Retro electronic character |
| **Afterdrop** | Short, atmospheric, useful for a stronger visual identity |
| **Stack Rivals** | Immediately communicates competition |

Use `stackpulse` as a temporary internal identifier. These are creative suggestions; store, domain, and trademark availability have not been checked. Choose a public name after a separate clearance search. Avoid branding that suggests an official Tetris product.

## 3. Player experience and scope

Primary loop: read the next piece, position and rotate, build efficiently, clear lines, survive increasing pressure. In versus play, good clears cancel incoming garbage before sending attacks to an opponent.

Launch modes:

| Mode | Purpose | Delivery |
| --- | --- | --- |
| Classic Marathon | Score, lines, levels, familiar handling, one next piece | First playable |
| Practice Lab | Input timeline, piece setup, restart seed, frame step, handling comparison | First playable, expanded later |
| Fusion Duel | Classic horizontal cadence with modern spin opportunities | Multiplayer alpha |
| Modern Duel | Faster handling and familiar modern conveniences | Multiplayer alpha |
| Free-for-all | 3–8 players, one target per attack, last survivor | MVP |
| Spectate/replays | Follow a player, inspect the room, review matches | MVP |
| Ranked/public matchmaking | Stable rules, accounts, ratings, regional queues | After MVP |
| Large battle rooms | 32/99-player experiments | After load and balance validation |

MVP excludes progression systems, cosmetics economy, chat, teams, AI opponents, workshop scripting, console certification, and faithful reproduction of original software crashes. Practice tools and offline play work without an account or server.

## 4. Presets and customization

A single slider would hide incompatible rule changes. Provide named presets, then grouped advanced controls. Every modified preset becomes Custom with a human-readable difference list and content hash.

| Setting | Classic | Fusion v1 (provisional) | Modern v1 (provisional) |
| --- | --- | --- | --- |
| Horizontal handling | Measured NES behavior | Classic cadence | DAS 100 ms, repeat 33 ms |
| Rotation | Measured NES rotation, no added kicks | Modern 90-degree rotation/kicks | Same modern rotation/kicks |
| Lock behavior | Measured gravity-step locking | 300 ms delay, max 8 resets | 500 ms delay, max 15 resets |
| Randomizer | Measured classic generator | Seeded seven-bag | Seeded seven-bag |
| Preview | 1 | 1 | 5 |
| Hold | Off | Off | Once per active piece |
| Hard drop | Off | Off | On, immediate lock |
| Ghost | Off | Off by default | On |
| Spin rewards | Off | On | On |
| Combo/back-to-back rewards | Off | On | On |
| Garbage | Off in Marathon | On in versus | On in versus |
| Timing clock | Measured NTSC reference clock | 60 ticks/second | 60 ticks/second |

Fusion intentionally changes classic rotation and locking to allow the planned T-spin single/double/triple play. It is not an exact NES simulation. The first playtest must establish whether its longer lock delay still feels sufficiently classic; keep the preset versioned so tuning does not silently invalidate replays.

Classic scope initially targets one identified NTSC revision. PAL is a distinct future preset. Exact spawn positions, timer ordering, input sampling, randomization, score behavior, level transitions, and frame clock remain validation work; do not substitute guessed constants and label the result exact.

Advanced settings groups:

- Handling: initial movement, DAS delay, repeat interval, charge/reset behavior, opposite-direction policy, soft drop.
- Rotation/locking: rotation table, kick table, permitted rotation actions, lock delay and reset limit.
- Rules: gravity curve, entry delay, clear delay, randomizer, preview, hold, ghost, top-out policy.
- Battle: attack table, cancellation, garbage delay/cap/hole policy, target selection.
- Presentation/accessibility: palette, block patterns, effects, audio levels, text scale.

One match has one shared simulation ruleset. Device bindings and visual accessibility remain personal. Handling values that affect gameplay are locked by competitive presets; custom rooms can explicitly opt into per-player handling with an Unranked label and a hashed per-player configuration. Reject unsupported combinations with an explanation rather than silently changing them.

## 5. Mechanics specification

### Board and simulation

Use a 10-column, 20-visible-row playfield. Modern/Fusion storage includes 20 hidden rows above it; coordinates are x=0..9 and y=-20..19. Classic uses a rules-specific spawn/collision adapter, verified in the reference phase; this storage layout must not impose modern top-out behavior on Classic.

Represent all seven four-cell pieces as integer coordinates and explicit rotation states. Keep geometry, spawn coordinates, and kick tables in versioned rules data. Modern v1 targets SRS-style 90-degree behavior with separately defined I and JLSTZ tables and explicit O behavior. No 180-degree rotation in v1. Before implementation, freeze the complete tables, coordinate conventions, and representative T-spin triple fixtures in `specs/rotation-v1.md`; the phrase “SRS-style” is not itself an executable contract.

Modern/Fusion tick order:

1. Incorporate scheduled authoritative external events; mark incoming attacks as pending.
2. Sample logical inputs for this tick; resolve opposite directions using last-pressed-wins, with simultaneous ties neutral.
3. Apply hold, then rotation, then horizontal movement. Successful hold ends remaining piece actions for that tick. Simultaneous clockwise/counterclockwise requests cancel.
4. Apply hard drop if enabled; otherwise gravity/soft drop and lock timers. Hard drop skips further movement and locks immediately.
5. On lock: classify spin, merge cells, clear rows, update score/combo/back-to-back, calculate attack, cancel pending garbage, emit residual attack.
6. At the next spawn boundary, apply eligible incoming garbage, evaluate overflow/top-out, then spawn the next piece and evaluate spawn collision.

Classic has a separately specified order established by reference traces. Do not force it through modern input/lock ordering just to share code. Reuse geometry/collision helpers where behavior matches.

Modern/Fusion spawn collision ends the game; garbage pushing occupied cells beyond storage ends the game. Locking entirely above the visible field also ends the game after row clearing; a partially hidden placement may survive. Encode each case in fixtures before implementing battle logic.

### Input and authentic handling

DAS means automatic repeated horizontal movement after holding a direction. Hypertapping and rolling are physical ways to produce repeated presses; they are not buttons, macros, or separate movement algorithms.

Capture timestamped keyboard/controller transitions, ignore operating-system key-repeat events, and maintain held state per device. Combine bindings into logical actions with reference counts so releasing one bound button does not release another still held button. Give keyboard, D-pad, and optional analog-stick bindings the same simulation path; sticks use configurable deadzone and hysteresis.

Classic samples held state on emulated frame boundaries. Preserve the raw event stream for diagnostics, but do not turn multiple transitions within one reference frame into extra classic moves. Modern/Fusion also use tick-based logical inputs with a documented edge policy. Lost focus clears local held inputs and sends releases; offline may pause, online does not. Device removal releases its bindings and shows a reconnect notice.

No arbitrary tap-rate restriction, smoothing, or generated turbo. Hardware adapters and OS polling may lose transitions before the game sees them; measure at least a keyboard, common USB gamepad, and NES-controller USB adapter. Claiming “perfect rolling support” requires physical testing, not merely an automated key sequence.

### Spin detection and rewards

For Fusion/Modern v1: a T-spin requires the last successful manipulation of the current piece to be a rotation and at least three occupied corners around its center at lock. Board walls count as occupied. Successful lateral movement invalidates the rotation marker; gravity, soft drop, and hard drop preserve it. Failed actions do not overwrite it. A subsequent successful rotation restores it. Initially classify all qualifying spins as full spins; mini classification is explicitly deferred. This is a project rule, not a claim to match every modern game's scoring.

Prototype scoring/attack table, frozen as `battle-v1` before networking integration:

| Clear | Score points | Base garbage lines |
| --- | ---: | ---: |
| Single | 100 | 0 |
| Double | 300 | 1 |
| Triple | 500 | 2 |
| Four-line clear | 800 | 4 |
| T-spin, no clear | 400 | 0 |
| T-spin single | 800 | 2 |
| T-spin double | 1200 | 4 |
| T-spin triple | 1600 | 6 |

Battle score is informational, without a level multiplier. Classic has its independently validated score/level system. Multiplayer victory comes from survival, not score.

- Back-to-back: four-line clears and line-clearing T-spins qualify. From the second consecutive qualifying clear, add 1 attack and multiply its base score by 3/2 using integer arithmetic. A normal line clear breaks the chain; a no-clear lock leaves it intact.
- Combo: consecutive line-clearing locks have indices 0,1,2,...; add `min(4, floor(index/2))` attack. A no-clear lock resets the combo. No separate combo score bonus in v1.
- Perfect clear: add 6 attack when the board is empty after clearing. This stacks with the other bonuses. No extra score bonus in v1.
- Total attack is computed once, then cancels pending incoming garbage oldest-first, then sends the remainder to one opponent. No negative queue counts or duplicated cancellation.

These are initial balance choices. Practice fixtures must cover every clear type, chain break, cancellation, and top-out interaction before tuning.

### Garbage and targeting

Each outgoing attack becomes one packet containing row count, source player, stable event ID, and a deterministic hole sequence. All rows in that packet share one hole; the next packet chooses a hole uniformly, repeats allowed. Separate RNG streams generate pieces, garbage holes, and automatic targets so UI actions and targeting do not alter piece order.

Packets become eligible 30 battle ticks after their authoritative creation. Eligibility does not immediately move an active piece: apply up to 8 eligible lines at the recipient's next spawn boundary, retaining overflow. Clearing lines may cancel both eligible and not-yet-eligible pending packets. Newly emitted attacks enter recipients' queues on the next tick, avoiding player iteration order advantages. Simultaneous outgoing attacks are not globally netted against each other.

For a duel, the opponent is the target. For free-for-all, use manual selection/cycling with a deterministic random live opponent as the default. Resolve target validity at dispatch; if the target has been eliminated, select another live opponent. Do not multiply attacks across all players. Defer badges, KO multipliers, attacker bonuses, and complex targeting modes.

All eliminations on one tick are resolved together. If everyone remaining tops out on that tick, record a draw/shared placement rather than choosing a winner by iteration order. Disconnection retains the board for a 10-second reconnect window with neutral input; the board continues falling and can lose. Reconnection requires a fresh snapshot and the room-issued reconnect credential. After timeout, mark forfeit.

## 6. Visual and audio direction

Use a dark arcade display, sharp pixel typography, framed panels, strong block silhouettes, and restrained original color palettes. Choose an original panel layout and sound identity; retro inspiration does not require copying the original screen.

Default layout: large player field centered slightly left; next/hold and score adjacent; incoming garbage meter beside the field; opponent field on the right. Free-for-all replaces the opponent panel with a selectable grid, keeping the player's field large. Spectator view supports a featured field plus thumbnails, player cycling, and room status.

Menus: Play Offline, Multiplayer, Practice, Replays, Settings. Settings and lobbies must be fully operable with a controller, including visible focus, confirm/back bindings, remapping, and conflict warnings. Save bindings per device profile. Local pause is immediate; online menus clearly indicate play continues.

Render at a logical 640x360 layout with integer scaling where possible, nearest-neighbor textures, and letterboxing. Provide a larger-text layout instead of squeezing labels into fixed pixel boxes. Avoid relying on color alone: optional block patterns and labelled attack feedback. Screen shake, flashes, particles, and CRT treatment are optional and off for competitive defaults. Distinguish rotate, blocked move, lock, clear, incoming attack, and top-out sounds; sound must never drive simulation timing.

## 7. Recommended technology stack

**C++20 + SDL3 + a shared headless simulation + ENet dedicated servers.** The small 2D presentation does not need a large scene engine; the difficult work is timing, deterministic rules, input, and networking. Explicit control over those systems outweighs the cost of building a small menu layer.

| Layer | Choice | Reason / boundary |
| --- | --- | --- |
| Native client | C++20, SDL3 | Windowing, gamepads, keyboard, audio, 2D rendering in one native dependency |
| Graphics | SDL renderer API, sprite atlas, bitmap font | Enough for boards and menus; no custom Vulkan renderer needed |
| Simulation | Standard C++20 library with fixed-width integers | No SDL, networking, filesystem, clock, or OS dependencies |
| Game transport | ENet reliable UDP, wrapped behind an interface | Distinct reliable control and transient snapshot channels |
| Dedicated server | C++20 console executable on Linux | Runs the same rules as the client, no display required |
| Config/rules | JSON via nlohmann/json at boundaries | Readable presets; validate and compile into immutable structs |
| Wire/replay encoding | Explicit versioned binary encoding | Fixed widths, little-endian fields, bounds checks; never serialize native struct memory |
| Development UI | Dear ImGui, debug builds only | Timelines, inspectors, latency graphs |
| Tests | Catch2 + CTest, scripted headless clients | Mechanics fixtures, determinism, integration, load and fault tests |
| Build | CMake presets + Ninja, vcpkg manifest/baseline | Reproducible client/server/test dependency graph |
| CI | Windows MSVC and Linux Clang jobs | Native client build and cross-platform deterministic checks |
| Packaging | Windows ZIP first; Linux server archive/container | Simple alpha distribution; installer/signing later |
| Persistence | Local JSON settings, binary replay files | No database necessary for private-room MVP |
| Later online services | HTTPS account/lobby service + PostgreSQL | Add with public matchmaking, not before |

SDL3 officially supports native desktop platforms and low-level input/audio/graphics access ([SDL documentation](https://wiki.libsdl.org/SDL3/FrontPage)). Its gamepad event type includes nanosecond timestamps; that enables ordering and measurement, not a nanosecond hardware-latency guarantee ([event documentation](https://wiki.libsdl.org/SDL3/SDL_GamepadButtonEvent)). Events must be pumped on the video/main thread ([polling documentation](https://wiki.libsdl.org/SDL3/SDL_PollEvent)).

ENet is a reliable UDP library ([upstream](https://github.com/lsalzman/enet)). Treat encryption, authentication, and account security as separate requirements; ENet is not the complete security design. MVP is a private-room alpha with no account passwords or sensitive personal data over this transport. Public service release requires an authenticated encrypted transport design/review before account features are exposed.

Supporting upstreams: [Catch2](https://github.com/catchorg/Catch2), [JSON for Modern C++](https://github.com/nlohmann/json), [Dear ImGui](https://github.com/ocornut/imgui). Pin tested releases/commits during foundation work; do not depend on floating branches or guess future compatible version numbers.

Alternatives: Godot with a separate shared C# simulation would speed menu authoring, and Rust with native window/input/render libraries would improve memory safety. C++/SDL is recommended here for a small native presentation, direct input/timing control, and a single client/server language. Reconsider after the input prototype if toolchain or UI development cost outweighs those benefits. This is a project-fit recommendation, not a claim that engines cannot deliver low latency.

## 8. Architecture and deterministic contracts

Dependency direction:

```text
platform_input -> client_controller -> game_core <- match_server
                         |                |
                         v                v
                    presentation      replay/tests
client_transport <-> protocol <-> server_transport
rules_loader ----------> immutable Ruleset
```

`game_core` owns piece motion, timing, collision, locks, scoring, garbage queues, and deterministic match resolution. Rendering receives immutable views and emitted events. The server owns identity, rooms, network deadlines, and validation; it cannot invent an alternate set of game rules.

Foundation contracts to freeze before parallel feature implementation:

| Type/API | Required contents / behavior |
| --- | --- |
| `Ruleset` | Schema version, preset ID/version, rational tick rate, all geometry/timing/scoring tables, canonical content hash |
| `InputFrame` | Player ID, tick, sequence, held-action bitmask; edge derivation from prior accepted held state; validation flags |
| `PlayerState` | Board, active piece, next/hold, RNG, input/DAS/gravity/lock timers, score/level, combo/B2B, incoming packets, life state |
| `MatchState` | Tick, rules hash, ordered player states, target/RNG state, scheduled external events, outcome |
| `advance_match(state, inputs)` | Pure deterministic one-tick advance returning new state and stable-ID events |
| `Snapshot` | Complete state required to resume, tick, accepted-input acknowledgements, schema/version, canonical hash |
| `GameEvent` | Tick, player, event ID, type, bounded payload; presentation consumes events idempotently |
| `ReplayHeader` | Engine compatibility version, exact rules bytes/hash, seed, players, tick rate |

Use an explicitly specified PRNG with published project test vectors; never `std::random_device`, implementation-dependent distributions, floating-point gravity, pointer order, or unordered-map iteration in simulation. Define byte order and canonical field order for serialization/hashing. Use integer gravity accumulators and a rational clock for classic timing; rendering frequency never determines movement.

Modern/Fusion run at 60 simulation ticks/s. Classic frame duration is derived from the selected reference revision/platform and frozen in its ruleset. Maintain elapsed-time scheduling with a monotonic clock and integer remainder. Render independently at the display rate; do not visually interpolate grid positions. If the client falls behind, perform bounded catch-up then resynchronize online; never silently skip authoritative ticks. Record overload offline and prevent those sessions being labelled timing-validated.

Snapshots must include every hidden timer, random state, previous input, and scheduled garbage event; a board-only snapshot cannot reliably reconcile predictions.

## 9. Multiplayer and protocol

Authoritative dedicated server, immediate local prediction, and client reconciliation. Clients submit inputs, never scores, attacks, board results, or claimed wins. The server independently simulates each board and generates attacks/results. Immediate local feedback is speculative until confirmed; avoid irreversible effects from predictions.

Initial timing proposal: clients predict 3 battle ticks ahead of the server's simulated tick; synchronize tick offset using periodic timestamp exchanges and RTT estimates. Server uses a fixed per-room playout deadline and never waits indefinitely for one player. If the input for a tick is absent, carry the previous held state for at most 6 ticks, then neutralize it. Drop late inputs for already simulated ticks and acknowledge the rejection; no global server rollback in MVP. Reject inputs beyond a bounded future window. Tune lead/deadline using network tests before committing a competitive preset.

Input stream: send each tick with a rolling redundant window of the last 8 frames, sequenced/unreliable. Deduplicate by player/tick/sequence and reject conflicting rewrites of accepted frames. Server snapshots at 20 Hz include acknowledged inputs and authoritative pending garbage. Reliable messages handle room changes, match start, full resync, and final results; use separate channels to avoid a control retransmission blocking transient snapshots. Bound datagrams to a conservative 1200-byte payload; chunk reliable full snapshots with total-size/time limits.

When a snapshot differs, restore its full state and replay unacknowledged inputs to the prediction tick. Incoming garbage may therefore correct the local board. Deduplicate sound/visual events by stable IDs and confirm match results only from the server. Keep at least 2 seconds of prediction history; request a full snapshot when a correction lies outside it. Opponent boards can display delayed snapshots, with the delay visible in diagnostic tools.

Protocol envelope: magic, protocol major/minor, message type, bounded payload length, session identifier, packet sequence. Message set: Hello/Welcome/Reject, CreateRoom/JoinRoom/LeaveRoom, SetReady/RoomState, StartMatch, InputBatch/InputAck, StateSnapshot/FullSnapshotRequest, Ping/Pong, MatchResult, Reconnect, Error. Define field widths, size limits, and malformed-message behavior in `specs/protocol-v1.md` before network agents start. Reject incompatible protocol or rules hashes before readying a player.

Rooms move through Waiting -> Countdown -> Playing -> Results -> Waiting. Rule changes reset ready status; rules and roster lock at countdown. Late arrivals spectate. Host disconnect transfers room management to the earliest connected participant; the dedicated server continues the match. Private-room alpha can use a configured server address and room code, avoiding an unnecessary public discovery service.

Spectators receive an authoritative full snapshot on joining plus bounded updates. They cannot submit player inputs. For many boards, publish the featured board at 20 Hz and thumbnails at 5 Hz; throttle per connection and subscribe only to required views. Capacity admission and bandwidth budgets are explicit server settings. One room is owned by one worker; distribute independent rooms across workers/processes before attempting intra-room concurrency.

Robustness requirements: validated enums/lengths/tick windows, per-peer input/message quotas, connection caps, handshake timeout, bounded allocation, stale-session rejection, and decoder fuzzing. A server-authoritative model limits forged states; it does not by itself detect macros or bots. Do not promise complete cheat prevention.

## 10. Fidelity and reference procedure

The workspace contains `Tetris.nes`. It was not opened, executed, disassembled, or copied in preparing this plan. Shipping binaries and tests must not require it.

Start with independent implementation from observable behavior. Do not base the initial code on disassemblies. Keep proprietary ROMs, extracted assets, and third-party source out of Git, build artifacts, and agent task inputs. This is an implementation/provenance policy, not a legal conclusion that a visually or behaviorally similar release is automatically cleared.

Reference milestone:

1. Record target region, revision, controller/adapter, display, and reference clock. If local reference execution is used later, keep it in an optional private harness outside distributable tests.
2. Specify and measure spawn geometry, rotations, gravity tables, soft drop, initial movement, DAS charge/reset/wall behavior, release/repress, opposite directions, entry/line-clear delay, locks, level transitions, top-out, scoring, and randomizer state evolution.
3. Convert observations into independently authored input/state traces with clear provenance and expected tick-by-tick outputs. Do not embed ROM bytes or extracted program code in fixtures.
4. Compare the implementation against those traces, especially holds through entry delay, blocked movement, rapid tapping, and gravity/rotation on the same tick.
5. Have experienced DAS, hypertapping, and rolling players perform blind comparisons on the intended hardware. Record remaining deviations.

If original randomizer behavior depends on startup timing or other hidden state, model that explicitly in the Classic seed/state contract; seven-bag is not an acceptable substitute for a claimed faithful preset. Preserve intentionally selected gameplay behavior without recreating hangs or memory corruption; document divergences at extreme levels.

Before broad public distribution, review branding, visual similarity, asset provenance, dependency notices, and any reference-derived material. Do not assume a new name or original source code alone settles the user's legal-distinctness concern.

## 11. Validation and success criteria

| Area | Acceptance target |
| --- | --- |
| Classic fidelity | Every approved trace matches on every tick; remaining gaps labelled, human handling sign-off recorded |
| Determinism | Same replay hashes on Windows/Linux, debug/release, across 100 fixed seeds and at least 100,000 ticks per seed |
| Rotation/spins | All kick transitions, blocked cases, T-spin single/double/triple, and invalid-spin cases have fixtures |
| Input | Key repeat ignored; rebinding, overlap, focus loss, disconnect, rapid transitions and frame boundaries verified |
| Rendering independence | Same input trace produces identical gameplay at 60/120/144/240 Hz rendering |
| Client performance | Provisional p99 simulation <1 ms and game CPU frame work <2 ms on a declared reference PC |
| Input latency | Visible change on next eligible simulation tick; measure end-to-end p50/p95 with high-speed capture, compare VSync modes |
| Network | 0/50/100/150 ms RTT, 20 ms jitter, 1%/5% loss and reordering: eventual state convergence, no duplicated attacks/results |
| Room capacity | 8 players + 16 spectators for 30 minutes, no desync; server p99 room tick <4 ms on declared hardware |
| Recovery | Resync, reconnect, missing inputs, stale packets, host exit, full room, and incompatible version paths tested |
| Distribution | Clean Windows launch and Linux headless launch; no ROM/assets/secrets unintentionally packaged |

Performance numbers are budgets to validate, not measured claims. Record CPU, OS, controller, adapter, monitor refresh, presentation mode, compiler, and commit with results. A single fast machine or keyboard-only test is insufficient evidence for the core input promise.

## 12. Main risks and decision gates

| Risk | Mitigation / gate |
| --- | --- |
| Modern mechanics erase classic feel | Validate Classic first; playtest Fusion as a separate ruleset |
| “Exact” behavior remains unspecified | Reference specification and fixtures block fidelity claims and Classic completion |
| Input hardware masks rolling taps | Test real devices early, expose input diagnostics |
| Prediction corrections hurt fairness | Duel network spike before expanding rooms; adjust lead, deadline, garbage grace |
| Large-room spectator traffic grows quadratically | Subscription tiers, rate caps, benchmark each capacity increase |
| C++ and custom UI slow development | Small dependencies, sanitizers, narrow interfaces, one early native prototype |
| Agents implement conflicting behavior | Contract milestone, module ownership, golden fixtures, integrator reviews |
| Public launch has additional security/provenance needs | Separate launch gate after private-alpha validation |

The next step is milestone M0 in the companion plan, not simultaneous implementation of every feature. Once the contracts and reference scope are frozen, independent client, rules, server, and verification work can proceed safely in parallel.
