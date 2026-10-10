# Animation control, markers and movement output

`AnimationComponent` remains the per-instance player owned by a skeletal mesh's
skeleton child. An optional `shared_ptr<const AnimationControllerDefinition>` adds
parameter-driven control to the same player. Definitions, skeletons and clips can
be shared; parameters, clocks, poses, triggers and output queues belong to each
player. Publish these assets only after authoring; do not mutate shared definitions.
The target skeleton must outlive a standalone player.

## Configure states and transitions

```cpp
#include "engine/animation_component.hpp"

auto definition=std::make_shared<engine::AnimationControllerDefinition>();
definition->parameters={
    {"speed",engine::AnimationParameterType::Number,0.0},
    {"jump",engine::AnimationParameterType::Trigger,false}
};
definition->states={
    {"Idle",idleClip,true,1,{}},
    {"Locomotion",walkClip,true,1,
        engine::AnimationBlend{runClip,0,0.2,1.0}},
    {"Jump",jumpClip,false,1,{}}
};
definition->transitions={
    {0,2,0.1,{},{{1,engine::AnimationComparison::Equal,true}}},
    {0,1,0.2,{},{{0,engine::AnimationComparison::Greater,0.0}}},
    {1,2,0.1,{},{{1,engine::AnimationComparison::Equal,true}}},
    {1,0,0.2,{},{{0,engine::AnimationComparison::LessEqual,0.0}}},
    {2,0,0.2,1.0,{}}
};
auto& player=mesh.animation();
player.setController(definition); // Validate before replacing the current controller.
player.setParameter("speed",0.6); // Typed finite numbers or booleans.
player.trigger("jump");          // Latched until used by a winning transition.
```

State, clip and parameter references use indices into their corresponding arrays.
Number conditions support equality/inequality and ordered comparisons. Boolean
conditions support equality/inequality. Trigger conditions require equality with
true. Every condition in a transition must match. Empty conditions make a
transition unconditional once its optional exit time is reached. Definitions reject
invalid references, types, values and duplicate state/parameter names.

Transitions evaluate at the **start of each variable tick** against the source
clock and current parameters. The first eligible transition in declaration order
wins; only one transition fires per tick, including a zero-duration tick. Exit
time is unwrapped playback time divided by source duration; a two-clip state uses
the longer duration, and a zero-duration source has normalized time one. A looping
exit time of 1 means after the first complete loop. An endpoint reached during a
tick becomes eligible at the next tick. Non-loop states hold their last pose and
can still leave through transitions. An explicit `pause()` freezes transitions as
well as playback; `resume()` enables both. Scene disable and application pause
freeze control. Fixed updates and `Scene::synchronize()` do not advance it.

Trigger consumption happens only after a winning transition enters its target.
Unreferenced triggers stay latched, including across other transitions. Use
`resetTrigger(name)` to cancel one. `parameter(name)` reads instance values.
The states, transitions and marker meanings are application policy; the engine
has no movement, input, combat or sound assignments.

An optional state blend maps its numeric parameter from `[minimum, maximum]` to
a clamped weight in `[0,1]`. Both clips use the state speed and clock with
independent duration wrapping. Parameter changes update the weight without
resetting the clock or discarding pending movement. Transitions can crossfade into
single clips or blended states. Fades use simulation seconds, freeze the outgoing
pose, and interpolate translation/scale and shortest-arc quaternion rotation.
Interrupting a fade starts at its current pose. Target state speed applies on
entry. These are two-clip blends; layers, additive poses and multi-point blend
trees are outside this interface.

Manual `play`, `blend`, `crossFade`, `setBlendWeight` and `stop` detach control.
`clearController()` detaches it while retaining the current playback. Seek and
pause/resume retain the definition. Replacing a skeleton cancels control,
extraction and queued output on the next animation update. A shared definition
does not retain a scene, actor or component.

## Author and consume clip markers

Before publishing a mutable skeleton, add sorted marker metadata to its clips:

```cpp
authoredSkeleton->clips[walkClip].markers={
    {0.25,"footstep","left"},
    {0.75,"footstep","right"}
};
authoredSkeleton->validate();
// Publish the finished skeleton/mesh as shared const assets normally.
```

Markers have a time in clip seconds, a nonempty name and an opaque string payload.
Times must be finite, nondecreasing and within `[0,duration]`; equal times retain
authoring order. Limits are 4,096 markers per clip, 1,024 bytes per name and 4,096
bytes per payload. FBX/GLB import currently supplies poses and empty marker lists;
applications author marker metadata explicitly rather than editing published rigs.

Updates emit markers in `(previousTime, nextTime]`, accounting for speed, all
crossed loops, and non-loop endpoint clamping. Time zero does not fire on play or
seek; it fires on subsequent loop boundaries. A duration marker and a time-zero
marker are distinct events at a boundary. Zero-duration clips emit no markers.
Paused, stopped and disabled players emit none. Play, blend, crossfade, explicit
weight changes, seek, stop and controller installation clear queued events; those
controls do not synthesize marker crossings. Controller transitions preserve
already queued events.

Both contributing blend clips emit events; a zero-weight source emits none. Events
carry the clip/marker index, owned name/payload strings, unwrapped clip clock and
source weight at crossing. Frozen outgoing fade poses emit none; the target emits
with its fade contribution. The consumer decides whether to filter low weights or
merge sounds/actions. Each tick's batch is chronological, with ties ordered by
source A then B and authored marker index. Batches append in tick order; clip
clocks reset on transitions, so queued times are not one global scene timeline.

`events()` returns a borrowed span valid until player mutation/destruction.
`consumeEvents()` drains owned copies that remain valid after destroying the
player. There are no event callbacks or subscription lifetimes. Gameplay can
resolve any scene handles it retains and safely destroy objects after consumption.
The queue holds at most 4,096 events. Excess crossings or loop clocks beyond the
exact integer range of doubles throw before clock/transition/output publication;
consume output regularly and use smaller updates to process an exceptional backlog.
Event creation can allocate; marker-free warm playback retains the existing
allocation-free root-motion path.

## Consume root motion and events together

```cpp
player.enableRootMotion(locomotionJointIndex);
// Set parameters from gameplay commands before updating the scene.
scene.update(deltaSeconds);
auto output=player.consumeOutput(); // Each event and movement interval consumed once.
for(const auto& event:output.events) {
    // Dispatch application effects from event.name/payload/weight.
}
// For identity mesh placement, unit actor scale and no physics/controller writer:
actor.setWorldTransform(actor.worldMatrix()*output.rootMotion.matrix());
```

`consumeOutput()` drains both the event queue and the accumulated translation/
quaternion delta. `consumeRootMotion()` and `rootMotionDelta()` remain available.
The delta is in the skeletal mesh's local coordinates before component placement;
convert through that placement when the actor uses another coordinate frame.
Extraction strips the locomotion joint from the skin pose and does not move an
actor or initialize physics. Gameplay owns collision, fixed-step buffering and
movement policy. For a character motor, buffer the output and convert it to that
motor's world-space command contract; avoid applying transforms over its solver.
Rendering, culling and LOD never decide whether output advances.

Controller transitions preserve unconsumed movement without adding a teleport.
Blend changes use the new weight for the next interval, preserving previous
intervals. Fades weight target movement by the average fade contribution; turning
loop extraction, bind anchoring and manual-control rebasing retain the contracts
in the [README](../README.md#skeletal-meshes-and-animation).

Scene and asset archives include marker metadata from version 3 onward. Scene
archives additionally retain shared controller definitions, instance parameters,
latched triggers, current state, completion state, crossfades and pending output.
Older version 1/2 data loads with empty markers and no controller. Applications
recreate marker response logic; executable handlers are never serialized.

## Validation

`animation_control_tests` exercises priority, trigger lifetime, exit/completion,
live blends, pause/disable/fixed-step boundaries, output ownership, resource
replacement, exact marker boundaries, ties, multiple loops, fades, queue overflow,
invalid-input preservation and scene/asset round trips. Existing `animation_tests`,
`root_motion_tests` and `scene_archive_tests` cover playback/render preparation,
turning movement, warm allocation checks and legacy archives.

```powershell
cmake --build build/native --target animation_control_tests animation_tests root_motion_tests scene_archive_tests
ctest --test-dir build/native -R 'animation|root_motion|scene_archive' --output-on-failure
# Configure an Emscripten build using README and the repository-local pinned SDK:
cmake --build build/web-animation-control --target animation_control_wasm_tests
node build/web-animation-control/animation_control_wasm_tests.js
```

The explicit Wasm runner uses the same portable control/event assertions in Node,
without publishing the playground. Browser rendering regression uses
`scripts/skeletal-animation-smoke.cjs`. Native/Wasm assertions and desktop browser
pixels do not establish physical mobile acceptance or sustained device performance.

Verified on 2026-10-10: the isolated `build/native-animation` harness passed all
four focused suites above; Emscripten 6.0.11 compiled the Release browser executable
and its control/event Wasm runner, whose assertions passed in Node. The skeletal
browser regression passed deformation, blending, interrupted playback, pause,
endpoint and import/recovery checks with no console/GPU errors. Its fixture
assertions now distinguish color submissions from the existing shadow pass and
frustum culling. Browser evidence is in
`build/evidence/animation-control-browser/skeletal-animation-results.json`.
The full repository native suite and physical mobile devices were not run for this
change. Other feature work was present in the shared checkout during validation.
