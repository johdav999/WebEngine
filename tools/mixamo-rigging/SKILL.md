---
name: mixamo-rigging
description: Fit a supplied Mixamo armature to an unrigged humanoid mesh and bind it with Blender in a background process, then export a skinned GLB for WebEngine. Use for character skinning and Mixamo asset preparation, not runtime animation playback.
---

# Mixamo character preparation

Use an isolated Blender background process. Discover the installed Blender executable;
never reset a user's interactive scene. Obtain the character and a user-supplied
Mixamo reference rig in its rest pose. Do not download licensed characters or use
the Mixamo website without a request. This workflow does not reproduce Adobe's
automatic skeleton placement service.

Inspect the character's units, facing direction, topology and rest pose. Fit the
reference armature's joints to the character in a staging copy, preserving bone
names, parent relationships and roll conventions. A bounding-box scale alone is
not a fit. Inspect hips, shoulders, elbows, wrists, knees, ankles and fingers from
front and side. Require an explicit artist decision when anatomical placement is
ambiguous. For nonhumanoid characters choose an appropriate rig instead.

Use [bind.py](scripts/bind.py) once alignment is known. Supply either an already
fitted rig with `--aligned`, or `--fit` JSON mapping exact bone names to `head` and
`tail` XYZ arrays in Blender world coordinates (optional `roll` in radians).
Unlisted bones retain their original rest positions. The script fits endpoints,
generates automatic weights, retains and normalizes four influences per vertex,
rejects unweighted vertices, and stages a GLB, editable blend and report together.
Inputs are never overwritten; the output directory must not exist. A two-frame
`Rest` clip is exported so the engine animation editor can open the new character.

```powershell
& $blender --background --factory-startup --python-exit-code 1 --python "$skillRoot/scripts/bind.py" -- --mesh character.glb --rig fitted-rig.fbx --aligned --output prepared-character
```

Accepted inputs: `.blend`, `.fbx`, `.glb`, `.gltf`, `.obj`. Choose `--armature NAME`
if the donor contains multiple armatures. Mesh input must be unrigged; do not strip
an existing rig without a user request. Meshes must be positioned in the same
world coordinate frame as the fitted rig. The helper rejects constraints and
existing modifiers rather than baking unknown geometry changes.

Inspect the resulting `.blend` in rest pose and test shoulder, elbow, hip and knee
bends in a separate preview copy. Automatic weights are a starting point: correct
cross-limb influence and joint collapse, then rerun weight/export checks. Do not
claim production-quality skinning solely from complete weight coverage. Blender's
[automatic-weight contract](https://docs.blender.org/manual/en/4.5/animation/armatures/skinning/parenting.html)
describes the limitations of bone heat weights.

Import `character.glb` through WebEngine's normal GLB importer. Load separate
Mixamo FBX clips with `FbxImporter::loadAnimationsMemory` (or native
`loadAnimationsFile`), build a `matchMixamoJoints` profile, review scale/rest pose,
then `AnimationLibrary::appendRetargeted`. Browser: Retarget clips → choose FBX →
Match Mixamo joints → Bake → Apply. Save the `.weretarget` mapping and `.weanim`
library. A meshless FBX has no skin-cluster bind matrices: verify its authored
default transforms are the intended rest pose before baking.

Validate the exported GLB through the engine, then verify visible deformation in
both browser and Windows. Record Blender version, source files, fitting choices,
weight diagnostics and actual checks. Report missing tools/assets or unperformed
visual acceptance explicitly; do not imply an actual character was rigged when
only the workflow or synthetic fixtures were tested.
