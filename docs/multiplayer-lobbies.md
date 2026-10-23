# Guest lobbies, invitations and recovery (M10)

The authored-scene example includes an optional guest-authenticated lobby service.
Each lobby has its own native authoritative scene-server process. Browser and
Windows clients can create a room, join by invitation, see a roster and recover
the same player after a temporary transport loss. The fixed-arena and direct
single-scene gateways remain available for development. See [runtime prefabs/RPCs](multiplayer-runtime.md),
[physics](multiplayer-physics.md) and [validation](multiplayer-runtime-validation.md).

## Start and use

Build the opt-in multiplayer targets using the [existing setup](multiplayer.md)
and install the pinned `examples/multiplayer` Node dependencies. From the repo root:

```powershell
node examples/multiplayer/lobby-server.cjs build/windows/Release/multiplayer_scene_server.exe 8178
```

The service binds loopback. In either editor, enable **Lobby options**, enter a
guest name, room name and `ws://127.0.0.1:8178` endpoint. **Host selected actor**
with no invitation creates a room and publishes the supported source scene.
**Join authored scene** requires an invitation and does not replace the room's
content. The isolated client still has an explicit Join action in the browser;
the native launcher connects automatically. Source scenes/history stay retained.
Runtime clients also provide Create/Join controls. The creator receives an
invitation capability, displayed in a masked field; reveal/select it manually
when inviting someone. Guest names are display labels, not verified account names.

Only the authenticated creator may publish scene content. Guests can request
the accepted content but cannot publish it. Membership does not grant arbitrary
gameplay authority: ordinary network ownership, registered invocation permissions,
payload validation and server game rules still apply. The roster includes public
guest identities, display names, host/online flags, capacity and started status.

## Portable protocol and adapter lifetime

`engine/network_lobby.hpp` belongs to standard-only WebEngine::Network. WEL1
version 1 messages are bounded to 2048 bytes and completely validated. Operations
are Create, Join and Resume. Controls are Request, Accepted, Rejected, Leave,
Left, Confirm and Roster. Names are printable ASCII (guest 1–32, room 1–64 bytes).
Invitation/session tokens are 64 lowercase hex characters; guest IDs are 32 hex
characters and room IDs 24. Unknown flags, duplicate roster IDs, trailing bytes,
truncation and credentials inappropriate to the operation reject.

`LobbyConnection(unique_ptr<Connection>, LobbyRequest)` owns an ordinary browser
or Windows adapter. `state()`, `send()` and `receive()` pump authentication on the
single owner thread. Open means Accepted was validated and Confirm queued. It
filters controls and exposes only gameplay/content packets through Connection.
`credentials()` and `roster()` return adapter-owned data; do not retain references
past its lifetime. `leave()` is asynchronous: keep pumping until Left or closure.
Destroying the connection cancels transport IO through the existing adapter.
No clock, scene, account provider or callback is retained by the portable wrapper.

## Authentication and authoritative recovery

The Node host uses cryptographic random 256-bit invitation/session capabilities,
SHA-256 token lookup and constant-time fixed-size invitation hash comparison.
Creating/joining issues an in-memory pseudonymous guest identity and session token.
An invitation grants membership in one room; it is not a session-recovery token.
Resume requires the guest's session token. An already connected guest cannot be
displaced by a duplicate login. On resume the token rotates; the old token remains
valid only during the unconfirmed handoff, then Confirm revokes it. At most the
old/new pair is retained during that interval, so losing Accepted can be retried.

Unexpected socket loss suspends the peer for a default **30 seconds** (host option
1–300 seconds). Its player, gameplay state, owned prefabs, slot and authoritative
physics world remain alive. Input queues are cleared. Physics continues server-side.
The host bridges suspend/resume to `ServerSession::suspend`/`resume`; the latter
rotates the per-peer protocol epoch, resets command/RPC/event sequences and delta
baselines, and requires a new Hello. `simulationPeers()` includes reserved peers;
the ordinary ready-recipient list does not. Old-epoch packets cannot control the
recovered player. Admission/content transfer is rebuilt before fresh snapshots.

The example client retains credentials in memory and retries an unexpected loss
up to eight times within the grace deadline, starting at 250 ms and doubling to
a four-second cap. Explicit rejection ends retrying. Recovery success is counted
only after a fresh authoritative snapshot. The client rebuilds component handles,
prefab mappings, prediction and presentation histories from current state. Transient
events are not replayed. Explicit **Leave** cancels retries, sends Leave, clears
credentials and removes the authoritative player/owned objects immediately when
acknowledged. Grace expiry does the same cleanup. Closing all memberships destroys
the room and invalidates its invitation. Closing a tab/crashing without an
acknowledged Leave uses the grace path.

## Capability handling and boundaries

Session/invitation capabilities never enter source archives, URLs, normal saves,
diagnostic JSON, logs, screenshots of revealed fields, or automated evidence.
The browser editor passes invitation settings over a bounded, expiring same-origin
BroadcastChannel. A random non-secret handle identifies it in the launch URL;
settings are not stored in IndexedDB. Up to 16 pending channels expire after
120 seconds and close after consumption. Public scene packages retain their
existing separate IndexedDB handoff. The native editor passes a bounded encoded
Create/Join request through a restricted inherited anonymous stdin pipe to the
intended client. No invitation argument, temporary credential file or clipboard
write is used. The existing temporary native *scene package* contains public content.

The service bounds rooms to eight and memberships to 16 per room, including
reserved disconnected guests. It limits auth attempts per address, frames per
connection, payloads, queued delivery and process/socket backpressure. Limits are
development safeguards, not an internet-service abuse defense or load certification.
The trusted in-process UAT disconnect hook is disabled by default and has no
network administration endpoint. Child processes and timers are cancelled on shutdown.

This milestone supplies guest capability authentication and **process-lifetime**
session recovery. It does not provide accounts, identity-provider login, matchmaking,
discovery, invitation delivery, kick/ban, host-role migration, durable storage,
cross-device credential synchronization or recovery after a service/process restart.
The service itself is loopback cleartext; internet exposure requires separately
designed TLS termination, origin policy and deployment security. Existing adapters
support wss URLs; that alone does not deploy a secure public service. Keep credentials
in memory and require a fresh join after client restart or expiry.
