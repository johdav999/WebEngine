# Camera controllers

`CameraController` composes optional world-space behaviours over a reusable
`CameraComponent`. It belongs to `engine_core` and consumes targets, parameters,
delta seconds and viewport aspect. Applications map input actions or AI commands
to those parameters and choose update/pause policy.

## Compose a following camera

```cpp
#include "engine/camera_controller.hpp"
#include "engine/scene.hpp"
#include "engine/physics.hpp"
using namespace engine;

Scene scene;
auto& subject = scene.spawn("Subject");
auto& camera = scene.addChild<CameraComponent>("Camera");
camera.transform.position = {0, 3, 8};
camera.lookAt({0, 0, 0});
CameraController rig(scene, camera.id());

auto& follow = rig.add<FollowCameraBehaviour>();
follow.target = CameraTarget(subject.id());
follow.offset = {0, 3, 8};
follow.offsetInTargetSpace = true;
auto& track = rig.add<TrackingCameraBehaviour>();
track.target = follow.target;
auto& smooth = rig.add<SmoothingCameraBehaviour>();
smooth.positionHalfLife = 0.15f;
auto& collision = rig.add<CollisionCameraBehaviour>();
collision.ignoredActor = subject.id();
collision.radius = 0.3f;
collision.layers = 0xffff;

FixedStepClock clock;
// Once per application frame:
scene.update(deltaSeconds);
clock.advance(scene, deltaSeconds);
const auto viewport = camera.pixelViewport(canvasWidth, canvasHeight);
rig.update(cameraDeltaSeconds, viewport.width / viewport.height);
// Render after the camera update.
```

The two delta variables and canvas dimensions above are supplied by the
application. Use presentation delta for cameras that keep moving during a
simulation pause, or zero delta to hold temporal smoothing/transitions. A zero
delta still evaluates composition and collision constraints. With the optional
`Application` runtime, a camera update can run in `afterSimulation` before
`present`; controller ownership stays with application code.

## Behaviours and execution

| Behaviour | Result |
| --- | --- |
| `OrbitCameraBehaviour` | Places the camera at yaw/elevation/distance around a target and aims at it. Radians, +Y elevation, yaw zero on +Z; defaults match the legacy orbit camera. |
| `FollowCameraBehaviour` | Updates position and collision focus from a target and an offset; orientation remains available for a separate tracking behaviour. Offsets can be world-space or target-local. |
| `TrackingCameraBehaviour` | Aims at a separate target with configurable world up, without moving the camera. Coincident points keep orientation; vertical directions use a stable up fallback. |
| `FramingCameraBehaviour` | Fits a group of world-space target spheres conservatively, including near/far clipping. Perspective moves the camera along its current viewing direction; orthographic also adjusts height. |
| `SmoothingCameraBehaviour` | Exponential half-life damping for position/focus, quaternion orientation and lens. Zero half-life snaps; positive half-life holds at zero delta. |
| `CollisionCameraBehaviour` | Sweeps a camera sphere from focus to candidate eye position, shortens the boom by hit distance minus clearance and aims at focus. |
| `CameraController::transition` | Blends from a captured displayed pose toward the stack's live result, including orientation, focus and lens, using linear or smoothstep progress. |

Each update evaluates all enabled **Compose** behaviours in insertion order,
then **Smooth**, then the controller's transition, then **Constraint** behaviours.
Collision belongs to the final stage even when inserted earlier in the stack.
Multiple behaviours writing the same field deliberately apply in that order.
Smoothing uses the displayed pose at frame start. Usually add one smoothing
behaviour. Collision takes priority over framing and smoothing: an obstruction
may force the camera too close to fit every target.

Behaviour evaluation and local camera conversion use a candidate pose. Invalid
parameters, transforms, lens data, impossible framing or invalid query results
throw `invalid_argument` and leave the camera and transition clock unchanged.
Re-entering evaluation or editing controller storage from a callback throws
`logic_error`. Custom behaviours derive from `CameraBehaviour`, select a stage
and implement `apply`; they must only modify the supplied candidate, with no
scene mutations or private temporal side effects.

## Modes, framing and transitions

```cpp
auto& orbit = rig.add<OrbitCameraBehaviour>();
orbit.enabled = false;
orbit.target = follow.target;
orbit.distance = 12;

// Capture first, then change the mode. Repeating transition() interrupts smoothly
// from the currently displayed pose. Targets may keep moving during the blend.
rig.transition(0.6, CameraTransitionCurve::SmoothStep);
follow.enabled = false;
track.enabled = false;
orbit.enabled = true;

// Frame several subjects instead of following a fixed offset:
auto& frame = rig.add<FramingCameraBehaviour>();
frame.enabled = false;
frame.targets = {{CameraTarget(subject.id()), 1.2f},
                 {CameraTarget(Vec3{4, 1, -2}), 0.8f}};
frame.padding = 1.15f;
```

Framing ignores unavailable targets; an empty/live-empty group leaves the pose
alone. Radii are world units, independent of target scale. It computes a
conservative enclosing sphere with padding, fits the narrower horizontal or
vertical field of view, and rejects a group that cannot fit the clipping range.
Use the actual viewport aspect for split views. Full framing guarantees assume
a rigid camera world transform: scaled/sheared camera ancestry distorts the
projection. Temporal smoothing can temporarily leave targets outside the frame.

`transition(0)` cuts on the next update, `cancelTransition()` removes the blend,
and `clear()` removes behaviours, clears focus history and cancels transitions.
Disabled/unavailable cameras return `false` without advancing time. Projection
mode and clipping planes remain properties of `CameraComponent`; transitions
blend FOV or orthographic height within its current mode. Cross-projection
transitions are not defined. Configure both modes explicitly when switching.

## Ownership and targets

The controller owns behaviours with `unique_ptr`. References returned by `add`
remain valid until removal, `clear`, or controller destruction. Handles are
resolved in the camera's scene each frame; disabled, foreign, stale and
pending-removal targets are unavailable. `CameraTarget::offset` is local to the
resolved target; a fixed world point uses an identity basis. Plain `Node`
targets can be supplied as sampled world points; registered actors/components
can be followed through handles.

`CameraController(scene, camera.id())` safely stops updating a removed camera,
but the scene must outlive the controller. The `CameraComponent&` constructor
supports standalone cameras and borrows the camera; that camera must remain
alive. There is no automatic scene tick or global camera selection. Controller
poses convert to camera parent space and preserve local scale. Singular camera
or ancestor transforms are rejected. Only the camera is moved; subjects and
physics simulation poses remain authoritative.

## Collision and spatial backends

An empty collision query uses `scene.physicsIfCreated()` and the generic
`PhysicsWorld::sweep` with a cached `CollisionShape::sphere`. Camera use alone
does not create a physics world. The sweep returns the closest live body in
the requested layer mask and excludes the configured subject actor. Sensors
are excluded unless `includeSensors` is enabled. Queries use simulation poses.
A camera radius detects edge obstacles that a centre ray misses.

Initial overlap returns zero travel; the camera stays at focus. This is boom
shortening, not a general penetration solver or a sweep from the previous eye
to the new eye. Applications choose a focus outside obstacles and a radius
that covers their desired camera/near-plane clearance.

Applications without Jolt bodies can install `collision.query`. It receives
`CameraCollisionRequest` and returns sphere-centre travel in `[0, length(to-from)]`
or `nullopt` for no hit. Capture lifetimes are the application's responsibility;
clear the callback or destroy/remove the behaviour before captured services die.
Queries must not mutate the scene/controller. Controller destruction cancels all
owned callback/transition state. Runtime behaviours/callbacks are not archived;
applications reconstruct the stack and resolve new handles after scene loading.

The existing `OrbitController` remains the playground's parent-space input/action
adapter and retains its archive contract. Use one camera writer per frame;
applications using the new stack translate actions into orbit parameters instead
of running both controllers over the same camera.

## Validation

`camera_controller_tests` exercises composition, actor/component target lifetime,
foreign/stale handles, disabled/deleted cameras, transformed parents, orbit poles,
perspective/orthographic group framing, timestep subdivision, quaternion blending,
live and interrupted transitions, swept-volume edge hits, subject/layer exclusion,
initial overlap, obstacle removal, custom queries and atomic failures. Existing
camera and physics suites cover compatibility. WebAssembly validation uses the
pinned Emscripten SDK; Node execution establishes portable Wasm behaviour, not
rendered browser or physical-device acceptance.

Verification on 2026-10-10:

- Native `camera_controller`, `camera` and `physics`: 3/3 passed, including
  sensor filtering, callback mutation guards and failed/disabled transition clocks.
- Release `engine_core` built using repository-local Emscripten 6.0.11 in
  `build/web-camera-controllers`; the controller test suite compiled and passed
  as WebAssembly under the SDK's Node 24.19.0 runtime.
- Full native snapshot: 30/31 passed. The separate `selection` suite failed
  with `Actor destruction requires an owning scene`.
- No rendered-browser or physical-device camera acceptance was performed.


## Presentation targets and authored follow rigs

[Stage 3](driving-controls.md) adds `CameraTarget::poseSource` (simulation by default),
read-only physical presentation matrices, bounded `TrackingCameraBehaviour` velocity
look-ahead and explicit application-owned `CameraFollowRig`. CameraComponent stores
validated authored settings, with editor history and v26 codecs; the application
still chooses one writer, update clock, viewport and reset/teardown policy.
