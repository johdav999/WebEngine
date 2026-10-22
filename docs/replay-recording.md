# Replay and ghost recording

Open **`?scene=replay`** for a procedural driving lab, or expand the floating replay
panel in the Formula demo. Collapse it to free the driving view without stopping
capture or a racing ghost. The Windows vehicle app has the same shared capture/playback
policy, retained GUI controls, and native `.weghost` Open/Save dialogs.

This is a **recorded pose/event review** facility. It does not reproduce solver
execution, regenerate contacts, rerun recorded commands, or resume arbitrary
game state. Use its poses, commands, telemetry and markers to inspect what happened;
deterministic physics/input replay remains a larger separate extension.

## Driving workflow

1. Choose **Record**, drive with the existing controls, then **Finish recording**.
   The lab enables Driving controls by default; click the canvas to focus. Capture advances only
   with eligible simulation time: resume motion if the simulation is paused.
2. **Mark** adds a named bug observation in the browser; native **Mark bug** adds a
   driver marker. Commands, gear changes and resets are also diagnostic events.
3. **Review session / Review** freezes live physics, hides the live car's meshes,
   and follows a render-only recorded proxy. Play/pause, scrub, change speed, and
   jump to an event. The browser shows all events in a scrollable list; native
   **Next event** jumps through them and the HUD shows the current event.
4. **Close playback / Close replay** restores live visibility, camera settings and
   prior pause policy. Review never teleports the live chassis or changes velocity.
5. **Race ghost** explicitly teleports the live car to the recording's first pose
   at rest, cancels held commands and resumes driving. The non-colliding ghost
   advances at **1× simulation time**; review slow motion does not carry into a race.
   Pause freezes the ghost; its final pose holds at the end. Race rules/lap scoring
   stay application-owned. A diagnostic recording can contain resets/cuts, so it is
   not automatically a valid lap or a competitive best-time submission.

The browser supports `.weghost` download/import and an explicit Clear session.
Native Open/Save dialogs use the same format; public example methods also support
an explicit profile save `last-session.weghost`. Files are separate from checkpoints
and never automatically restore or replay. Invalid/corrupt/incompatible loads keep
the valid recording. Pending browser loads check scope/revision and recording/review
state before publication. Neither UI uploads recordings.
Browser playback/edit actions stay disabled until asynchronous file reading and
validation finish, preventing a late import from replacing a newly started race.

## Portable values, recorder and player

`engine/replay.hpp` belongs to `engine_core`. A `ReplayData` schema names pose and
numeric channels and supplies an application context string. Frames contain an
absolute timestamp, `JointPose` values (translation/positive scale/quaternion),
numeric telemetry and an optional cut flag. Events contain timestamp/name/payload.
Immutable `ReplayRecording::create` validates the complete candidate.

```cpp
ReplayData schema;
schema.name = "Test run";
schema.context = "my-track-v1/car-v3";
schema.poses = {"chassis"};
schema.values = {"speed"};
ReplayRecorder recorder(schema);
recorder.capture({0, {replayPose(car.transform.matrix())}, {0}});
// Caller samples authoritative state after its simulation owner has updated it.
recorder.capture({.1, {replayPose(car.transform.matrix())}, {speed}});
recorder.event({.1, "marker", "Inspect braking"});
auto recording = recorder.finish();
ReplayPlayer player(recording);
player.play();
player.advance(eligibleSeconds); // Explicit 0..0.25s; no hidden clock/subscription.
auto sample = recording->sample(player.time());
auto bytes = ReplayArchive::save(*recording); // WEGHOST 1 text, .weghost.
```

The first frame is exactly zero and later frame times strictly increase. Sampling
linearly blends translation/scale/numeric channels and uses the existing normalized
shortest-arc quaternion blend. Sampling the final endpoint returns the recorded
endpoint. A cut holds the preceding sample until the cut frame, preventing a reset
from drawing a motion sweep across the course. Rotation changes exceeding180°
between samples cannot be inferred, particularly fast wheel spin. Add samples when
the distinction matters; no higher-frequency physical motion is reconstructed.

`eventsBetween(begin,end)` exposes ordered events in `(begin,end]` without dispatch,
consumption or callbacks. Its span borrows the immutable recording; retain that
recording while using the span. Inspect events at zero directly from `data().events`.
Seeking pauses; it does not execute events or commands. Player speed is .125..4,
looping defaults false and completion pauses. Play at completion restarts. The
application chooses eligible simulation or presentation deltas and suspension.

Budgets: **1..32 pose channels**, **0..16 numeric channels**, **18001 frames**,
**180000 total pose samples**, **2048 events**, **600 seconds**, **64MiB files**.
Names/pose/value identifiers are bounded printable text (256/128 bytes), context
1024 bytes and event payload1024 bytes. Translation is finite within±1e6; positive
scales .0001..1000; rotations must be normalized; telemetry finite within±1e9.
Matrices must be positive TRS without shear/reflection. Native and Wasm use quoted,
locale-independent versioned text with strict counts/values/trailing-data checks.
File size is checked before parsing; no scene/resource/solver types are embedded.

The recorder validates each sample before mutation. Frame/sample or event budget
exhaustion returns false and preserves the admitted buffer; invalid input throws.
`finish()` copies into an immutable validated recording and needs at least two
frames. This is bounded in-memory capture, not disk streaming, a ring buffer,
arbitrary-duration recording or compressed video. Callers choose retention policy.

## Render-only proxies and ownership

`engine/replay_ghost.hpp` exposes `ReplayVisual` and `ReplayGhost`. Supply1..31
ordinary static mesh resources/texture/material overrides. The scene owns a new
actor with mesh components only, tinted translucent cyan, without shadow casting,
collision, rigid bodies, vehicle constraints or callbacks. `apply(scene,poses)`
expects a root pose relative to the scene plus one local pose per visual. It
validates all transforms before publishing and checks scene scope/generation handles.
It retains no Scene/Node pointers; `show` and `remove` resolve handles each call.
Remove explicitly on application handoff; scene teardown also releases it. The
proxy subtree uses the existing transient-archive mechanism and is omitted by scene
and application snapshots, including its otherwise unreferenced resources.

Generic replay has no automatic scene binding, custom component codec, event
execution or skeletal/morph capture. Applications can record their chosen channels
and bind them to their own presentation objects. Skinned characters need additional
application policy; the provided ghost is for static vehicle mesh parts.

`examples/driving_replay.hpp` owns shared driving policy, not new engine rules.
It binds the source with scene/component handles, captures afterSimulation at
**up to30Hz** using actual eligible timestamps, and records chassis TRS plus static
mesh poses relative to it. Wheel poses come from current `VehicleState.localPose`
and authored visual offsets; render interpolation, visibility, culling and LOD do
not supply recording state. Slow frames produce fewer samples, never extra solver
steps or fabricated catch-up poses. Duration/sample limits finish the buffer;
event exhaustion exposes `eventsDropped()` (also shown in browser recording status)
while retaining admitted events.

Application context includes an explicit course tag and a stable vehicle-geometry/
authored-offset fingerprint. Source scene/map/resources are not embedded. Compatible
browser/native vehicle-course files are interchangeable. This is a compatibility
check, not cryptographic authenticity or proof the course has not changed. The
application must version its course tag when world content changes. Changed source
geometry or authored layout requires clearing/rebinding the session. Animation/deformation, scenery,
NPCs, force accumulators and other game state are not captured by this example.

Browser adapters live in `src/replay_browser.inc` and `web/replay.js`; native policy
lives in `examples/windows_vehicle_app.hpp`. Both use the same portable session,
resources and clock rules. Review cancels input, guards conflicting previews and
preserves source state; closing restores camera/mesh presentation through handles.
Browser source controls become inert while reviewing. Recording/visible playback
must close before entering the editor or cinematic previews. Terminal shutdown
removes the proxy and releases recording owners before source scene destruction.
Close review before saving the source scene: its temporary camera/visibility
settings are presentation state. The browser scene-save export enforces this;
native vehicle checkpoints store the unchanged chassis separately.
No DOM, SDL, Dawn or Jolt implementation types enter new portable interfaces.

## Build and verification

Follow README and repository Emscripten6.0.11. Build `replay_tests` (CTest `replay`)
and explicit `replay_wasm_tests` (Node). Build/stage `webengine web_assets` and run
`scripts/replay-smoke.cjs` sequentially on WebGPU/forcedWebGL2 with
DEVICE_SCALE_FACTOR1 and2. Native MSVC/Dawn builds include `windows_replay_smoke`;
run `scripts/replay-windows-smoke.ps1`, optionally with `-BrowserRecording` pointing
to a downloaded compatible file. The script records separate native evidence and
verifies actual framebuffers. Regress relevant vehicle/input/camera flows.
See [recorded validation](replay-recording-validation.md). Desktop/DPR evidence does
not establish physical mobile, physical controllers, Safari or sustained performance.
