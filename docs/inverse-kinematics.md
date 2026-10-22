# Inverse kinematics

`engine/ik.hpp` adds optional, deterministic pose modifiers. `TwoBone` positions a
hand, foot or paw through a direct root–middle–tip chain, with a pole controlling
the bend plane and optional tip orientation. `LookAt` rotates one joint's authored
forward axis toward a target, with a maximum swing measured from the incoming
animated pose. Both preserve local translations and scales; unreachable limbs
clamp to their existing reach rather than stretching.

## Playback and ownership

Add up to 32 named constraints to `AnimationControllerDefinition::ik` before
publishing the definition. Constraints run in vector order after base animation,
crossfade, root-motion stripping and animation layers, before skin matrices.
Later constraints see earlier results. Each player copies the default targets
into its own storage. Shared rigs/controllers remain immutable.

```cpp
auto controller = std::make_shared<AnimationControllerDefinition>();
controller->states.push_back({"Locomotion", idleClip, true, 1, {}});
IkConstraint hand;
hand.name = "Hand";
hand.root = shoulder;
hand.middle = elbow;
hand.tip = wrist;
hand.orientTip = true;
hand.target.position = {0.7f, 1.3f, 0.4f}; // Skeletal model coordinates.
hand.target.pole = {1, 1, 0};
controller->ik.push_back(hand);

IkConstraint look;
look.name = "Look";
look.mode = IkMode::LookAt;
look.root = head;
look.forward = {0, 0, 1}; // Head-local forward axis.
look.maxAngle = pi / 3;
look.target.position = {1, 1.7f, 3};
controller->ik.push_back(look);
mesh.animation().setController(controller);
```

`setIkTarget(name, target)` and `setIkTargets(span)` validate and evaluate immediately,
including while paused. They preserve clocks, markers and accumulated root motion.
The effective weight is constraint weight × target weight; both default to 1 and
must lie in `[0,1]`. `enabled=false` or zero weight bypasses that constraint.
Blend weights with gameplay's chosen time policy for gradual contact release.
Setting weight never advances animation. Manual playback/detachment removes IK;
manual crossfades capture the outgoing solved pose. Controller state transitions
retain targets and solve once after their base crossfade. Skeleton replacement
cancels the controller.

`ikTargets()` and `poseBeforeIk()` return borrowed spans valid until the next
relevant control/pose operation. The latter contains the composed, root-stripped
pose before IK and is empty without controller IK. `ikResult()` reports per-constraint
residual error (model units for limbs, radians for look-at) and reach/angle limiting;
it is diagnostic state, not a gameplay event. Targets are owned values; no scene
pointer, callback, worker, subscription or second animation clock is retained.
Call on the scene owner, resolving application-held actors/components through handles.
Explicit target updates can change a disabled character's pose; ordinary scene
lifecycle ticking still obeys pause/disable.

IK cannot rotate an extracted root-motion joint or any of its ancestors. This is
validated in either installation order, even for disabled constraints. Only the
base animation produces markers and movement. IK never moves an actor, changes a
physics body, or feeds its result back into simulation.

## Grounding and world targets

`ikTargetFromWorld(modelToWorld, worldTarget)` converts position, pole and orientation.
Pass the skeletal mesh's full placement, including actor/component ancestors.
Use the simulation placement for gameplay probes; interpolated presentation poses
must not drive authoritative decisions.

```cpp
auto& player = mesh.animation();
auto foot = ikJointTarget(
    *mesh.skeleton().skeleton(), player.poseBeforeIk(), footJoint);
foot.pole = player.ikTargets()[footConstraintIndex].pole;
PhysicsQueryFilter filter;
filter.includeSensors = false;
filter.excludedActors.push_back(character.id());
IkGroundProbe probe;
probe.above = 0.5f;
probe.below = 0.8f;
probe.soleOffset = 0.03f;
if (auto contact = groundIkTarget(scene.physics(), modelToWorld, foot, probe, filter))
    player.setIkTarget("Foot", *contact);
else {
    foot.weight = 0; // Applications can instead blend out over several frames.
    player.setIkTarget("Foot", foot);
}
```

For slope orientation, supply the incoming foot orientation in `foot.rotation`
(model space) and its local sole-up axis in `probe.soleUp`. The helper minimally
rotates that sole axis toward the hit normal, preserving the incoming heading
as far as a single swing allows. `worldUp` defaults to +Y; ray distances and sole
offset are world units. Default slope limit is 60 degrees. The default filter
ignores sensors and includes solids of all layers; an explicitly supplied filter
is honored unchanged. Self exclusion is always the caller's responsibility.
A miss or too-steep hit returns `nullopt`. It reads existing registered collision,
so synchronize the scene first. No raycast is issued by the animation player.

Probe the pre-IK pose after animation and batch targets into one `setIkTargets`
call when controlling several feet. Contact locking, gait phase, pelvis offsets,
moving-platform anchors and smoothing remain application policy. No automatic
terrain, mesh or object search is performed.

## Standalone solver and limits

`solveIk(skeleton, inputPose, constraints, targets, outputPose)` supports static rigs
and custom animation pipelines, including overlapping input/output. Failure leaves
the output unchanged. Use `ikJointTransform` to inspect a joint's full model matrix
or `ikJointTarget` for its incoming model-space position and orientation.
The solver uses bounded stack scratch, no previous-frame state and no per-solve
heap allocation. Warm player target updates reuse owned pose buffers.

Supports 1–256 joints, 0–32 constraints, finite targets, nonzero quaternions,
nonzero limb segments, direct three-joint chains and positive uniform joint
scales. World conversion requires an affine rotation/translation/positive uniform
scale placement. Nonuniform scale, reflections and shear are rejected for active IK.
Singular poles use the incoming knee plane or a deterministic perpendicular.
Look-at handles coincident targets as no-ops and antiparallel directions
deterministically. Limits are a swing cone, not anatomical hinge/twist constraints.
There is no full-body balancing or multi-joint iterative solver.
[Animation retargeting](animation-retargeting.md) is a separate optional clip bake.

## Editor, demo and persistence

Open `?scene=ik&editor=1`, select **IK character**, then **Animation library &
state machine → Inverse kinematics**. The authored block rig demonstrates two
feet grounded on sloped pads, a reaching arm and bounded head aim over animated
weight shifts. Its initial foot targets come from real physics raycasts. Contact
targets remain fixed afterward; moving the pads requires probing again.

Author names, solver/joint chains, target/pole coordinates, weights, optional tip
quaternions, look axes/limits and ordering. Graph preview exposes per-instance
target/pole coordinates, enable/weight controls and residuals. Pause freezes clocks
while explicit target controls remain responsive. Stop restores the source scene.
Library history, scene Apply undo/redo and file validation use the existing paths.

Scene format **33** retains constraints and live targets; readers accept 1–32,
with IK empty in those older records. Diagnostics and scratch matrices are rebuilt.
`.weanim` keeps WEANIM1 with **ANIM 3** metadata and an embedded v33 scene; metadata
versions 1–2 remain readable. Same-rig library apply remaps IK joint indices by
joint identity, like layer masks. Older engines reject new formats.

The separation between ordered modifiers and focused solvers follows the useful
design direction in [Godot's IK overview](https://godotengine.org/article/inverse-kinematics-returns-to-godot-4-6/);
this engine uses its own analytical two-bone and single-joint look-at implementation.
See [validation](inverse-kinematics-validation.md).
