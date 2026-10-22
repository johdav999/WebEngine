# Animation retargeting

Retarget sampled clips to a compatible character with different proportions or
bind orientations. This is an explicit bake into ordinary destination-local poses:
playback, layers, blend trees, markers, root motion, IK and renderer skinning use
their existing paths. Destination geometry, bone offsets and inverse binds remain
authored by the destination rig.

Try `?scene=retarget&editor=1`. The blue source and taller gold character share a
wave but have different arm lengths and forearm bind orientations. Select
**Retargeted character** and open **Animation library & state machine**.
The example is application code in `src/retarget_example.hpp`.

## Editor workflow

1. Select the destination skeletal mesh and open the animation editor.
2. Choose **Retarget clips** and an animated `.fbx`, `.glb` or `.weanim` source.
   Existing model decoders supply the skeleton and sampled clips. Sources stay
   local; model geometry and textures are released after decoding.
3. Review each destination joint's source selection. Exact names are suggested.
   Select **Keep bind pose** for unmapped joints. Map each source at most once.
4. Choose translation modes. **Keep bone offset** is the default for non-root
   joints. **Scaled animation offset** transfers displacement relative to rest,
   useful for root travel and pelvis bob. Exact-name root suggestions use it.
5. Review **Translation scale**. **Estimate scale** uses the median nonzero mapped
   rest-segment length ratio in model space, or 1 if no usable segments exist.
   It is a heuristic: several unchanged segments can give 1 even when limbs grow.
   Override it for the intended stride or units. **Model rotation X/Y/Z/W** is a
   quaternion mapping source model axes to destination axes; identity is default.
6. **Save mapping profile** / **Open mapping profile** reuse named correspondence,
   translation choices, scale and orientation as `.weretarget`.
7. **Bake retargeted clips** appends every source clip in one library history edit.
   Preview/scrub a baked clip. **Apply to mesh** creates the normal scene undo unit;
   **Save .weanim** keeps library organization and **Save scene** keeps applied clips.

Source selection and mapping drafts do not edit the scene or library history.
Structurally valid drafts with missing joints or incompatible ancestors remain
editable with a diagnostic and disabled Bake. Invalid numeric values, duplicate
mappings, corrupt profiles and failed source loads preserve the previous draft.
Bake validates everything before publishing a replacement library. Close/discard,
document rebind and scene invalidation release the staged source. Async file reads
check document generation; Play gates source/profile changes.

The existing **Import clips** / `AnimationLibrary::append` remains strict about
matching names, parents and bind poses. **Open library** also retains destination
compatibility checks. Retargeting is selected deliberately.

## Portable API

Link `engine_core`; include `engine/animation_retarget.hpp` or
`engine/animation_library.hpp`.

```cpp
auto profile = engine::matchAnimationJoints(source, target);
// For differently named rigs, replace suggestions with explicit mappings:
// profile.joints = {{"Hips", "Pelvis", engine::RetargetTranslation::ScaledDelta},
//                   {"LeftArm", "Upper arm", engine::RetargetTranslation::Bind}};
profile.translationScale = engine::estimateRetargetScale(source, target, profile);
profile.validate(source, target);

// A new immutable library; the original is unchanged.
auto next = library->appendRetargeted(source, profile);
// On the idle scene owner, or through EditorSession for scene history:
next->apply(character);

// Lower-level standalone bake; no retained pointers or input mutation.
std::vector<engine::AnimationClip> clips =
    engine::retargetAnimations(source, target, profile);
auto text = engine::retargetProfileText(profile);
auto loadedProfile = engine::parseRetargetProfile(text);
```

The caller owns both immutable input skeletons for the synchronous operation.
There are no subscriptions, scene handles, clocks, renderer dependencies or
per-frame retarget allocations. Bake at an authoring/loading boundary. Publishing
to a live component retains the normal idle-owner and guarded-traversal rules.
Only clips transfer: source controller, layers, IK settings and library metadata
are not copied. Existing destination controller references and clip IDs stay valid.
Appended clips receive new stable IDs and the normal Imported folder/defaults.

## Correspondence and pose math

Names must be unique within each rig, with an empty name allowed only for one root.
Mappings are one-to-one and require at least one mapped joint for bake/estimation.
For each mapped pair, its nearest mapped ancestors must also correspond. Unmapped
helper bones are allowed; changing the mapped ancestor topology is rejected.
Unmapped destination joints retain their **local** bind pose and therefore follow
animated parents. Source helper rotations contribute to mapped descendants.

For model-space quaternions, let `A` map source axes to target axes, `S` be the
animated source orientation, `S0` its bind orientation and `T0` target bind:

```text
desired target orientation = A * S * inverse(S0) * inverse(A) * T0
target local orientation = inverse(animated target parent) * desired orientation
```

Thus a source rest sample maps to target rest even with different bind rotations.
Default translation keeps each target local bind offset, preserving its bone
length. `ScaledDelta` measures source displacement relative to its nearest mapped
ancestor, brings that displacement through the source ancestor's bind transform
to model units, applies `A` and the chosen scale, then reconstructs it using the
target ancestor's bind and animated transforms. Roots use model-space displacement.
This avoids applying unit scale twice and accommodates intermediate helper joints.
Target local bind scales are retained. Clip sample count, duration, name, marker
times/order/payloads are preserved. Runtime interpolation still occurs between
the baked samples; the bake does not add resampling or curve reconstruction.

## Validation, storage and limits

- At most 256 joints and 64 clips; source and output poses each at most 32 MiB.
  `appendRetargeted` also checks the combined destination library budget before bake.
- Local bind scales must be positive and uniform. Source sampled scales must match
  bind scale within relative `1e-5`; animated scale is rejected during bake.
  Existing skeleton validation, finite-transform and inverse checks also apply.
- At most 256 mappings; names at most 256 bytes without control characters.
  Translation scale is finite in `[0.0001,10000]`. Model quaternion must be finite
  with nonzero length and is normalized for evaluation.
- Browser source files are at most 64 MiB and retain existing importer limits.
  Profiles are at most 256 KiB. Format `WERETARGET 1` contains scale, quaternion
  XYZW, mapping count and quoted source/target names plus mode `0=Bind,1=ScaledDelta`.
  Parsing checks the entire input; partial mappings are allowed as drafts.

Baked clips need no new scene or library encoding. Current scene format 34 and library
ANIM3 retain baked clips; retargeting adds no new codec fields. Mapping/source provenance is saved separately in the
profile, not embedded in the applied scene or `.weanim`.

This is rest-relative transfer for explicitly compatible hierarchies. There is no
semantic joint-name inference, humanoid classification, joint-limit calibration,
automatic ground/contact correction, twist redistribution, full-body optimization,
animated/nonuniform scale transfer or runtime streaming retargeter. Use
[IK](inverse-kinematics.md) for authored contact adjustment. Large changes in
proportions can require animation cleanup even when correspondence is valid.

See [validation](animation-retargeting-validation.md),
[animation authoring](animation-authoring.md) and
[layers/blend trees](animation-layers.md).
