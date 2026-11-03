# Mixamo rigs and animation import

Rigged Mixamo FBX characters use the existing `FbxImporter::loadMemory/loadFile`
and `SkeletalMeshComponent` paths. Blender-authored skinned GLB characters use
`GlbImporter`. Geometry, inverse binds, strongest-four weights and runtime playback
remain ordinary engine resources on browser and Windows.

## Animation-only FBX

`FbxImporter::loadAnimationsMemory(bytes, options)` returns an immutable `Skeleton`
with sampled clips without requiring triangle geometry. Native Windows also has
`loadAnimationsFile(path, options)`. Both use the same portable ufbx evaluation,
Y-up meter conversion, hierarchy limits and sample budgets as character import.
Geometry conversion, image decoding and companion-file resolution are skipped.
The parser still reads the bounded FBX structure. Input bytes and options are
borrowed only for the synchronous call; failure publishes no resource.

Skinned sources retain their skin-cluster rest transforms and mesh ancestors.
Unskinned sources collect declared FBX bones and their ancestors, using authored
default node transforms as rest pose. An animation-only file cannot supply missing
skin bind data: verify the export's rest pose rather than assuming its first
animated frame is a T-pose. Files without bones or skeletal clips reject. The
returned sentinel-only binding table is for clip processing, not geometry binding.
Normal mesh loading still rejects files without triangle meshes.

Defaults: 32 MiB source file, 128 MiB per parser allocator, 32 MiB sampled poses,
30 Hz including endpoints; allowed rates 1–120 Hz, at most 256 hierarchy joints
and 64 clips. `FbxImportOptions` controls these budgets. No implicit network IO.

## Match and bake

```cpp
auto source = engine::FbxImporter::loadAnimationsMemory(bytes);
auto profile = engine::matchMixamoJoints(*source, *character.mesh->skeleton);
profile.translationScale = engine::estimateRetargetScale(
    *source, *character.mesh->skeleton, profile);
auto library = engine::AnimationLibrary::fromSkeleton(character.mesh->skeleton);
auto next = library->appendRetargeted(*source, profile);
next->apply(character); // At the idle scene-owner boundary.
```

The explicit `matchMixamoJoints` preset recognizes canonical body and finger names
with optional namespace and `mixamorig` prefix (for example `mixamorig:Hips`,
`mixamorigHips`, `character:Hips`, `Hips`). It does not rename either skeleton.
Unknown joints remain unmapped. Duplicate canonical aliases, missing Hips or
incompatible nearest mapped ancestors reject. Hips uses `ScaledDelta` even below
helper roots; other mapped joints use `Bind` translation. Review the suggested
mapping and estimated scale; the function itself returns scale 1. Exact-name
`matchAnimationJoints` behavior is unchanged.

Retargeting keeps the destination rest lengths, geometry and inverse binds.
It transfers rest-relative orientation and hip displacement, preserving clip
duration, samples and markers. It cannot infer arbitrary humanoid semantics,
correct foot contact or repair a wrong reference rest pose. Existing uniform,
positive, nonanimated joint-scale restrictions still apply. See
[retargeting contracts](animation-retargeting.md).

Browser: select the destination skeletal mesh, open the animation editor, choose
**Retarget clips**, select the FBX, then **Match Mixamo joints**, review the draft,
**Bake retargeted clips**, preview and **Apply to mesh**. Source loading and preset
failure preserve the previous valid draft; Bake remains one library undo edit.
**Import clips** also accepts animation-only FBX but deliberately retains strict
joint/hierarchy/rest equality checks. Native consumers use the same portable API
and existing `EditorSession::setAnimationLibrary`; no new native graphical panel
is added. Save `.weretarget`, `.weanim` and scene through their existing codecs.

## Rig an unweighted character

The [mixamo-rigging skill](../tools/mixamo-rigging/SKILL.md) runs Blender headlessly
to fit a supplied Mixamo armature using explicit bone endpoints or an already
fitted reference, bind automatic weights and export a skinned GLB plus editable
blend file and diagnostics. It retains four normalized influences and rejects
unweighted vertices. A two-frame `Rest` clip allows the engine animation editor
to open the newly bound character before importing movement clips.

Anatomical fitting and weight cleanup are offline authoring responsibilities.
This is not Adobe's cloud auto-rigger, automatic anatomical landmark detection,
or an engine runtime weight generator. A real character needs rest-pose and
bending inspection; weight coverage alone does not establish visual quality.

See [validation evidence](mixamo-validation.md).
