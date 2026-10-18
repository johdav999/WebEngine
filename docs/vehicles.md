# Wheeled vehicle foundation

The optional `VehicleComponent` in `engine_core` adapts Jolt 5.3.0 wheeled vehicle
constraints. Rendering is independent: the same fixed-step simulation runs with
WebGPU and WebGL 2. Public APIs contain no Jolt or browser types.

## Construction and commands

```cpp
#include "engine/vehicle_component.hpp"
#include "engine/rigid_body_component.hpp"
#include "engine/collider_component.hpp"
#include "engine/scene.hpp"
auto& car = scene.spawn("Car");
car.transform.position = {0, .95f, 0};
RigidBodySettings body;
body.mass = 1200;
body.centerOfMassOffset = {0, -.25f, 0};
body.continuousCollision = true;
car.addComponent<RigidBodyComponent>("Chassis", body);
car.addComponent<ColliderComponent>("Hull", CollisionShape::box({.8f,.25f,1.75f}));
auto& vehicle = car.addComponent<VehicleComponent>();
scene.synchronize();
vehicle.submit({.7f, .2f, 0, 0}); // throttle, steering, service brake, handbrake
// Scene::fixedUpdate / Application owns the actual physics step.
```

Metres, kilograms, seconds and radians; +Y up and local +Z forward. Exactly four
wheels are supported, ordered front-left, front-right, rear-left, rear-right.
Positive steering turns right. Throttle is [-1,1]; negative selects automatic
reverse. Brake and handbrake are independent [0,1] values. Automatic reversal is
Jolt transmission policy, not a brake-to-reverse input convention. Commands are
held until replaced/cancelled. Applications must submit fresh input or `cancel()`
on control/lifecycle handoffs. The browser example does this through InputSystem.

`VehicleDefinition` is published as a shared immutable resource. Copy it, edit the
candidate, then call `configure(shared_ptr<const VehicleDefinition>)`. It validates
before replacement. Never mutate an already published definition through another
mutable shared pointer. Each vehicle owns separate commands, telemetry and solver
state. Prefab built-ins register `VehicleComponent` with a shared definition as
configuration; bind visual IDs after instantiation.

Defaults: 500 N m engine, 1000–6000 RPM, five forward ratios
2.66/1.78/1.3/1/.74, reverse -2.9, differential 3.42, rear-wheel drive
(`frontTorqueFraction=0`), up/down shifts 4000/2000 RPM and axle anti-roll stiffness
1000. Wheels: .35 m radius, .2 m width, .1–.45 m suspension, 2 Hz/.6 damping,
front steering .5 rad, 1500 N m service brakes and 4000 N m rear handbrakes.
Front torque fraction selects front/rear/all-wheel distribution; individual wheel
brake torques are configurable. Cylinder casts detect wheel support, exclude the
chassis and sensors, and use scene collision layers. Tyre friction is configurable
per wheel. Stage 2 adds [physical surface mappings](physical-surfaces.md) and combined grip telemetry.

## Ownership, updates and reset

Use one identity-transform vehicle component with one solid dynamic body and one
box/convex-leaf compound collider directly on a scene-root actor. Existing unit-scale physics
topology applies. The world owns constraints, collision testers and step-listener
registrations. Missing/disabled collider or body suspends registration; adding a
valid replacement recreates it. A configured static/kinematic/sensor chassis is
invalid. No physics world is allocated merely by an unused vehicle definition.

Scene synchronization creates/removes registrations without advancing simulation.
Commands are supplied before fixed physics; one coherent `state()` snapshot is
published after each successful solver step and body pose writeback. It contains
signed forward speed in m/s, RPM, gear (-1 reverse/0 neutral/1–5 forward), and wheel
contact position/normal/actor handle, suspension length, steer/spin/angular velocity,
longitudinal slip ratio, lateral slip angle (radians) and local wheel pose.
Contact actor handles are scene-scoped snapshots: resolve before use. Inactive
state has `active=false`; contact data is rebuilt by simulation, not loading.

Disable/detach, definition replacement, body rebuild/removal and teleport release
registrations before the body and reset commands/contact/vehicle runtime state.
Body reconfiguration follows existing velocity-reset semantics. Re-enabling creates
a fresh drivetrain. `RigidBodyComponent::teleport` resets chassis and wheel
interpolation history, optionally clearing body velocity; call synchronize before
issuing new commands. Destruction and scene transfer cannot retain solver listeners.

## Physics body extensions

`addTorque` accepts world N m. `addForceAtPosition(force, worldPoint)` accepts world
newtons/metres and `addImpulseAtPosition(impulse, worldPoint)` accepts world N s.
`pointVelocity(worldPoint)` returns world m/s including angular motion about COM.
These reject invalid/inactive bodies; forces and impulses require dynamic motion.
They wake bodies and are not also applied by the vehicle adapter.

`centerOfMassOffset` offsets the cooked shape COM in actor-local metres without
moving collider geometry. `inertiaScale` is a positive local tensor-axis multiplier:
I' = S I S, S = diag(sqrt(scale)); it is not a principal-axis override. Default
{1,1,1} retains mass-scaled shape inertia. Both settings are validated, rebuild the
body on reconciliation, and persist in v24. Zero/negative scale is invalid.

## Wheel presentation

`setVisuals(array<string,4>)` binds optional stable saved IDs of direct nonphysics
chassis children; empty entries mean no mesh. Visual nodes remain ordinary nodes.
Their authored local transforms are offsets/scale after the sampled wheel pose.
Wheel and chassis previous/current rigid poses interpolate with the same alpha
inside scene collection. Neither changes authoritative transforms. Descendants of
wheel visuals inherit this presentation pose. Render masks, views, culling and LOD
never change wheel queries or drivetrain state.

Bindings are validated on registration. Removing a bound presentation node from a
registered vehicle does not stop simulation; clear/rebind that ID before rebuilding
or saving the vehicle. Debug drawing includes bounded suspension segments/contact
normals through the existing physics debug path.

## Editor and example

Open `?scene=vehicle` for the flat lane, hill, bank, kerb, ramp, wall and reset area.
The blue chassis and block wheels are diagnostic assets, not production car art.
The existing orbit camera follows the first vehicle. Vehicle foundation controls:
W/S throttle/reverse, A/D steering, Space brake, Shift handbrake; click the canvas.
The Driving controls toggle chooses the existing Gameplay/Camera context. It uses
portable actions; there is no vehicle-owned DOM input service or gamepad support.

Open `?editor=1&scene=vehicle` to edit the course. Select VehicleComponent for
engine/transmission/axle tuning and the four wheel groups, including attachment,
suspension, brakes, friction and visual saved IDs (`-` clears). Rigid body inspectors
include COM and inertia axes. The normal Add to actor list supports VehicleComponent
on a valid chassis; Vehicle foundation > Create vehicle preset creates an undoable
complete example actor. Undo/redo, removal, subtree duplication, files and Play use
existing editor commands. Authoring and file actions remain gated during Play.
Live telemetry is read-only, shows the first vehicle and is reported from the
isolated Play runtime. Stop restores the source document, selection and history.
The example reset is a runtime action and is unavailable in authoring mode.

## Persistence

Scene/application/effect envelope writer version is 27 (vehicle fields introduced in v24). Prior supported formats
remain readable; old bodies receive zero COM offset/unit inertia scales. Shared
vehicle definitions deduplicate through the existing resource table. V24 stores
wheel visual saved IDs and a partial checkpoint: RPM, current gear, wheel spin and
angular velocities, alongside existing chassis pose/velocities. `checkpoint()` /
`restore()` expose that supported state. Held commands, contacts, listener IDs,
clutch/shift timers, tyre solver impulses and render caches are not serialized.

Authoring files normally contain the unadvanced preset state. Runtime saves retain
the supported checkpoint; restoration stages a new scene, registers bodies/vehicles
and applies checkpoint state before the next solver step. Contact and transmission
transients rebuild; exact trajectory continuity/deterministic replay is not promised.
`restore` validates before staging and cancels commands. Missing/invalid bindings or
solver construction reject the candidate scene, preserving the active scene.

Subtree duplication shares immutable definitions and remaps internal wheel IDs to
fresh saved identities; references outside the duplicated subtree are rejected.
Play clones share immutable definitions and own independent bodies/drivetrains.
Standalone vehicle-library asset catalog authoring, damage, AI driving and multiplayer remain later work.
Compound chassis and physical surfaces are delivered in [Stage 2](physical-surfaces.md).

## Validation

See [recorded checks](vehicles-validation.md). Build `vehicle_tests` and run CTest
`vehicle`; `vehicle_wasm_tests` runs the same contracts on Node/Wasm. Browser smoke:

```powershell
# Set NODE_PATH to Playwright/pngjs and BROWSER_EXECUTABLE to Chrome/Edge.
$env:DEMO_URL = 'http://localhost:8098'
node scripts/vehicle-smoke.cjs
$env:DPR = '2'
$env:EVIDENCE_DIR = 'build/evidence/vehicle-dpr2'
node scripts/vehicle-smoke.cjs
```

The smoke forces both renderers and records actual backend/telemetry/screenshots.
Tests establish scoped contract/visual correctness, not handling approval, physical
mobile/controller acceptance or sustained performance.

Current archive writer is v26; v24 introduced the vehicle fields.


[Stage 3](driving-controls.md) adds shared gamepad action input, inspectable device
status, isolated pointer/touch preview controls and a composed chase camera with
explicit presentation source, bounded look-ahead and final collision constraints.
Camera tuning is authored independently of vehicle simulation.
