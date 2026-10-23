# Runtime prefabs, gameplay RPCs and events (M8)

The optional networking modules now support authoritative prefab lifetimes and a
separate, bounded RPC/event stream. Browser and Windows editors author the same
catalogue and permissions. The example's spawn/despawn/pulse/impulse rules remain
application code; the reusable engine has no pickup, projectile or combat policy.
See also [physics](multiplayer-physics.md), [guest lobbies and recovery](multiplayer-lobbies.md)
and [validation](multiplayer-runtime-validation.md).

## Author a runtime prefab

Select an enabled root mesh actor, add Replication settings and set Ownership to
**Runtime prefab**. Its hierarchy becomes a whitelisted template, hidden during
the isolated server/client session until the server spawns an instance. Configure
Distance/Always/Owner only relevance and registered gameplay components as usual.
The template stays visible and editable in the source editor. Publication freezes
content; restart the session to change templates or schemas.

The validated NetworkScene catalogue assigns prefab kinds starting at 1024, sorted
by saved identity. Player kind 1 and persistent server kinds starting at 2 retain
their earlier meanings. Prefab definitions have entity 0; they do not create an
initial server actor. Up to 32 authored prefab types are supported. Source templates
must meet the existing bounded scene/component profile. Built-in linked prefab
provenance is retained; the ordinary PrefabAsset definition and resource types are
reused, not a second asset format.

`engine/network_prefabs.hpp` adds `PrefabSpawner(scene, codecs={}, firstId,
capacity=48)`. `add(kind, actorPrototype)` snapshots a detached prototype with the
existing archive codecs; the PrefabAsset overload uses its canonical definition.
Pointers/assets are borrowed only for registration. The spawner owns prototypes,
codecs and generation-checked ActorIds; Scene owns live instances and must outlive
all calls. Calls require the idle hierarchy. Its destructor releases registrations
and mappings; use `clear()` explicitly to destroy live instances.

`spawn(kind, owner, transform, initialize={})` validates the whitelisted kind,
capacity, pose and detached candidate before adoption. The synchronous initializer
may configure initial component data on that candidate; it must not mutate external
state. Failure during preparation leaves published instances and the next identity
unchanged. `despawn(id)` schedules ordinary deferred destruction and erases the
mapping; absent IDs return false. Do not destroy managed actors through unrelated
lifetime policy. IDs start at runtimeEntityBase (16777216), increase monotonically
and are never reused; exhaustion rejects. Re-entry receives fresh local handles.

The example reserves 16 player slots plus configured server actors within the
64-record limit. Remaining slots bound simultaneous runtime prefabs; zero capacity
is valid. Its extra game policy allows at most four prefabs per owner and eight
RPCs per second. Owner disconnect/explicit leave removes owned instances; authenticated
disconnect grace retains them until recovery or expiry. These quotas and cleanup
rules are host policy, not universal engine rules.

## Register and route gameplay invocations

`engine/network_rpc.hpp` is part of standard-only WebEngine::Network.
InvocationRegistry owns up to 32 InvocationTypes, each with a unique ID 1–32,
stable ASCII key (up to 128 characters), nonzero version, direction, maximum payload
of 0–256 bytes and pure validation callback. Directions are ServerRpc and
ClientEvent. Captured dependencies must outlive registry copies. Validation must
not dispatch callbacks or mutate gameplay. Dispatch occurs explicitly in host code
after schema, authority, permission and payload checks.

ClientSession::rpc(target, kind, payload) sends WEN1 Type 7 using an independent
sequence. ServerSession::receive enforces the authenticated transport peer,
session epoch and ownership of target, then enqueues at most 64 calls separately
from movement commands. Hosts consume `takeRpcs()`, validate their registry and
authoring permission, then apply game rules on the idle simulation owner. A client
cannot use this API to invoke server-owned or another player's actor. In the
example, a despawn/impulse request targets the caller's own player and includes
the affected prefab ID in its validated payload; the game checks its ownership too.
There are no remote function pointers or arbitrary method-name execution.

`eventFor(peer, target, kind, payload)` creates WEN1 Type 8 for one ready recipient,
using a separate per-recipient sequence. Hosts implement owner-only replies or
multicast by choosing recipients. Target must exist in the full authoritative
registry. Send generated packets in order, or close the connection on send failure.
The existing adapters retain their 64-packet and payload bounds. Clients validate
schema/direction/source permission before consuming `takeEvents()`; that queue is
also bounded to 64. Apply effects through live generation-checked handles, and
discard an effect whose target has disappeared.

Events are transient reliable-stream messages. They are not retained for late
join, replayed by snapshot resync, or replayed after authenticated recovery.
Persistent results belong in replicated component fields. Independent RPC/event
sequences preserve the movement acknowledgement/replay contract when invocations
interleave with input. Prediction and smoothing never grant gameplay authority.

## Editor permissions and compatibility

`engine/network_gameplay_authoring.hpp` defines InvocationPermissions with
serverRpcs/clientEvents bitmasks. `makeInvocationMetadata()` defaults to denying
all calls/events. It uses an inert ordinary Component tag:
`WebEngine.Invocations.v1:<rpcMask>:<eventMask>`. Duplicate/unknown versions,
unsupported IDs, invalid placement or non-inert metadata reject publication.
There is no scene archive migration. Ordinary save/load, duplication, dirty
tracking and undo/redo preserve the annotation.

Header-only `invocationProperties(registry, base)` adds Boolean `rpc.<key>` and
`event.<key>` fields to a shared editor registry. Register a normal Component
factory through EditorComponentSupport to create settings. The example offers
**Gameplay RPCs and events**, initially enabling its registered demonstration
actions. Inspect these fields before hosting. Frontends share the same registry
and history; no network implementation dependency enters engine_core/editor.

Pass the invocation registry after the component registry to save/loadNetworkScene.
WES4 includes both exact canonical manifests before archive construction. The
invocation manifest contains IDs, versions, direction, bounds and keys, not code.
Legacy unregistered WES2 and matching WES3 component packages remain readable.
The current scene schema is **MP09** (M8 introduced MP08; M9 adds physics tails).
Rebuild server and clients together. WEN1 envelope version 1 and M1 MP01 remain.

Runtime component composition, arbitrary reflection, unreliable/event delivery
modes, ownership transfer, deterministic rollback and internet deployment remain
separate work. Client events are application effects, not a persistent event bus.
