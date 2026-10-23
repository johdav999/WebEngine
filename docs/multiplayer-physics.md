# Server-authoritative rigid bodies and collisions (M9)

The scene server now runs the existing Jolt world for replicated rigid bodies.
Clients receive authoritative poses and velocities and render passive replicas.
This is shared C++ functionality on browser and native Windows, with no renderer,
browser API or Jolt implementation type in the portable network interfaces.
See [physics contracts](physics.md), [runtime prefabs/RPCs](multiplayer-runtime.md)
and [validation](multiplayer-runtime-validation.md).

## Authoring and simulation

Use the existing Add body/Add collider controls and typed physics properties in
either editor. The profile admits one paired RigidBodyComponent/ColliderComponent,
directly beneath a root actor, with the ordinary physics unit-scale and transform
contracts. Unconfigured static ground bodies may remain bootstrap content. Moving
bodies must belong to a replicated Server actor or Runtime prefab. Static triangle
meshes are permitted; moving mesh colliders remain unsupported by the solver.
Malformed/missing pairs, nested bodies, invalid scale and unsupported components
reject before publication. Settings and geometry use the existing scene archive.

The sample selected player retains the earlier kinematic movement/prediction model
and does not accept a rigid body. Physics props and persistent server actors collide
with the authored world and each other. Collision-aware character movement is a
separate game/controller integration; this milestone does not silently change the
sample player's movement rules.

Server Application fixed traversal owns gameplay, solver steps, contact delivery
and authoritative actor poses. The network service wakeup does not supply solver
time. Snapshot capture reads current actor transforms and actual linear/angular
velocities; simulation does not depend on relevance, culling or presentation.

The example prefab spawns above the floor with server-selected initial health.
**Push mine** sends a registered impulse RPC targeting the caller's own player.
The server validates the referenced prefab owner, live dynamic body, finite impulse
and magnitude <=10 before calling the existing body impulse API. Clients cannot
send poses, velocities, contacts or unrestricted forces as authoritative state.

Only actual server CollisionPhase::Begin callbacks generate collision events,
after solver locks are released. Contact point and closing speed come from Jolt.
The application owns/chains the callback and restores it before Arena teardown;
callbacks cannot outlive the scene. Transient contact queues are bounded and may
drop notifications under overload; authoritative body state still comes from
snapshots. No gameplay authority comes from client-side collision detection.

## Portable state and passive clients

Include `engine/network_physics.hpp` and link WebEngine::NetworkScene.
`capturePhysics(actor)` returns ReplicatedPhysics (BodyMotion, enabled, world-space
linear/angular velocity). `encodePhysics`/`decodePhysics` use a strict 28-byte tail:
one uint32 motion/enabled flags word and six finite bounded float32 values.
Unknown flags, invalid motion, truncation, NaN/Inf and values beyond absolute 1e6
reject. This is state projection, not a solver checkpoint, contact-cache transfer
or deterministic rollback representation.

NetworkScene infers each replicated template's optional physicsMotion from its
validated body. The sample MP09 record retains pose (36 bytes), movement ack
(4 bytes), component count/entries, then the 28-byte physics tail only for body
templates. Clients validate the expected motion and complete component state before
publishing session/history. Declared maximum state, including this tail, must fit
the existing 256-byte per-record budget: minimum 44 bytes without physics and
72 bytes with it. WES4 and scene archive versions remain unchanged.

`preparePhysicsReplica(actor)` disables body and collider on a detached clone
before adoption. `preparePhysicsClient(scene)` disables all bootstrap physics and
synchronizes release before starting client simulation. Renderer-facing actors
still receive snapshot poses and M4 interpolation. Authoritative velocities remain
network presentation/diagnostic data; clients do not feed them to a competing
solver. The live matrix checks zero client solver bodies and unit tests prove no
local falling/drift after projection.

Leave, relevance re-entry, scene replacement and authenticated recovery retain the
existing handle/lifetime rules. Recovery reconstructs current body state for the
same network identity; it does not restore or replay a client solver history.
Physics settings/topology are authored and frozen per session. Runtime motion-type
changes, joints, vehicles, character rollback, client body prediction, extrapolation
and cross-machine deterministic solver replay are outside this profile.
