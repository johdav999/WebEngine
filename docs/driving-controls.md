# Gamepad input and composed follow cameras

Stage 3 of the [driving support plan](driving-support-plan.md) adds generic browser
input and camera mechanisms. Games own controller mappings, steering policy,
vehicle commands, camera tuning and mode selection. See [validation](driving-controls-validation.md).

## Optional browser gamepad adapter

Load `web/input.js` then `web/gamepad.js`. Attach one `GamepadInput` to the existing
`BrowserInput`; `BrowserInput.flush` polls it once before submitting the frame's
normal action packets. There is no second input snapshot, RAF loop or solver path.
The existing `Application::submitInput` lifecycle gate remains authoritative.

```js
const input = EngineInput.instance;
input.gamepad = new EngineInput.GamepadInput(input);
const unbind = input.gamepad.bind({
  context: 'Gameplay', kind: 'axis', index: 0,
  action: EngineInput.Actions.Move, value: [1, 0],
  deadZone: .15, invert: false, exponent: 1,
});
input.gamepad.select(null); // Automatic standard controller; explicit index also supported.
```

Bindings map one scalar axis/button to a two-dimensional action contribution.
`value` components are finite -1..1. Axes clamp to -1..1; button values clamp to
0..1. Missing/non-finite samples become zero. Axial dead zones remap magnitude as
`max(0,(abs(raw)-deadZone)/(1-deadZone))^exponent`; sign and inversion then apply.
Defaults: axis dead zone .15, button/trigger dead zone .05, exponent 1, inversion off.
Dead zones are finite 0..less-than-1; exponent is finite .25..4. This is axial filtering,
not radial-stick filtering, temporal smoothing or a speed-dependent steering model.
Those policies stay with applications when needed.

Each binding reserves its own source ID through `BrowserInput.nextSource`; it
shares the existing source allocator with keys and virtual controls. Releasing a
pad, removing its binding or selecting another pad zeros only that adapter's
contributions. A held keyboard source survives gamepad disconnection. Existing
context changes intentionally cancel all sources at the context handoff.

Connection/disconnection events also cancel a matching active device before the next
poll, including index reuse. Owned event subscriptions release on disposal.
Automatic selection keeps its connected standard controller, otherwise chooses the
lowest connected standard index. Explicit `select(index)` also permits custom
mapping devices; applications must supply appropriate raw indices. Limits are 16
visible controller slots, 128 bindings per adapter and indices 0..63 for axes/buttons.
Unsupported/inaccessible browser APIs produce an empty device list or bounded error
status. Device exposure and activation remain browser-controlled; a controller may
need user interaction before the browser exposes it. No connection is inferred.
`getGamepads` and `allowed` callbacks are injectable synchronous host dependencies.

Input requires a ready, visible host, allowed context, keyboard ownership outside
GUI/text fields and canvas or registered virtual-control focus. Blur, hidden page,
resize, focus/context changes, selection, enable changes, cancellation and controller
replacement require all currently active bound controls to return to neutral before
re-arming. A held trigger cannot leak into a newly focused preview. Host pause,
suspension, scene handoff and GPU readiness gates clear/re-arm the example's input.
`setEnabled(bool)`, `cancel()`, `snapshot()` and idempotent `dispose()` are explicit.
Snapshot contains bounded device/index/mapping/status data, never scene state.
Disposing BrowserInput disposes its attached gamepad adapter and releases sources.

In the application example, standard left stick X steers, RT accelerates, LT brakes,
A holds handbrake and B requests reverse. Steering inversion and controller selection
are host controls. Existing W/S/A/D/Space/Shift and virtual buttons feed the same
vehicle commands. **Gamepad and preview controls** is available in the vehicle panel;
hold Forward/Reverse/Left/Right/Brake during isolated Play or live simulation.
Pointer/touch cancellation releases the respective source. Editor idle mode cannot
drive the authoring scene. Gamepad status/selection are inspectable in the editor,
while authored component/material controls remain disabled during Play.

## Presentation targets and velocity look-ahead

`CameraTarget::poseSource` defaults to `CameraPoseSource::Simulation` for compatibility.
Opt into `Presentation` to sample the same current interpolation matrix used by
scene collection. `PhysicsWorld::presentationMatrix(node)` returns an owned matrix
without changing authoritative transforms, independent of visibility, collection,
LOD and renderer. It rejects foreign/removed nodes. Dynamic bodies, characters,
descendants and bound wheel visuals use their existing sampled transforms; static
or non-physical nodes use their world matrix. No physics world is created for a camera.

```cpp
CameraTarget target(subject.id());
target.poseSource = CameraPoseSource::Presentation;
tracking.target = target;
tracking.lookAheadSeconds = .3f;
tracking.maxLookAhead = 4;
```

`CameraTarget::velocity(scene)` supplies authoritative rigid-body point velocity at
the target offset, or zero without a live physical body. It does not finite-difference
render positions. `TrackingCameraBehaviour` adds `lookAheadSeconds` (default 0) and
`maxLookAhead` (default 5 m), both finite nonnegative, to aim ahead by velocity*time,
clamped to the maximum distance. Negative velocity naturally looks back in reverse.
This does not turn the camera into a second simulation writer. Applications may
supply their own behaviours for other velocity sources or driving policies.

## Authored follow composition

`CameraComponent::follow()` holds validated `CameraFollowSettings` data; `setFollow`
replaces it atomically. The camera still owns projection/data. Application-owned
`CameraFollowRig` consumes that data and composes the existing Follow, Tracking,
Smoothing and Collision behaviours. Creating or loading settings does not start a
camera tick or select a view.

```cpp
#include "engine/camera_follow.hpp"
auto settings = camera.follow();
settings.enabled = true;
settings.target = subject.savedId();
settings.presentationPose = true;
settings.lookAheadSeconds = .3f;
camera.setFollow(settings);
CameraFollowRig rig(scene, camera.id()); // Scene outlives rig; camera uses a guarded handle.
// Once after simulation/interpolation, before rendering:
rig.update(cameraDeltaSeconds, viewportAspect);
// After teleport, reset or an explicit camera cut:
rig.reset();
```

Defaults: disabled/unbound; presentation pose and target-local boom on; focus offset
(0,1,0)m, boom(0,3,-8)m; look-ahead .3s capped 4 m; position/rotation half-lives
.12/.1s; collision on, radius .3m, clearance .05m and layers 0xffff. These +Y-up/+Z
example-facing values are configurable data, not physics orientation rules.
`PrefabCameraSettings::follow` supplies the same settings through the existing factory.

Bounds: target is empty or a valid saved node ID; offsets are finite within +/-1e6m;
look-ahead time and half-lives 0..10 s, maximum look-ahead 0..10000 m, radius(0,100]m,
clearance 0..100 m. Collision layers are an integral16-bit mask. Invalid settings/time/
aspect preserve valid camera state. Direct self-follow is rejected. Targets resolve
as scene-local actors/components; absent/disabled/removed targets return false and
hold the camera, clearing follow history. Unbound saved targets are permitted for
application-owned late spawning. Rebinding a different generation or pose source
snaps on its first valid evaluation. Old runtime handles never enter archives.

The rig follows the sampled target, aims at velocity look-ahead, smooths exponentially,
then applies the existing final collision sweep, excluding the subject actor and
sensors. Smoothing cannot override the final constraint. Call `reset()` on teleports/
mode handoff when an immediate cut is required; the next valid update snaps while
still respecting collision. A zero-delta initialized update holds smoothing but
still runs constraints. The application supplies actual per-view aspect and chooses
its camera clock; subjects' simulation never reads the resulting camera.

`OrbitController::adoptCamera(pivotDistance)` captures the current local eye/yaw/
pitch without moving the camera when handing control back to orbit navigation.
Orbit has no roll degree of freedom. The editor uses this at chase-to-navigation
handoff so its cached orbit state does not jump back to the earlier view.

One writer must own each camera. In the example, enabled composition replaces orbit
updates in live simulation and Play. Editor idle viewport navigation remains its
writer; authored follow settings take effect in isolated Play. Main/Play rigs are
separate and released before scene replacement, Play teardown and shutdown. The
scene must outlive user-owned rigs. Existing boom-shortening limits apply: collision
queries use authoritative geometry, not interpolated obstacles, and do not provide
general camera penetration recovery or guarantee target framing behind obstructions.

## Editor and persistence

Select the active CameraComponent in the outline and edit **Camera follow**:
enable/target, presentation source, local/world boom, focus/boom offsets, look-ahead,
half-lives, collision/radius/clearance/layers. These typed settings use existing
history, invalid-edit rollback, autosave, duplication and Play cloning. Follow fields
are editable on a viewport-owned camera; its transform/lens/structural protections
remain in place. Target text uses existing actor/component saved IDs; `-` clears.
Editor target assignment requires a currently resolvable actor/component.

Scene/application/effect envelope writer **v26** adds authored follow settings to
camera records. Supported older versions default follow off. Internal target IDs
remap on subtree duplication; external saved references stay external in the same
scene. Full-scene saves preserve saved IDs. Runtime controllers, smoothing/transition
history, devices, permission/activation state, held buttons, live bindings and input
samples remain transient. No application gamepad preference store is added; games
may version user settings separately. Restore input bindings and application-owned
rigs deliberately. Deploy matched browser/main/worker artifacts after schema changes.

The shared handling course `?scene=vehicle` now enables the composed chase camera.
Use `?editor=1&scene=vehicle` to tune it and test isolated Play. Dedicated smoke
controls behind `vehicleTest=1` are application test adapters, not portable APIs.
See [validation and reproduction](driving-controls-validation.md).
