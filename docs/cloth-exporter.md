# Blender clothing exporter

`tools/blender-cloth/export_cloth.py` exports one physical garment to `.wecloth`.
It is a Blender add-on and a background CLI. `engine/cloth_asset.hpp` loads those
bytes through the optional `WebEngine::Clothing` library on Windows and in browsers.
Blender is an authoring dependency only; no Python, Blender or filesystem API is
needed by the runtime loader. This is for prepared cloth, not automatic fitting,
retopology or rigging of Tripo output.

## Authoring

1. Import the character into Blender, fit the garment in its rest pose, and transfer
   bone weights. Use the same armature as the character. Keep the same scene origin,
   rest bones when exporting the character GLB and garment. Apply world transforms
   on both garment and armature to identity before exporting either asset. The
   current import/bind-space contract rejects non-identity object transforms;
   transforming bone rest data within the armature is supported.
2. Use one Blender unit per meter and scene unit scale 1. Apply intended topology
   modifiers. Keep exactly one Armature modifier using vertex groups, linear
   skinning, no envelopes and no Preserve Volume. Bake/remove constraints first.
   Export reads base mesh/rest bones, independently of the current animation pose.
3. Limit each vertex to four nonzero deform-bone influences. Export normalizes them;
   it rejects extra influences rather than silently dropping weights. Use at most
   1,024 vertices, 2,048 triangles, 2,048 unique triangle edges and 256 bones.
4. Paint these vertex groups (case sensitive):

   | Group | Meaning |
   | --- | --- |
   | `WE_Pin` | Required group. Weight >= 0.5 pins a vertex to its skinned target. |
   | `WE_MaxDistance` | Optional per-vertex factor times maximum distance in meters; missing assignments default to 1. Zero also pins. |
   | `WE_InvMass` | Optional interpolation between minimum and maximum inverse mass; missing assignments default to minimum. Larger values mean lighter fabric. |

   At least one vertex must be pinned. Inverse mass is always positive, including
   pins; pinning comes from zero maximum distance, not zero inverse mass.
5. Use continuous UVs: one UV value per mesh vertex. UV seams requiring vertex
   duplication or mirrored UV orientations at shared vertices are rejected.
   Export uses smooth vertex normals. One material slot
   is allowed; textures and material graphs are assigned by the game after loading.
   Remove shape keys, loose vertices, duplicate/degenerate faces, zero-length links
   and non-manifold edges. Disconnected fabric pieces do not gain stitching.

Install the single Python file through Blender's add-on **Install from Disk**, enable
**WebEngine Cloth**, select the garment in Object Mode, then choose
**File > Export > WebEngine Cloth (.wecloth)**. Maximum distance, inverse mass range,
drag and self-separation are editable in the export options. The add-on reports
validation failures and preserves any existing file on failure.

For automated export (put `--python-exit-code 1` before `--python`):

```powershell
blender character.blend --background --python-exit-code 1 --python tools/blender-cloth/export_cloth.py -- --object Cape --output assets/cape.wecloth --max-distance 1 --mass-min 1 --mass-max 20
```

The output directory must already exist. Output replacement uses a temporary file
in the same directory and atomic rename. The exporter never changes object data,
pose, selection or transforms; Blender may update derived mesh caches.
Replace `--output ...` with `--validate-only` to validate without writing an asset.

## Runtime integration

```cpp
#include "engine/cloth_asset.hpp"

// Fetch/read bytes through the application's existing asset service.
// characterMesh is the character GLB imported with its original rest hierarchy.
auto garment = engine::loadClothAsset(bytes, characterMesh->skeleton, fabricMaterial);
engine::ClothingCatalog catalog;
catalog.skeleton = characterMesh->skeleton;
catalog.items.push_back({"cape", "back", garment.mesh, 0, garment.cloth});
// Add authored body pieces if the catalog controls body visibility.
engine::ClothingInstance outfit(driver.id(), catalog);
outfit.apply(scene, {{"cape"}});
// After advancing the character pose, once per simulation tick:
outfit.advanceCloth(scene, simulationDelta, bodyCapsules, {0,-9.81f,0}, environment);
```

The loader checks each bone's unique name, parent link and world rest matrix, then
finds a matching inverse-bind entry in the driver's palette. Matrix tolerance is
absolute `1e-4` per element. Export roots may have imported armature/scene ancestors;
their complete world bind transform is still checked. Unweighted control bones
need no skin binding, but must exist in the driver hierarchy. Export the character
with all bones, without hierarchy flattening or independent retargeting.

Compatible palette order differences are remapped explicitly. The result retains
the exact immutable driver `Skeleton` pointer, and normalized weights use its
binding indices. An incompatible rig, unknown format, malformed/truncated data or
invalid cloth definition throws `std::invalid_argument` before publishing an asset.
The existing outfit is untouched. No automatic bone-name-only attachment is used.

Geometry is Y-up meters in the same world-baked mesh bind space as the engine GLB
importer. Blender's bone bases are converted with the same Z-up to Y-up matrix.
Runtime driver transforms must remain rigid and unit scale. The loader accepts an
optional immutable engine material; absent one, it creates a default double-sided
material. Textures, game item IDs, equipment slots, body masks and collider authoring
remain application responsibilities. Keep assets immutable after loading.

## Simulation defaults and limits

The exporter builds stretch links along triangulated edges (compliance `1e-7`),
including triangulation diagonals, and opposite-vertex bend links across interior
edges (`2e-4`). These are distance constraints, not a calibrated textile model.
Defaults: 8 iterations, damping 2, thickness 0.015 m, friction 0.2, teleport distance
2 m, self separation 0.02 m, aerodynamic drag 1.2, swept and edge contacts enabled.
All triangles are cloth collision/aerodynamic surfaces. See
[dynamics](cloth-dynamics.md), [surfaces](cloth-surfaces.md), [edges](cloth-edges.md)
and [materials/motion](cloth-materials-motion.md) for solver bounds and limitations.

There is no separate low-resolution simulation proxy, seam welding, material/texture
export, body fitting, inter-garment collision, animation export or runtime Blender
cloth cache. Use an ordinary skinned GLB workflow for fitted clothes that do not
need cloth physics. Exported physical garments are deliberately bounded.

## WECG version 1 wire format

All integers/floats are little-endian 32-bit; floats are IEEE-754 binary32. Maximum
file size is 1 MiB. No implicit padding or trailing bytes are permitted.

1. Eight unsigned integers: magic `0x47434557` (`WECG`), version `1`, bone count,
   vertex count, triangle count, link count, iterations, flags (bit 0 swept, bit 1 edge).
2. Six floats: damping, thickness, friction, teleport distance, self separation, drag.
3. Each bone in parent-first order: unsigned UTF-8 name byte count (1..255), name
   bytes without NUL, signed parent index (-1 for root), 16 column-major world rest floats.
4. Each vertex: position XYZ, normal XYZ, UV (top-left origin), four unsigned bone
   indices, four weight floats, maximum distance, inverse mass. Zero-weight slots
   still contain valid exported bone indices.
5. Each triangle: three unsigned vertex indices, preserving winding.
6. Each link: two unsigned vertex indices and compliance float.

## Validation

`tests/blender_cloth_fixture.py` runs in real Blender, exports a two-bone
garment plus the matching character GLB, exercises rejection/atomic output and
add-on registration, and regenerates `tests/cloth_asset_fixture.hpp`. The checked-in
fixture makes C++ tests independent of Blender installation. Run:

```powershell
blender --background --factory-startup --python-exit-code 1 --python tests/blender_cloth_fixture.py
cmake --build build/clothing-native-vs2022 --config Release --target clothing_tests --parallel 1
ctest --test-dir build/clothing-native-vs2022 -C Release -R '^clothing_contracts$' --output-on-failure
# In the repository-local pinned Emscripten environment:
cmake --build build/web-release --target clothing_tests -j 1
ctest --test-dir build/web-release -R '^clothing_contracts$' --output-on-failure
node scripts/cloth-export-browser-smoke.cjs
```

The browser script requires Playwright and a Chromium installation; set `NODE_PATH`
for the installed module and `BROWSER_EXECUTABLE` for an explicit Edge/Chrome binary.
It starts and closes its own loopback server and runs import/simulation contracts
in the real browser. It does not claim rendered garment appearance or mobile acceptance.
Actual validation evidence is recorded in [cloth-exporter-validation.md](cloth-exporter-validation.md).
