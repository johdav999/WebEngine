# Physics joints

`PhysicsWorld` provides optional, portable Jolt-backed hinge, slider, spring and
grab constraints. Settings and `PhysicsJointId` live in
[`physics_joint.hpp`](../include/engine/physics_joint.hpp); creation and control are
methods on [`PhysicsWorld`](../include/engine/physics.hpp). No Jolt or browser
types enter the public interface.

## Creation and coordinates

Add rigid-body/collider components normally, then call `scene.synchronize()` to
register them before creating joints. Creation validates every setting and both
endpoints before publishing a constraint. A disabled, stale, foreign, unregistered,
sensor or pending-removal endpoint is rejected. Connecting a body to itself is
rejected. At least one endpoint must be dynamic; static/kinematic pairs cannot
respond to a constraint.

`bodies.first` is a required rigid-body **component** handle. `bodies.second` is
another rigid-body handle or exactly `ComponentId{}` for the fixed world. Anchors
and axes are supplied in world coordinates at creation. Each anchor then stays
attached to its body's material point, including rotated bodies and colliders
with offset centers of mass. The reference axis follows the second body (or
stays fixed when attached to the world). Axes are normalized; zero/nonfinite
axes are rejected. Simulation poses determine constraints and measurements;
render interpolation, culling and mesh detail do not affect them.

```cpp
#include "engine/physics.hpp"
#include "engine/scene.hpp"

// doorBody is an existing RigidBodyComponent, with an active collider.
scene.synchronize();
engine::HingeJointSettings hinge;
hinge.bodies.first = doorBody.id(); // second={} anchors to the world
hinge.anchor = {0, 1, 0};
hinge.axis = {0, 1, 0};
hinge.minAngle = 0;
hinge.maxAngle = 1.57f;
auto joint = scene.physics().createHinge(hinge);
// Run the normal scene fixed updates; forces/contact drive the door.
float angle = scene.physics().hingeAngle(joint);
scene.physics().removeJoint(joint);
```

| API | Contract |
| --- | --- |
| `createHinge(HingeJointSettings)` | Fixes a shared anchor, permits rotation about one axis. Creation orientation defines zero. `minAngle` is in [-pi,0], `maxAngle` in [0,pi], in radians. Optional nonnegative friction torque is in N m. `hingeAngle(id)` measures first body relative to second. |
| `createSlider(SliderJointSettings)` | Permits translation along one axis and locks relative rotation. Creation anchor defines zero. Finite `minDistance <= 0 <= maxDistance` limits travel in world units; defaults allow unrestricted travel. Optional nonnegative friction force is in N. `sliderPosition(id)` measures first anchor relative to second along the reference axis. |
| `createSpring(SpringJointSettings)` | Couples two independently supplied anchors at a nonnegative rest length, with rotation free. Nonnegative frequency is in Hz; damping ratio is in [0,1]. Positive frequency produces a soft spring; zero frequency produces a hard distance tether. |
| `createGrab(GrabJointSettings)` | Holds a selected body point at a world target through a hard point constraint, leaving rotation free. `anchor` selects the original material point; `target` is its desired world location. `setGrabTarget(id,target)` moves the target and wakes the body. |

Creation wakes moving endpoints. Defaults suppress collision response between
connected bodies. Set `bodies.collideConnected=true` to permit ordinary collisions;
the simulation layer matrix still applies. Multiple joints between a pair suppress
collisions if any of them has `collideConnected=false`. Creation/removal invalidates
cached contacts and wakes moving endpoints, so the next fixed step uses the new
policy. Spatial query filters remain independent of joint collision suppression.
Joints use the existing iterative fixed-step solver; limits and targets have finite
solver tolerances, and large impulses/target jumps can cause transient error.

## Ownership, cancellation and application policy

The physics world owns constraints. Returned IDs are non-owning, scoped to one
world, and never reused within that world. `hasJoint(id)` reports whether the
identity and its current endpoints are valid. `removeJoint(id)` releases the
stored constraint and returns false when the identity is absent or foreign.
Coordinate getters and retargeting reject stale/foreign IDs and wrong joint types.
`stats().joints` counts joints with current active endpoints. Each world permits
up to 4,096 stored joints; exceeding the limit throws without removing valid joints.

Body/collider disabling, detachment, destruction, owner/scene transfer or a body
rebuild releases its constraints **before** the Jolt body is destroyed. Settings
and collider-shape changes invalidate joint lookup immediately; synchronization
then rebuilds the body and permanently removes the old joint. Re-enabling a body
does not resurrect joints. Scene teardown releases every constraint. Applications
must explicitly recreate intended joints using the new live body handles.

Creation, removal and target updates run on the world's owner thread outside the
solver. They are supported in normal gameplay ticks and collision/trigger callbacks
after solver locks are released. `destroy()` from a callback still uses the scene's
guarded deferred-removal path. No callbacks or input subscriptions are added.

```cpp
// hit.body and hit.point can come from PhysicsWorld::raycast.
engine::GrabJointSettings grab;
grab.body = hit.body;
grab.anchor = hit.point;
grab.target = hit.point;
auto held = scene.physics().createGrab(grab);
scene.physics().setGrabTarget(held, desiredWorldPoint);
// On pointer release, blur, cancellation, pause or scene transition:
scene.physics().removeJoint(held);
held = {};
```

Applications own picking, pointer-to-world mapping, target speed, release/cancel
policy, snapping and puzzle legality. The grab is a hard point constraint with
free rotation; it has no force cap or compliant spring tuning. Motor drives,
break thresholds, runtime limit reconfiguration, rope distance ranges, joint
components/prefab factories and built-in joint archive codecs remain extensions.
These are runtime world objects: scene/application archives do not persist joints
or their IDs. Restore game joint definitions separately and recreate them after
body registration. Physics restoration remains independent of deterministic solver
checkpoints.

## Validation

`tests/physics_joint_tests.cpp` exercises world/two-body joints, hinge and slider
limits, free/constrained axes, damped and hard springs, moving kinematic references,
grab target updates/sleep/off-center anchors, connected collisions, invalid settings,
stale/foreign identities, rebuild/removal/transfer and callback mutation.
It also covers friction, rotated offset center-of-mass frames, capacity overflow
without partial publication and teardown with multiple attached constraints.

```powershell
cmake --build build/native --target physics_joint_tests physics_tests movement_tests scene_archive_tests
ctest --test-dir build/native -R '^(physics_joints|physics|movement|scene_archive)$' --output-on-failure
# Configure with the repository-local Emscripten SDK as described in README.md.
cmake --build build/web-joints-release --target physics_joint_wasm_tests
node scripts/physics-joints-smoke.cjs build/web-joints-release/joint-tests
```

The explicit Wasm target is excluded from normal builds and does not publish
`dist/`. The smoke script uses the same Playwright environment and optional
`BROWSER_EXECUTABLE` as existing tests, serves its artifacts on a temporary local
port, checks the C++ assertion result and browser runtime errors, and writes
`build/evidence/physics-joints/results.json`. This verifies browser/Wasm solver
execution; mechanical-game UI and physical Android/iPhone acceptance require
their own checks.

### Verified in this checkout, 2026-10-10

- Debug native `build/native-physics`: joint, physics/spatial/sensor, movement and
  scene-archive suites passed (4/4). Evidence:
  `build/evidence/physics-joints/native-ctest.txt`.
- Release Wasm `build/web-joints-release`: the joint and existing physics suites
  passed in Chrome 154.0.8037.99 with exit code zero and no runtime errors.
  Evidence: `build/evidence/physics-joints/results.json` and
  `build/evidence/physics-joints/spatial-regression/results.json`.
- These runs cover the portable solver contract. No mechanical-puzzle UI,
  physical mobile device or sustained performance acceptance was performed.
