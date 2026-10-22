# Animation layers and blend trees

The existing `AnimationComponent` supports optional multi-clip blend trees and
ordered pose layers in its immutable `AnimationControllerDefinition`. The same
portable evaluation feeds WebGPU and WebGL2 skinning. No renderer, input binding,
physics writer, or second animation scheduler is introduced.

## Speed and direction

Set `AnimationControllerState::tree` to an `AnimationBlendTree`; leave `blend`
empty. A state or layer can use a single clip, the existing two-clip blend, or a
tree. `state.clip` remains a valid fallback clip for manual controller detachment.

```cpp
auto controller = std::make_shared<engine::AnimationControllerDefinition>();
controller->parameters = {
    {"Speed", engine::AnimationParameterType::Number, 0.0},
    {"Carry", engine::AnimationParameterType::Number, 0.0}
};
controller->states = {{"Locomotion", idleClip, true, 1, {}, {}}};
controller->states[0].tree = engine::AnimationBlendTree{
    0, {}, {{idleClip,0,0}, {walkClip,1.5,0}, {runClip,4,0}}, true
};
mesh.animation().setController(controller);
mesh.animation().setParameter("Speed", 2.5); // Application units, e.g. m/s.
```

1D trees linearly blend the two neighboring samples and clamp outside the range;
sample order is unrestricted. For 2D, set `parameterY` to a different Number
parameter and author `(x,y)` samples in Cartesian space, typically sideways and
forward velocity. This combines speed and direction without an angle-wrap seam.
2D uses normalized Cartesian gradient-band weights: each sample's influence is
the minimum clamped projection against the other samples. It reproduces exact
samples and continuously blends between them, clamping inputs to the sample
bounding box. Collinear points are supported. This is not polar-angle blending
or a triangulated interpolation contract. `animationBlendWeights(tree,x,y)`
returns the weights for a validated tree without heap allocation.

Translations/scales use normalized weighted sums; rotations use normalized,
hemisphere-aligned quaternion sums anchored to the first contributing sample.
Authored order is deterministic. Existing two-clip blends retain shortest-arc
spherical interpolation. No recursive/nested tree nodes are provided.

`synchronizePhase` defaults true. The longest clip defines the tree cycle; each
source samples at `baseSeconds * sourceDuration / treeDuration`. Changing weights
does not restart or retime the cycle. Author equivalent foot-contact phases in
the clips; synchronization does not discover contacts or prevent foot sliding.
With phase synchronization off, clips wrap independently in seconds. Zero-duration
sources hold their sole pose. Non-looping trees hold their final pose; normalized
exit time uses the longest duration. State speed multiplies the tree clock.
Input smoothing and animation choice remain application policy.

## Carrying, looking and interacting

```cpp
engine::AnimationLayer carry;
carry.name = "Carry";
carry.motion.clip = carryClip;
carry.mask = engine::animationSubtreeMask(*mesh.mesh->skeleton, spineJoint);
carry.weightParameter = 1; // Carry Number input, clamped to [0,1].
auto layered = std::make_shared<engine::AnimationControllerDefinition>(*controller);
layered->layers.push_back(carry); // Publish a new immutable definition.
mesh.animation().setController(layered);
mesh.animation().setParameter("Carry", 0.8);
```

Layers apply in vector order, after the base state's crossfade and root stripping.
`Override` interpolates selected local joint poses. `Additive` adds local
translation deltas, post-multiplies a weighted shortest-arc rotation delta, and
multiplies scale ratios relative to the bind pose. Set `referenceClip` and
`referenceTime` for an explicit sampled reference instead. Reference scale
components must have magnitude greater than `1e-6`. Use compatible authored poses;
mirrored/singular interpolated transforms are invalid.

An empty mask selects all joints; otherwise supply exactly one `[0,1]` weight per
skeleton joint. Masks do not automatically propagate, except through the
`animationSubtreeMask` authoring helper, which includes a joint and its descendants.
The effective joint weight is mask weight × layer weight × optional clamped Number
parameter. Library application remaps masks by joint identity for same-rig targets
whose joint array order differs.

Each layer's `motion` supports clip, two-clip blend or tree, with its own loop flag,
speed and clock. Clocks advance even at zero weight, remain independent of base
state transitions, and non-loop layers hold their last pose. Use
`restartLayer("Interact")` when an application starts a one-shot action. It resets
only that layer and emits no movement or markers. `layerTime(name)` inspects its
clock. Layer state machines and automatic trigger-to-layer assignments are not
provided; gameplay controls the weight parameter and restart explicitly.

Layers continue over a completed non-loop base pose. Explicit pause, disabled
scene subtrees, and application pause freeze all clocks. Resume continues them;
fixed ticks and synchronization do not advance them. Base seeking does not seek
layers. Manual playback/controller removal removes overlays; manual crossfade
freezes the composed outgoing pose to avoid a jump. A detached tree
resumes its state's fallback clip on the next playback tick. Skeleton replacement
cancels controller/layers/output under the existing player lifetime contract.

Only the **base** generates root-motion and marker output. Base tree markers
carry source weights and the common unwrapped tree-clock crossing time, sorted by
time, then sample order and marker index. Synchronized movement scales each source
interval to its clip clock. Frozen outgoing fades do not emit output. Layers are
pose overlays and do not emit events or actor movement. While extraction is enabled,
layers skip the selected locomotion joint and its ancestors; descendants still
animate. This keeps an upper-body pose from changing the movement/extraction anchor.
Gameplay still owns collisions and consumption of `consumeOutput()`.

## Authoring and persistence

In **Details → Animation library & state machine**, select a state and use
**Blend tree → 1D speed / 2D direction**. Select numeric axes, edit sample clips and
coordinates, and enable phase synchronization as needed. Under **Animation layers**,
add/reorder named layers, select override/additive mode, clips, weights/parameters,
subtree/custom masks and optional reference poses. Layers can author their own
trees. Preview uses live parameter controls and **Restart [layer]** buttons;
Stop returns to the untouched source scene.

Edits use the existing bounded library undo history. Apply swaps the complete
animation state through one scene history command. Scene archives **v33** retain
trees, masks, layer settings, parameters and clocks; versions 1–31 still load with
composition disabled; v32 records retain composition with IK off. `.weanim` keeps
its WEANIM1 envelope with **ANIM 3** metadata and an embedded v33 scene; ANIM 1–2
metadata remain readable. Old engines reject
new archives rather than silently dropping layers.

Limits: 16 layers, 64 points per tree, 256 joints; finite sample coordinates within
±1,000,000 and separation at least `1e-6` in the active dimensions. Axes and weight
parameters must reference Number fields. Duplicate layer names, conflicting
two-clip/tree sources, invalid masks/references and invalid initial composed poses
reject installation before replacing valid playback. Existing controller/library,
allocation and event-queue budgets still apply.

See [animation control](animation-control.md), [authoring](animation-authoring.md)
and [validation](animation-layers-validation.md). [Inverse kinematics](inverse-kinematics.md)
adds optional limb placement and procedural look-at after layers.
[Retargeting](animation-retargeting.md) bakes compatible source clips into ordinary
destination poses before playback. Independent layer event queues and recursive
graphs remain extensions.
