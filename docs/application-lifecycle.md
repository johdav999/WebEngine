# Application and scene lifecycle

`engine::Application` in `engine_core` owns loaded scenes, the `InputSystem`,
the variable simulation clock, the fixed-step scheduler, and the input and
presentation clocks. The browser demo uses this same API for normal frames,
pause/resume, visibility changes, archive transitions and shutdown.

```cpp
#include "engine/application.hpp"

engine::Application app;
const auto playground = app.loadScene(std::make_unique<engine::Scene>("Playground"));
// Configure a renderer binding here, if the host has one.
app.start(playground);

engine::ApplicationFrameCallbacks callbacks;
callbacks.input = [](engine::Scene&, const engine::InputSystem& input,
                     const engine::ApplicationFrame& frame) {
    // Camera and menu controls use input and frame.input.delta.
};
callbacks.simulate = [](engine::Scene&, const engine::ApplicationFrame& frame) {
    // Game-specific variable simulation uses frame.simulation.delta.
    // Application calls Scene::update and fixedUpdate itself.
};
callbacks.present = [](engine::Scene&, const engine::ApplicationFrame& frame) {
    // Render using frame.presentation and frame.interpolationAlpha.
};
app.advance(0.0, callbacks);    // Host monotonic timestamp, in seconds; first delta is zero.
app.advance(0.016, callbacks);
app.pause();
app.advance(0.032, callbacks);  // Input and rendering continue; simulation is frozen.
app.resume();
app.advance(10.0, callbacks);   // First resumed delta is zero, without catch-up.

const auto level = app.loadScene([] { return std::make_unique<engine::Scene>("Level"); });
app.requestTransition(level);  // Retain playground for a later return.
app.advance(10.016, callbacks);
app.requestTransition(playground, true); // Unload level after switching.
app.commitTransition();       // Explicit idle boundary, when needed outside a frame.
app.shutdown();
```

The application state moves from `Created` to `Running`, alternates between
`Running` and `Paused`, and ends at `Stopped`. Start is permitted once; pause,
resume and shutdown are idempotent. Shutdown is terminal. Destruction also
cleans up owned scenes, but call `shutdown()` while the renderer host is still
alive to release its scene references through the binding callback.

## Clock ownership and frame order

| Clock | Running | Paused | Host suspended | Owner and purpose |
| --- | --- | --- | --- | --- |
| Input | Advances | Advances | Frozen | Application starts one stable `InputSystem` snapshot per frame; camera/menu controls consume it |
| Variable simulation | Advances | Frozen | Frozen | Application calls the simulation callback and `Scene::update` once per running frame |
| Fixed simulation | Advances | Frozen | Frozen | Application owns `FixedStepClock`, calls `Scene::fixedUpdate`, and sets physics interpolation |
| Presentation | Advances | Advances | Frozen | Application invokes the host's presentation callback; the renderer owns GPU submission |

Frame order is pending controls, asset publication/preparation/transition, input snapshot and callback,
variable simulation callback and scene update, fixed simulation steps,
`afterSimulation`, then presentation. Inactive loaded scenes receive none of
these callbacks. Paused scenes use `Scene::synchronize()` to reconcile actor/
component lifecycle and skeletal resources without running variable or fixed
ticks. Direct `Scene::update`/`fixedUpdate` and `FixedStepClock` remain available
for standalone users; do not also drive them for an application-managed frame.

Settings default to a 50 ms variable/input/presentation delta cap, a 1/60 s fixed
step and at most eight fixed steps per frame. The fixed scheduler separately
caps accumulated frame time at 250 ms and drops excess catch-up. `clocks()`
exposes cumulative elapsed times/frame counts, the current frame deltas,
fixed seconds, fixed step count, dropped fixed simulation seconds and
interpolation alpha. Raw host interval is in seconds. Zero is a valid first
timestamp; negative, nonfinite and backwards timestamps are rejected.

`setSuspended(true)` freezes every clock and rejects input while a host is hidden.
`setSuspended(false)` preserves the explicit pause state. Start, pause, resume,
suspension changes, transitions and `resetClocks()` cancel queued/held input,
drop fixed remainder and rebase the next timestamp without clearing elapsed
totals. This prevents hidden or inactive time from entering the next simulation.
The browser adapter also cancels gestures on visibility and archive changes.

## Scene ownership, hooks and transitions

`loadScene` takes unique ownership, or invokes a synchronous loader factory that
returns a unique scene. The application-owned `assets()` manager coordinates
asynchronous dependencies through `prepareScene`; its batch and callback complete
before presentation binding/activation. A pending transition keeps the outgoing
scene active until ready. Standalone staging and transfer at an idle boundary remain
supported. See [asset contracts](assets.md). `SceneId` is scoped
to an application and never reused. Lookup rejects foreign, missing and unloaded
IDs. Raw scene/camera/actor pointers are borrowed and expire on unload.

Subclass `Scene` to override `onLoad`, `onActivate`, `onPause`, `onResume`,
`onDeactivate` and `onUnload`. `onLoad` may throw; every other hook is `noexcept`.
The exposed scene state is `Detached`, `Loaded` or `Active`. Loading calls
`onLoad`; activation does not spawn/tick actors until the next application
frame. A load failure calls `onUnload` on the candidate and destroys it while
preserving the active scene. A failed loader factory likewise leaves the active
scene untouched. A transition while paused activates the target and calls its
pause hook.

`requestTransition` validates the target immediately. The last request wins and
commits before the next frame's input snapshot; `commitTransition` can instead
commit at an explicit idle boundary. Requests from actor ticks never destroy
the scene being traversed. The default retains the outgoing scene; passing
`unloadPrevious=true` unloads it after rebinding and activating the target.
Re-requesting the active scene is a no-op. Explicit `unloadScene` only accepts
inactive scenes that are not pending targets. Scene destruction follows the
existing actor/component despawn, physics and resource ownership contracts.

Configure `setSceneBinding` before start to connect presentation ownership.
The callback receives the target scene before activation, and `nullptr` before
shutdown destroys scenes. It must validate before changing its binding and
retain the old binding if it throws. Failed target binding preserves the active
scene and its hooks. In the browser demo this binds validated camera views;
shutdown destroys the renderer before destroying scenes.

Load/unload, immediate commits and recursive frames require an idle application.
Pause/resume/shutdown requests made during callbacks are deferred to the frame
boundary; a shutdown request suppresses subsequent presentation. A throwing
frame callback pauses the application, clears its input/timing baseline and
propagates the exception. The browser displays the error and retains the scene.

## Browser controls and verification

The demo exports `_engine_pause`, `_engine_suspend`, `_engine_shutdown` and
`_engine_lifecycle_snapshot`. The snapshot is available as
`window.__applicationSnapshot`; browser timestamps are converted from RAF
milliseconds to application seconds. The normal motion button controls
simulation pause. Shutdown disposes the input adapter, stops audio, releases
GUI/renderer references and scenes, disables controls, and ends the RAF loop.

Native coverage lives in `tests/application_tests.cpp`, registered as the
`application` CTest suite. `scripts/application-smoke.cjs` exercises actual
WebGPU pixels and runtime clock snapshots, pause camera input, suspension,
archive replacement/return, and idempotent shutdown. Its host timestamps are
controlled for reproducible images. Evidence is written to
`build/evidence/application/results.json` and PNGs. See
[application-lifecycle-validation.md](application-lifecycle-validation.md) for
the verified build and regression scope.
