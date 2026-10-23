# Planetary collision and movement: stage 2

`engine_planet` / `WebEngine::Planet` now includes optional planetary gravity,
source collision preparation and a spherical, arbitrary-up explorer/lander motor.
Include `engine/planet_physics.hpp`. These compose existing `PhysicsWorld` convex
queries and ordinary static/dynamic actor components. No Jolt, platform or GPU
types enter the public interface. The existing +Y character motor is unchanged.

The shared [planet demo](planet-terrain.md) flies from orbit, lands through
registered collision, walks/jumps on the surface and takes off again on browser
WebGPU/WebGL2 and Windows Dawn/direct D3D12. The former 3 m height clamp is removed.

## Collision source and publication

`preparePlanetCollision(terrain,focus,settings)` prepares an immutable
`PlanetCollisionPatch` completely before returning it. Its origin is the double
source surface point beneath the focus. Two tangent axes define a square chart;
each grid sample projects back to the sphere and evaluates the same analytic
source used by the renderer. Adjacent cell triangles share vertices. There are
no render LODs, morphs, skirts, textures or camera decisions in this shape.

`PlanetCollisionSettings` defaults to a 256 m half extent and 128 cells per side:
a 512 m square, approximately 4 m sample spacing, 16,641 vertices and 32,768
triangles. Half extent validates in 32..1024 m; cells are powers of two in 16..128.
This is a piecewise-planar approximation of the analytic source, not an exact
continuous collision surface. Planet curvature introduces small chart distortion.

The resource contains `origin`, orthonormal `east/north/up`, `halfExtent`, `cells`
and an immutable ordinary `CollisionShape::triangleMesh`. `contains(position,margin)`
checks tangent-plane coverage independently of altitude. Source data is prepared
synchronously and bounded by the selected grid; there is no background loading or
hard frame-time guarantee.

The caller publishes this shape on an ordinary scene-root static body at local
position zero, with unit scale, and synchronizes it before queries. All physics
positions in that world region are `global.relativeTo(patch.origin)`. Only
translation changes; global axes remain unchanged. Publish a replacement at an
idle owner boundary and preserve existing bodies' global poses and velocities.
The motor retains double global position/velocity across this change.

The demo prepares collision below 1,800 m body altitude. It retains the chart
until the explorer travels more than approximately 96 m along either tangent
axis, then prepares a complete replacement before the next fixed traversal.
`ApplicationFrameCallbacks::simulate` owns publication; scene reconciliation
registers the new shape before the explorer's fixed component runs. Collision
component identity remains stable. Render detail toggles leave this resource
unchanged. Near-surface flight is limited to 300 m/s; missing collision coverage
holds accepted explorer state rather than permitting an uncollided descent.

## Radial gravity

```cpp
auto acceleration = engine::planetGravity(globalPosition, planetRadius, 9.81);
// World gravity is zero in this example. A dynamic body has gravityFactor=0.
body.addForce((acceleration * body.settings().mass).relativeTo({}));
```

The result points toward the planet centre and uses inverse-square falloff
outside the configured radius: `g = surfaceAcceleration * (radius/distance)^2`.
Acceleration is bounded to surface strength inside the radius. The function owns
no scene or clock. A zero/invalid position, radius outside 1..1e10 m, acceleration
outside 0..1000 m/s², or non-finite values reject. Valid global position length is
1..1e14 m. Other applications decide which bodies use this field; the library
does not replace scene gravity or register bodies automatically.

The example applies this force to a 2 kg, radius-0.35 m orange cargo sphere on
every normal fixed tick. It uses a real dynamic rigid body with continuous
collision and independent presentation. The explorer uses the same gravity field
in the query-based motor. Powered flight represents velocity-controlled thrusters
that compensate gravity; disabling thrusters exposes gravitational acceleration.

## Explorer motor

`PlanetSurfaceMotor` owns its sphere query shape and `PlanetMotionState`; it owns
no actor, world, scene, callbacks or clock. Call `reset(globalPosition,velocity)`
before stepping. Reset validates before replacing state and clears runtime
counters/support. Applications explicitly project state onto their existing actors.

```cpp
engine::PlanetSurfaceMotor motor;
motor.reset(startPosition);
engine::PlanetMotionCommand command;
command.velocity = desiredPlanetFrameVelocity;
command.poweredFlight = false;
command.jump = requestedJump; // Consume the request once, on a fixed step.
motor.step(terrain, command, 1.0/60, &scene.physics(), collision.get());
auto state = motor.state();
```

World and patch must either both be absent or describe the same currently
registered source collision. Passing an unregistered/mismatched shape is a caller
contract violation. Below the 1,500 m activation shell, absent coverage of either
the current position or requested endpoint sets `waitingForCollision` while
preserving accepted position/velocity. No analytic-height fallback replaces
collision. Above the shell, the motor can integrate without a world/patch.

Normal operation uses one existing 60 Hz scene fixed traversal. Commands are
device-independent global m/s. Powered flight controls all three velocity axes;
unpowered movement uses commanded tangent velocity and integrates radial falling
velocity. Walkable support cancels downhill drift through static support/friction.
This is a kinematic explorer motor, not a Newtonian spacecraft/orbital simulator.

Movement uses bounded initial overlap recovery, sphere sweeps, contact-plane
sliding and a short downward support sweep. Walkable slope classification uses
radial up and the contact normal. Supported jump requests apply a radial impulse;
upward motion bypasses floor snap. Terrain seams and local-origin changes require
no changes to gravity direction, collision representation or motor orientation.

Defaults and bounds:

| Setting | Default | Valid range |
| --- | --- | --- |
| Sphere radius | 0.65 m | 0.1..10 m |
| Skin | 0.02 m | 0.001..radius/4 |
| Ground snap | 0.22 m | 0..1 m |
| Jump speed | 5 m/s | 0..50 m/s |
| Maximum slope | 0.28 pi radians, 50.4 degrees | 0.1..pi/2, upper exclusive |
| Surface gravity | 9.81 m/s² | 0..1000 m/s² |
| Slide iterations | 6 | 1..8 |
| Query layers | all 16 | 16-bit mask |

Fixed delta is finite `(0,0.05]` seconds. Command/reset velocity is finite with
magnitude at most 20,000,000 m/s. Position validation uses finite length 1..1e14 m.
Invalid commands/deltas reject before replacing valid motor state. Counters include
steps, sweeps, jumps and landings. `groundBody` is an ordinary scene-scoped component
handle; resolve it before use. Support/landing/jump flags and normals are results,
independent of render interpolation. `contactLimited` reports exhausted recovery
or sliding work; it is not a promise of arbitrary obstacle traversal.

The sphere motor does not add the upright motor's stair stepping, a humanoid
capsule, rigid-body momentum exchange, character-to-character collision policy,
ship gear/damage, rolling rotation or full physical walking. Queries respect
existing solids/layers and exclude sensors. Dynamic cargo uses the solver instead.
Collision residency follows one explorer; remote dynamic objects outside the
loaded chart are not supplied planet-wide collision coverage.

## Demo controls and lifetimes

- **Land on surface** uses a 28-second velocity-controlled descent to 4 m, then
  switches off thrusters. Gravity and swept collision complete touchdown.
- **Take off to space** uses continuous powered flight to the existing orbit.
- On the ground, WASD walks at 6 m/s, or 10 m/s with Shift; Space jumps.
- Q/E moves radially during flight; E takes off from supported ground.
- G / **Toggle thrusters** switches between powered hover and gravity-driven fall.
- **20 m hover bookmark** explicitly repositions above the current source point;
  it is a diagnostic bookmark, separate from seamless-descent acceptance.
- **Terrain detail** changes visual refinement without changing collision.
- Right-drag looks relative to planetary up. Escape releases input and stops tours;
  it does not disable gravity. Focus/visibility loss cancel held commands.

The demo owns a bounded render actor pool, one static collision actor, one dynamic
cargo actor and a separate visual projection actor. Cargo and explorer use a
stable solver origin; render actors use an interpolated camera-relative origin.
The camera stays at local zero, 0.95 m above the explorer centre. HUD body altitude
therefore differs from eye altitude. Fixed-step poses interpolate for presentation
without altering simulation. Cargo rebasing retains its global pose and velocity.

`PlanetSimulation` is a demo-owned component borrowing its enclosing demo for the
Application-owned scene's lifetime. Hosts release scene/renderer references during
shutdown. Button requests apply at the idle pre-simulation boundary, after input
cancellation, so latched jumps are not lost. No asynchronous callbacks are retained.
The example has no save/restore codec for this runtime component.
Native fonts and GUI finish preparing before Application start begins asynchronous
GPU acquisition, so the first host step can immediately service device callbacks.

## Build and validation

Use the same [stage 1 build commands](planet-terrain.md#build-and-run), adding
`planet_physics_tests` to both native and Wasm builds. Run native CTest
`planet|planet_physics`, Wasm `planet_tests.js` / `planet_physics_tests.js`,
`scripts/planet-smoke.cjs` on both browser backends, and the Windows `--smoke`
runner on Dawn/direct D3D12 followed by `tools/verify_planet.py`.

See [stage 2 evidence](planet-stage2-validation.md). Planetary atmosphere/oceans,
authored terrain/edit persistence and physical mobile acceptance remain later work.
