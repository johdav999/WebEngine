# Movement controllers

`CharacterMotorComponent` accepts `MovementCommand` values from gameplay code.
It has no dependency on keyboard, mouse, touch, gamepad, input bindings, AI, or
network transport. Attach it only to actors that need controlled movement.

```cpp
#include "engine/character_motor_component.hpp"
#include "engine/scene.hpp"
#include "engine/physics.hpp"

engine::Scene scene;
auto& actor = scene.spawn("Character");
actor.transform.position = {0, 2, 0}; // World foot position.
auto& motor = actor.addComponent<engine::CharacterMotorComponent>();

// An input adapter, AI, or network simulation supplies the same command.
motor.submitMovement({{3, 0, 0}, false}); // World-space metres per second.
scene.fixedUpdate(1.0 / 60.0);
const auto& result = motor.movementResult();
if (result.landed) { /* Play a landing effect. */ }

motor.submitMovement({{3, 0, 0}, true}); // Request one grounded jump.
motor.stop(); // Clear movement and pending jump when control is released.
```

Add static/dynamic/kinematic world geometry with the existing rigid-body and
collider components; see [physics](physics.md). Visual meshes are independent.
The motor owns an upright +Y capsule whose origin is the feet, with total height
`2 * (halfHeight + radius)`. Actor rotation affects visuals; the collision capsule
stays upright. The default capsule is 1.8 metres tall and 0.6 metres wide.

## Commands and simulation

- Velocity persists until replaced or stopped. Collision-aware mode accepts
  horizontal XZ velocity (`y == 0`). Speed, input normalization, camera-relative
  direction, acceleration, and facing belong to the command producer. Horizontal
  control also applies in the air.
- A `jump` submission latches one request until the next simulated motor step.
  A later command with `jump == false` cannot erase it. Requests in air or on a
  steep slope are consumed without jumping; there is no automatic jump buffering
  or coyote time. Repeated `jump == true` commands request repeated jumps, so an
  input adapter should send its press edge. `jumpSpeed == 0` disables jumping.
- `Scene::update()` reconciles registration without moving the motor.
  `Scene::fixedUpdate()` runs gameplay fixed ticks, advances rigid bodies, then
  solves collision-aware motors before dispatching body collision events.
  Free motors move during their component fixed tick. Submit commands before the
  fixed update, or from an owning actor's fixed tick.
- Movement steps must be in `(0, 0.25]` seconds. Use the engine's `FixedStepClock`
  or `Application` fixed-step loop. Catch-up steps reuse horizontal velocity and
  consume each jump request once. Motor components enable fixed ticks by default;
  `setFixedTickEnabled(false)` freezes both movement modes.

Collision-aware mode lazily creates the scene's existing `PhysicsWorld` and uses
Jolt's virtual character solver. It sweeps the capsule, recovers penetration,
slides along walls, rejects upward travel on steep slopes, walks sufficiently low
steps with overhead clearance, and snaps down within `groundSnapDistance`.
Ground snapping never pulls an ascending jump back to the floor. Walkable slopes
permit grounded jumping; steep contacts can support sliding without granting a
jump. A rounded step edge may briefly report `Steep` before snapping to its lower
walkable surface.

`CharacterMotorSettings` exposes capsule size, slope angle in radians, maximum
step height, snap distance, jump speed, per-motor gravity magnitude, padding,
mass, maximum push force, and collision layer (0–15). Set step height or snap
distance to zero to disable that behavior. Gravity is along -Y and independent
of `PhysicsWorld::gravity()`. Moving ground contributes its world contact
velocity while grounded. The motor can push dynamic bodies with bounded force.
Collision checks respect the world's symmetric layer collision matrix and
exclude removed, disabled, and pending-destroy geometry.

## Results

Read `movementResult()` after the fixed update. It contains the last simulated
step's data; calling `Scene::update()` alone does not produce a new movement result.

| Field | Meaning |
| --- | --- |
| `requestedDisplacement` | Velocity integrated for the step before collision response, including gravity, jump and moving-ground contribution |
| `displacement`, `velocity` | Actual world movement and displacement divided by step duration, including snap/step/recovery corrections |
| `groundState`, `grounded` | In air, walkable ground, steep ground, or unsupported contact; `grounded` permits normal grounded movement and excludes the jump launch step |
| `groundNormal`, `groundVelocity` | World-space contact normal and supporting/touched surface velocity, zero in air |
| `groundActor`, `groundBody` | Scene-scoped generation-checked handles to the supporting/touched rigid body, empty in air |
| `jumped`, `landed` | Accepted jump and transition to walkable grounding during this step |
| `hitWall`, `hitCeiling` | Actual obstructing contacts; predictive nearby contacts do not set these flags |
| `contactLimitExceeded` | Jolt discarded contacts after reaching its bounded query capacity; simplify geometry or inspect that area |

`movementCommand()` returns the persistent velocity command without the transient
jump edge. `stop()` cancels commands; it does not cancel gravity or an airborne
jump already in progress.

## Collision-free mode and lifecycle

Set `collisionAware = false` to apply full XYZ velocity directly. Free mode does
not simulate gravity, jumps or ground, and does not create a physics world. It
still produces requested/actual displacement and velocity results.

Both modes require one active motor as a direct component of an actor directly
under the scene. The component transform is identity, and the actor/scene world
transform has unit scale, no shear and no reflection. An active rigid body on
the same actor is rejected because both would own its position. The motor uses
its own capsule settings; no collider component is needed on its actor.

`configure()` validates atomically and resets simulation velocity, support,
pending jump and results while preserving the persistent velocity command. To
switch from a free XYZ command to collision-aware mode, replace/stop any vertical
command first. `teleport(worldFootPosition)` resets the same runtime state and
interpolation. Direct actor position edits also rebuild the virtual character on
reconciliation. Disabling, detaching, changing an existing owner, scene transfer,
or destruction releases the old character and clears commands/runtime state.
Initial attachment preserves commands submitted before registration.

Collision-aware motor meshes interpolate using the existing physics interpolation
alpha. Authoritative actor transforms and movement results always use solved
positions. Archives retain motor settings, actor transforms and persistent
velocity commands; contacts, vertical simulation velocity and pending jump edges
restart on loading.

Virtual characters collide with rigid-body geometry. They do not have broad-phase
rigid bodies, so body raycasts/contact events do not expose motors, and motors do
not collide with each other. There is no crouching, shape switching, root-motion
adapter or arbitrary-up gravity in this implementation.

## Validation, 2026-10-10

`tests/movement_tests.cpp` runs under CTest as `movement`. Native Debug and an
isolated Release WebAssembly executable both passed the same movement checks.
Coverage includes free motion without a physics world; persistent commands and
jump edge handling; falling/landing; wall sliding and high-speed sweep; ceiling
collisions; walkable/steep slopes; low/high/disabled steps and descent snapping;
layers and removed support; moving platforms; interpolation; disable/re-enable,
tick opt-out, scene transfer and deferred destruction; validation; mode switching;
and archive restoration.

The final native run at `build/native-physics` passed **29/31 suites**, including
movement, physics, archives and actor/component/rendering regression coverage.
The concurrently changing selection and application suites exited with runtime
code `541541187` and no diagnostic output; those broader-suite failures remain
outside the movement validation result. WebAssembly
validation used bundled Node 24.19.0 to execute
`build/wasm-movement/engine/movement_wasm_tests.js`; it is CPU/Wasm runtime evidence,
not an interactive browser or physical-device acceptance check.

To build the optional Wasm test runner in a regular browser build directory:

```powershell
emcmake cmake -S . -B build/web-movement -G Ninja -DCMAKE_BUILD_TYPE=Release -DWEBENGINE_BUILD_MOVEMENT_WASM_TESTS=ON
cmake --build build/web-movement --target movement_wasm_tests
node build/web-movement/movement_wasm_tests.js
```
