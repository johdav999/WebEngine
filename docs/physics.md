# Physics

WebEngine links pinned Jolt Physics **5.3.0** into `engine_core`, both natively
and in the browser's existing Wasm module. Physics uses the CPU, a single-threaded
job system, and no JavaScript object bridge or WebGPU compute dependency. The
browser build still needs no cross-origin isolation headers.

## Add a body

```cpp
#include "engine/scene.hpp"
#include "engine/physics.hpp"
#include "engine/collider_component.hpp"
#include "engine/mesh_component.hpp"
using namespace engine;

Scene scene{"Physics scene"};
auto& floor = scene.spawn("Floor");
floor.transform.position = {0, -0.5f, 0};
RigidBodySettings ground;
ground.motion = BodyMotion::Static;
floor.addComponent<RigidBodyComponent>("Ground body", ground);
floor.addComponent<ColliderComponent>("Ground collider",
    CollisionShape::box({10, 0.5f, 10}));

auto& crate = scene.spawn("Crate");
crate.transform.position = {0, 4, 0};
crate.addComponent<MeshComponent>("Visual", Mesh::cube());
auto& body = crate.addComponent<RigidBodyComponent>();
crate.addComponent<ColliderComponent>("Collider",
    CollisionShape::box({0.5f, 0.5f, 0.5f}));

scene.update(0); // Reconcile lifecycle and create bodies before issuing commands.
body.addImpulse({2, 0, 0});
```

Physics units are metres, kilograms, and seconds; world up is +Y. Default gravity
is `{0, -9.81f, 0}`. Box dimensions are **half extents**, spheres use radius, and
capsules use half the cylindrical height followed by radius, aligned along +Y.
Visual meshes are independent of colliders. Size visual child meshes to match
their collision geometry; the actor itself stays at unit scale.

## Ownership and motion

Each scene lazily owns one `PhysicsWorld`. Calling `scene.physics()` creates it;
adding an active rigid body also creates it on the next `update()` or
`fixedUpdate()`. Scenes without physics keep the existing allocation-free warm
update/collection path. World initialization reserves an 8 MiB scratch allocator
with malloc fallback and bounded body/contact storage.

The initial contract is one active rigid body and one active collider, both
**direct components of an actor directly under the scene**. The body component's
transform is identity. The collider can have a local position and rotation but
must have unit scale. Actors and the scene root require unit world scale and no
shear or reflection; invalid hierarchies/transforms throw before simulation.
An absent, disabled, removed, or nested collider leaves its body inactive.

- **Static:** infinite-mass geometry; actor transform changes synchronize on the
  next fixed step. `teleport()` updates a registered body immediately.
- **Dynamic:** the solver owns position and rotation and writes the actor's world
  pose after each fixed step. Use `teleport()` rather than assigning the actor's
  transform to move the body. Forces, impulses, and velocity setters wake it.
- **Kinematic:** gameplay supplies the next target with `moveKinematic(worldPose)`;
  the solver derives velocity and reaches the target in the next fixed step. A
  changed actor transform can also supply a target. It then holds that position.

`RigidBodySettings` controls mass, friction, restitution, damping, gravity factor,
collision layer, sensors (`isSensor`), and optional continuous collision (`LinearCast`). Use
`configure()` to replace validated settings atomically. Reconfiguration or
changes to collider shape/local pose recreate the solver body on reconciliation,
resetting velocity and interpolation. Disable/re-enable also recreates the body.
Detachment and scene transfer release the old body immediately. Deferred
destruction disappears from physics queries immediately and removes the body
when the scene flushes pending destruction.

`CollisionShape` assets are immutable and shareable. Boxes, spheres, capsules,
and convex hulls support moving bodies. Hulls accept 4–256 points and Jolt validates
their geometry when cooked. Triangle meshes accept indexed, nondegenerate
triangles and are restricted to static bodies. Identical shared geometry and
collider poses reuse a cooked Jolt shape within a world.

## Fixed steps and rendering

Use an application-owned `FixedStepClock`, separate from `Scene::update()`:

```cpp
FixedStepClock clock; // 60 Hz; at most eight catch-up steps per frame.

// Once per frame, after input:
scene.update(deltaSeconds); // Lifecycle, variable ticks, animation.
clock.advance(scene, deltaSeconds); // Gameplay fixedTick, solver, contact events.
// Render/collect the scene afterwards.

// On pause, visibility changes, or restarting a frame clock:
clock.reset();
if (auto* physics = scene.physicsIfCreated()) physics->setInterpolationAlpha(1);
```

The clock caps incoming frame time at 0.25 seconds, runs a bounded number of
steps, discards excess catch-up time, and retains the fractional remainder.
`droppedSeconds()` reports discarded time. Reset the clock on hidden/paused
transitions; do not feed background-tab time back into it.

`Scene::fixedUpdate()` prepares components and bodies, runs opted-in gameplay
fixed ticks, reconciles mutations, steps physics, writes authoritative poses,
dispatches queued contacts, and finally flushes deferred destruction. Fixed
updates do not advance animation clocks. Physics steps must be in `(0, 0.25]`.

Scene collection interpolates previous/current solver poses using the clock's
fractional remainder, including descendant meshes, lights, and particles. This
does not change authoritative actor transforms. Explicit fixed-step callers can
set interpolation alpha themselves; its default is 1 (current pose).

## Queries, layers, and collision events

```cpp
scene.physics().setLayerCollision(0, 2, false); // Symmetric pair exclusion.
auto hit = scene.physics().raycast({0, 5, 0}, {0, -1, 0}, 20);
if (hit) {
    auto* actor = scene.findActor(hit->actor);
    // hit->body, point, normal, distance; generation-checked scene handles.
}
scene.physics().onCollision = [&](const CollisionEvent& event) {
    if (event.phase == CollisionPhase::Begin) {
        if (auto* actor = scene.findActor(event.first)) actor->destroy();
    }
};
```

There are 16 collision layers, separate from rendering visibility masks. All
pairs start enabled; static/static pairs are excluded. Raycasts normalize their
direction, return the closest live hit, and optionally take a 16-bit layer mask.
Query poses are authoritative simulation poses, not interpolated render poses.

### Spatial queries

Raycasts, overlaps, and linear shape sweeps share `PhysicsQueryFilter`:

```cpp
PhysicsQueryFilter filter;
filter.layers = (1u << 0) | (1u << 2);
filter.excludedActors = {player.id()};
filter.excludedBodies = {anotherBody.id()};
filter.includeSensors = false;
// Also available: includeSolids, includeStatic, includeDynamic, includeKinematic.

auto ray = scene.physics().raycast({0, 5, 0}, {0, -1, 0}, 20, filter);
const auto probe = CollisionShape::capsule(0.6f, 0.3f);
Transform pose;
pose.position = {0, 1, 0};
auto overlaps = scene.physics().overlap(*probe, pose, filter);
auto sweep = scene.physics().sweep(*probe, pose, {1, 0, 0}, 5, filter);
if (sweep) {
    // actor/body handles, target surface point, outward target normal,
    // distance travelled, fraction, penetrationDepth, startedOverlapping.
}
```

Query geometry supports boxes, spheres, capsules, and convex hulls. Poses use
world position/Euler rotation and require unit scale. Hulls retain their shape
origin even when their center of mass is offset. Targets can include static
triangle meshes; triangle meshes cannot be query geometry. Tests use Jolt's
narrow phase, not just bounding boxes. Mesh triangle hits deduplicate into one
deepest overlap per body. Overlaps are sorted by component handle; points and
normals are in world space, and penetration depth is nonnegative.

Sweeps translate at a fixed orientation, normalize direction, and return the
closest hit within the positive maximum distance. Initial penetration returns
zero distance/fraction with `startedOverlapping=true`; coincident centers may
have an arbitrary contact normal. Use overlaps for a stationary probe. Invalid
poses, directions, distances, or unsupported geometry throw before querying.

All query categories default to included. The layer mask is independent of the
simulation layer-pair matrix. Actor/body exclusions compare full generation and
scene handles; stale or foreign exclusions cannot hide a replacement body.
Disabled, removed, and pending-destroy participants disappear from queries
immediately. Configure/spawn/shape edits synchronize through `update(0)` or the
next fixed step; `teleport()` updates a registered body's query pose immediately.
The original layer-mask raycast overload remains available.

### Sensors and trigger events

```cpp
auto& zone = scene.spawn("Pickup zone");
RigidBodySettings sensor;
sensor.motion = BodyMotion::Static;
sensor.isSensor = true;
zone.addComponent<RigidBodyComponent>("Sensor body", sensor);
zone.addComponent<ColliderComponent>("Sensor volume", CollisionShape::sphere(2));
scene.update(0);

scene.physics().onTrigger = [&](const TriggerEvent& event) {
    if (event.phase == TriggerPhase::Enter) {
        if (auto* actor = scene.findActor(event.other)) {
            // Apply gameplay effects; destroy() and queries are safe here.
        }
    }
};
```

Sensors use the existing body/collider ownership contract and convex geometry.
They produce no solver response and no `onCollision` events. Static, dynamic,
and kinematic sensors are supported. Sensor pairs obey the symmetric simulation
layer matrix, including stationary static/static overlaps. A pair contains the
sensor actor/body and other actor/body handles. Two sensors produce one directed
pair for each sensor; self-overlaps are excluded.

After each fixed step, sensor narrow-phase overlaps are compared with the
previous set. `Enter` occurs once while a body remains inside, including sleeping
bodies and meshes with multiple contact triangles. `Exit` occurs on separation,
layer exclusion, disabling, removal, destruction, or scene transfer, on the next
fixed step in the original world. Exit retains the original handles even if they
no longer resolve; always check scene lookups. Trigger history and subscriptions
are runtime state; loading a scene starts fresh, with Enter on its first fixed step.

Delivery follows collision callbacks, after solver locks are released and under
the scene traversal guard. Callbacks may replace themselves, query, and request
deferred destruction. Enter delivery skips participants destroyed by an earlier
callback. Trigger pairs are bounded at 8,192; exceeding the limit throws instead
of silently dropping transitions. Detection samples final poses per fixed step;
an object crossing completely through a sensor between samples can be missed.
Use a sweep for fast gameplay probes. Sweeps do not rotate shapes along the path.

Contact `Begin`, `Stay`, and `End` events contain actor and body-component handles.
`Begin`/`Stay` include a world point and normal from the first body toward the
second; `End` has zero vectors. These are contact-manifold events: complex shapes
can produce more than one event for a body pair, and sleeping contacts need not
emit `Stay` every step. Delivery occurs after Jolt releases body locks, under the
scene traversal guard. `destroy()` is safe; detachment/reparenting and recursive
scene updates are forbidden. Stale, inactive, or pending-destroy body pairs are
discarded before delivery. The callback can remove or replace itself.

Limits per world are 4,096 bodies, 8,192 body pairs/contact constraints, and 8,192
queued events. Event overflow increments `PhysicsStats::droppedEvents`; solver
capacity overflow throws. `stats()` reports live/awake bodies, cumulative fixed
steps, and the last solver step duration. These timings do not include rendering
or shape cooking and are not a cross-device performance guarantee.

## Playground and collider drawing

Use **Physics playground** in the browser inspector, or open `?scene=physics`.
It starts six falling boxes/spheres on a floor. Add bodies, reset, click/tap to
apply an impulse, and use the existing **Pause motion** control to pause/resume.
Returning to the model, demo, or material showcase disables the playground's
solver bodies. The playground is limited to 64 moving bodies.

`setDebugDrawEnabled(true)` adds unlit collider outlines to the existing overlay
render pipeline. Primitive shapes use economical outline rings/edges; hulls and
meshes use cooked triangles. Drawing is capped at 768 lines per collected frame;
`debugLines(output, limit)` exposes bounded geometry to another debug renderer.
The overlay intentionally draws above world geometry and never casts shadows.

## Dependency and verification

`cmake/jolt.cmake` downloads Jolt 5.3.0 via CMake FetchContent and verifies its
SHA-256 hash. The first configure needs network access. For an offline source
checkout, pass `-DFETCHCONTENT_SOURCE_DIR_JOLT=/absolute/path/to/JoltPhysics-5.3.0`.
Jolt stays outside public engine headers and its build options do not override
the engine's RTTI/exception requirements. Browser artifacts include
`Jolt-LICENSE.txt` (MIT).

Native regression coverage is in `tests/physics_tests.cpp`; run the normal
CMake/CTest commands. `scripts/physics-smoke.cjs` checks actual Chrome/WebGPU
pixels, falling/contact behavior, pause/resume, creation/reset/removal, click
picking/impulses, collider display, and bounded frame-gap handling. It uses the
same Playwright/pngjs environment as the existing smoke scripts:

```powershell
$env:NODE_PATH = 'path/to/test/node_modules'
$env:BROWSER_EXECUTABLE = 'C:/Program Files/Google/Chrome/Application/chrome.exe'
$env:DEMO_URL = 'http://localhost:8080'
node scripts/physics-smoke.cjs
```

Evidence defaults to `build/evidence/physics/`. Browser acceptance is scoped to
the tested browser/GPU; physical Android/iOS behavior remains a device check.
See [movement controllers](movement.md) for the optional character motor.
Compound colliders, joints, ragdolls, vehicles, physics
workers, and physics-state serialization are future extensions.

### Verified in this checkout, 2026-10-10

- Isolated Debug native build `build/native-physics`: **19/19 CTest suites passed**,
  including physics motion, friction, restitution, CCD, ray/layer filtering,
  deferred destruction, cross-scene transfer, collider reparent/removal,
  interpolation, clock bounds, and playground lifecycle.
- Isolated Release Wasm build `build/web-physics-release`: succeeded; the Jolt
  MIT license is published as `dist/Jolt-LICENSE.txt`.
- `scripts/physics-smoke.cjs`: **8/8 checks passed**, Chrome 154.0.8037.99;
  `build/evidence/physics/results.json` and screenshots contain the evidence.
- Existing `scripts/browser-smoke.cjs`: **14/14 checks passed**, with no console
  or GPU errors. Its baseline disables shadows/culling to isolate its original
  rendering expectations; the physics smoke runs the default renderer settings.
- A clean `build/physics-fetch-check` configure downloaded and verified the
  pinned source archive without an offline override. The WinLibs-bundled CMake
  in this environment has no TLS trust anchors; the installed Windows CMake at
  `C:/Program Files/CMake/bin/cmake.exe` successfully uses the system trust store.
  TLS verification was retained.

The local preview serves the isolated snapshot in `build/physics-browser-site`
at `http://localhost:8127/?scene=physics`. It does not depend on another build
replacing `dist/`. The same uncommitted checkout contains this implementation
alongside concurrent rendering work; no commits or branch changes were made.

### Spatial-query and sensor verification, 2026-10-10

`tests/physics_tests.cpp` now also covers shared filters and actor/body exclusions,
stale/foreign handles, initial penetration, rotated box/capsule/hull queries,
offset hull centers of mass, mesh targets, narrow-phase misses, immediate disabled
and pending-destroy filtering, sensor motion without response, static/static and
sleeping overlaps, kinematic sensor movement, mesh-pair deduplication, sensor/target
removal and transfer, layer changes, callback destruction/replacement, and recovery
after a callback exception. The focused native physics/archive suites pass.

`scene_archive_tests` verifies sensor settings plus genuine version-1 scene and
asset-library fixtures. The same physics regression source also passes in Chrome
154.0.8037.99 as Release Wasm, with no runtime errors:

```powershell
# Configure an Emscripten build as usual, then build this explicit target:
cmake --build build/web-spatial-release --target spatial_query_tests
# Requires the same Playwright environment as other repository smoke scripts.
node scripts/spatial-query-smoke.cjs build/web-spatial-release/spatial-tests
```

Browser evidence is `build/evidence/spatial-queries/results.json`. This harness
executes C++ assertions in the browser; it does not claim sensor UI or physical
mobile-device acceptance. The `spatial_query_tests` target is excluded from normal
browser builds and does not publish the playground to `dist/`.

The latest full native build completed and CTest passed 29/31 suites; selection
and application suites failed in the concurrently changing checkout. Physics,
scene archives, movement, and camera controllers passed. The captured full result
is `build/evidence/spatial-queries/native-ctest.txt`; focused spatial-query evidence
does not establish that the whole checkout is green.
