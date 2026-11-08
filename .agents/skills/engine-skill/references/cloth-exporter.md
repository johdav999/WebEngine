# Blender cloth asset integration

Verify the consumer engine pin contains `engine/cloth_asset.hpp`,
`src/cloth_asset.cpp` and `tools/blender-cloth/export_cloth.py`. Link optional
`WebEngine::Clothing`. The exporter is offline Blender tooling; runtime import is
portable C++ with no IO or Blender dependency. Full contracts, format and evidence
are in engine-root `docs/cloth-exporter.md` and `docs/cloth-exporter-validation.md`.

Use the Blender add-on File > Export > WebEngine Cloth, or:

```text
blender character.blend --background --python-exit-code 1 --python tools/blender-cloth/export_cloth.py -- --object Cape --output assets/cape.wecloth
```

`--validate-only` replaces `--output` for a no-write validation pass. Fit/retopologize
generated art first. Keep the character and garment in the same rest space and
armature; export the character GLB with all bones and no independent retargeting.
One Blender unit is one meter, scene unit scale 1. Exactly one linear Armature
modifier, no envelopes/Preserve Volume, constraints, shape keys or other modifiers.
Each vertex needs 1..4 nonzero deform-bone weights. Export normalizes weights.
Apply garment and armature world transforms to identity before exporting both
assets. Non-identity object transforms are rejected under the current GLB bind-space
contract; bone rest transforms within the rig remain supported.

- `WE_Pin`: required group; weight >= .5 pins; at least one zero-distance vertex.
- `WE_MaxDistance`: optional factor times `--max-distance` (default 1 m); absent
  vertex assignment defaults to 1, explicit zero pins.
- `WE_InvMass`: interpolates `--mass-min` (default 1) to `--mass-max` (default 20);
  missing assignment uses minimum. Larger values are lighter, never zero for pins.

WECG v1 preserves vertex order and converts geometry/bone bases to engine Y-up.
Maxima: 1 MiB, 1,024 vertices, 2,048 triangles/unique edges, 8,192 links, 256 bones.
Continuous per-vertex UVs only; seams requiring render duplication are rejected.
Mirrored UV orientations sharing a vertex are also rejected to prevent tangent splits.
Smooth normals, one material slot; the caller supplies textures/material after load.
No simulation proxy, welding, auto-fit or inter-garment collision. Export builds
triangulated-edge stretch links (1e-7 compliance) and opposite-vertex bend links
(2e-4); defaults are 8 iterations, damping 2, thickness .015, friction .2,
teleport 2, self separation .02, drag 1.2, swept/edge contacts enabled.

```cpp
auto asset = engine::loadClothAsset(bytes, driver.mesh->skeleton, fabricMaterial);
catalog.items.push_back({"cape", "back", asset.mesh, 0, asset.cloth});
```

The loader checks unique bone names, non-root parent links, complete world rest
matrices and inverse-bind matrices (absolute tolerance 1e-4). Export roots may sit
beneath imported scene/armature ancestors. Unweighted controls need no palette
entry but must exist in the hierarchy. Compatible palettes are explicitly remapped
before the mesh shares the exact driver Skeleton pointer. Never bypass failures
by swapping pointers or matching names alone. Invalid bytes/rigs throw without
changing an existing outfit. Materials/assets stay immutable; no external paths
are read. Game IDs, slots, masks, asset IO and collision authoring remain game policy.

Advance via existing `ClothingInstance::advanceCloth` once per simulation update
after animation, with rigid unit-scale driver transforms. Existing pause/reset,
transient lifetime and candidate-publication contracts remain in force.

Validation: real Blender `tests/blender_cloth_fixture.py` regenerates the embedded
fixture; `clothing_contracts` exercises GLB compatibility, two-bone
skinning, painted values, simulation, palette remapping, truncation and invalid
rig rejection. Build/test native Windows and pinned local Emscripten separately.
`scripts/cloth-export-browser-smoke.cjs` runs the contracts in Chromium via
Playwright; it validates browser import/simulation, not rendering or mobile devices.
