# Multiplayer milestone 1

[Current M8–M10 contracts](multiplayer-runtime.md) add runtime prefabs and
gameplay RPCs/events, [server physics](multiplayer-physics.md), and
[guest lobbies/recovery](multiplayer-lobbies.md). The current scene schema is MP09
with WES4 component/invocation manifests; compatible WES2/WES3 remain readable.
Earlier stage-only restrictions/schema names below are historical. See
[current browser/Windows evidence](multiplayer-runtime-validation.md).

The fixed-arena foundation remains available. [Milestone 2](multiplayer-scenes.md)
now adds bounded authored-scene publication, player-template transform replication
and Host/Join actions in both editors. The limits described below concern M1.

Optional `WebEngine::Network` (`engine_network`) supplies a portable C++20 session
protocol. Browser and Windows adapters carry identical binary messages over
WebSockets. `examples/multiplayer` is a complete, server-authoritative test arena
with a native headless simulation, browser client, Windows client and editor test
launchers. Gameplay remains in the example. No renderer owns network state.

## Build and run

Use the repository-local Emscripten version in `.emscripten-version` and the native
prerequisites in README. Enable `WEBENGINE_BUILD_MULTIPLAYER_DEMO=ON` for each build:

```powershell
cmake -S . -B build/windows -DWEBENGINE_BUILD_WINDOWS=ON -DWEBENGINE_BUILD_MULTIPLAYER_DEMO=ON
cmake --build build/windows --config Release --target network_tests multiplayer_server webengine_editor --parallel 6
./.tools/emsdk/upstream/emscripten/emcmake.exe cmake -S . -B build/web-release -G Ninja -DCMAKE_BUILD_TYPE=Release -DWEBENGINE_BUILD_MULTIPLAYER_DEMO=ON
cmake --build build/web-release --target network_tests multiplayer_demo web_assets --parallel 6
npm.cmd ci --prefix examples/multiplayer --ignore-scripts
node examples/multiplayer/server.cjs build/windows/Release/multiplayer_server.exe 8177
```

Serve `dist` using the normal repository server. The arena is at
`http://localhost:8080/multiplayer/index.html`; `?backend=webgl2` forces WebGL2.
Launch `build/windows/Release/multiplayer_demo_windows.exe`, optionally with
`--d3d12` and `--server ws://127.0.0.1:8177`. Join explicitly in each client.
WASD/arrows move; Space or **Use beacon** interacts within 2.2 units. Join/Reconnect
discards the old connection and obtains a new player identity. Leave removes the
player from the next full snapshot. The server's beacon count persists until the
server stops. Clients cannot author positions or the count.

For sandbox Emscripten temporary-file failures, create `build/multiplayer-tmp`
and set process-local `TMP`/`TEMP` to its absolute path. Native MSBuild needs access
to the installed Visual Studio/Windows SDK registry.

## Engine API and ownership

Include `engine/network.hpp`. Link `engine_network` for codecs/session state and
one of `engine_network_browser` or `engine_network_windows` for
`connectWebSocket(url)`. Network core depends only on standard C++20. The browser
adapter privately uses Emscripten WebSockets; Windows privately uses WinHTTP.

`ServerSession(schema, session)` owns peer readiness, monotonically allocated
peer IDs, the last published entity ownership registry and queued commands.
`connect()` returns a transport-independent peer ID. `receive(peer, bytes)`
validates a Hello or Command; a Hello returns Welcome bytes for that peer.
`readyPeers()` identifies admitted clients, `takeCommands()` transfers accepted
commands, and `snapshot(records)` validates/encodes a complete replacement before
publishing ownership and advancing its sequence. `disconnect(peer)` cancels that
peer's pending commands. The application removes/reassigns its entities explicitly.

`ClientSession(schema)` creates Hello bytes. `receive(bytes)` validates Welcome
and stages complete Snapshot replacements. `command(entity, kind, payload)`
produces a sequence-numbered request. `reset()` clears identity, counters and all
replicated state; pair it with transport disposal before reconnecting. Network
entity IDs map to local generation-checked `ActorId` handles in the application;
they are not local scene handles or saved node IDs.

`Record` holds session entity ID, owner peer (zero for server-owned), kind and an
opaque application payload. `Command` holds requester, target, sequence, kind and
payload. Applications version/validate payloads and enforce gameplay constraints.
The server protocol admits commands only to entities owned by the requesting
peer. Server-owned interactions therefore use an owned player's request and
application-side target validation, as the beacon example demonstrates.

Wire format is explicitly little endian: magic `WEN1`, protocol version 1,
message type, content/schema ID, session ID, sequence and peer ID, followed by
type-specific fields. No native structs, pointers, `size_t`, executable behavior
or archives cross the wire. Message types are Hello, Welcome, Command and Snapshot.
Malformed, trailing, oversized, incompatible, duplicate and stale packets fail
before publication. Exceptions leave existing session state intact for validation
failures. Hosts close rejected peers; valid clients are unaffected.

Limits: 16 live peers, 64 entities/snapshot, 256 payload bytes/entity or command,
32768 bytes/message, 64 pending commands and 64 messages per adapter queue. Sequence
exhaustion requires a new session. There are no unbounded application queues.

All session methods and adapter public calls belong to one owner thread. Network
workers/browser callbacks never access scenes. Dispose the connection before
replacing its session/view. The Windows adapter owns its IO workers; browser
disposal unregisters callbacks and deletes the socket. Applications drain messages
and publish scene changes at idle boundaries, preserving deferred destruction.

## Server and example behavior

`multiplayer_server` is a C++ process without a renderer, GUI, audio device or SDL
window. It owns one `Application`, scene and arena. Existing component fixed ticks
advance motion at the default 60 Hz; bounded Application catch-up remains intact.
There is no new simulation scheduler or core frame phase. Server monotonic time,
not client timestamps or transport wakeups, determines elapsed simulation time.

The Node `ws` gateway owns network sockets and forwards bounded binary packets
through a hex-encoded private stdio bridge. Wakeups request service at roughly
60 Hz; snapshots are published every third wakeup. These are targets, not timing
guarantees. The gateway rejects text, limits packet rate and buffering, caps peer
count, and expires uncompleted handshakes. It listens only on IPv4 loopback.
`package-lock.json` pins its dependency; no package install scripts are needed.

The arena bounds movement to 3 units/second and +/-7 units, normalizes diagonal
input, expires held commands after 250 ms without refresh, validates payloads,
and rate-limits beacon use to once per 500 ms per player. These rules live outside
the reusable network library. Movement is kinematic, with no collision/physics
rollback claim. Rendered poses ease toward server positions without modifying
replicated authority. Clients send intent at up to 20 Hz and time out after ten
seconds without received session traffic. Browser blur neutralizes input; hiding
the page leaves the session explicitly. Rejoining is manual. Native focus loss
neutralizes input while the server continues.

## Editor support

Both scene editors expose a server endpoint and **Multiplayer test** action. They
open the fixed arena in a separate browser tab/native process, retaining the
authoring document, history and normal Play state. The native `DesktopEditor`
accepts an optional host-owned `setMultiplayerLauncher` callback; commands run on
the idle owner. `openMultiplayerTest` rejects Play/pending-document transitions.
The sample host supplies process launch policy, and clears process handles without
owning the independently opened client window. The browser host supplies URL/tab
policy. Native launcher callbacks must not outlive their captures.

The **Multiplayer test** launcher retains its fixed-arena behavior. The separate
[M2 Host/Join actions](multiplayer-scenes.md) publish a supported static scene and
selected player template. General component replication, replication property
inspectors and collaborative editing remain future work.

## Boundaries and next milestones

The development gateway has no accounts, authorization tokens, matchmaking,
internet deployment, TLS termination, server discovery, persistence or host
migration. Clients support `wss://` endpoints; cleartext `ws://` is accepted only
for `localhost`/`127.0.0.1`. Public-service authentication/deployment is separate.
The fixed arena retains full snapshots. Authored-scene clients now add
[M3 prediction/reconciliation](multiplayer-prediction.md),
[M4 presentation smoothing](multiplayer-presentation.md) and
[M5 delta/interest replication](multiplayer-replication.md) and
[M6 authoring/diagnostics](multiplayer-authoring.md). General physics
rollback, unreliable channels, WebRTC and arbitrary component factories remain
extensions.
No consumer repository pin is advanced by adding this module or updating its skill.

See [validation](multiplayer-validation.md) for actual cross-runtime evidence.
