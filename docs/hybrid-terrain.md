# Hybrid terrain milestones 1–5

Heightfields and authored static mesh sections share the existing scene, material,
rendering and physics systems. This milestone adds cell holes and a small mesh
section factory, plus the same cave/overhang example on browser and Windows.
Milestone 2 adds reversible hole painting and paired cave entrance/mesh editing.
Milestone 3 adds bounded automatic seam preparation and normal/material blending.
Milestone 4 adds hole-aware heightfield LOD and constrained cave-mesh simplification.
It does not implement Unreal's Mesh Terrain or Nanite.

## Heightfield holes

`TerrainData::holes` is empty by default (all cells solid), or contains exactly
`(samplesX-1)*(samplesZ-1)` bytes in row-major cell order. Zero is solid; one removes
both cell triangles. Other values and incorrect lengths reject before publication.
Configure the initial mask before `TerrainAsset::create`. Replace it immutably with
`asset->withHoles(mask)` or `asset->edit(brush)` using `TerrainBrushMode::Hole` or
`Fill`. Positive strength cuts/restores cells whose centres lie within the circular
brush; zero strength is a no-op. These are binary topology brushes, without opacity
or falloff. Sculpt and color brushes preserve holes.

```cpp
engine::TerrainData data;
data.samplesX=data.samplesZ=33;
data.heights.resize(33*33);
data.holes.resize(32*32);
for(unsigned z=9;z<22;++z)
    for(unsigned x=13;x<19;++x) data.holes[z*32+x]=1;
auto asset=engine::TerrainAsset::create(std::move(data));
scene.adoptChild(engine::makeTerrainActor("Ground",asset,true));
```

Base geometry, full/chunk triangle colliders, local `ground` and world terrain
queries all omit holes. Physical surface indices are filtered alongside triangles.
Cell lookup at an exact grid line chooses the positive-side cell (last cell at the
outer maximum); triangle raycasts/physics can still hit a neighbouring solid edge.
Use interior sample points when testing whether a cell is absent.

Cut chunks now generate coarser solid regions while preserving every hole and
chunk boundary segment. Mixed solid/hole tiles subdivide until entirely solid or
empty. Solid tiles merge only when every source sample lies within 0.00001 m of
their common plane; curved regions retain source triangles to avoid new shadow
receiver mismatch. Only planar solid tiles emit fans, with full-resolution samples on every tile
perimeter. This keeps mixed detail levels compatible without skirts or filling
holes. Only candidates that reduce triangles are retained. Tiny, heavily cut or
checkerboard chunks may keep full detail. Uncut chunks retain ordinary LOD.
Holes alone do not generate walls. Supply mesh sections near the cut perimeter and
optionally prepare their joins with `buildTerrainSeam`, described below. Avoid
overlapping coplanar triangles/colliders.

Entirely cut chunks retain their component/source identity but submit no mesh
draws. `collisionShape()` / `chunkCollision()` return null for empty geometry;
the actor factory and `TerrainStream` retain a disabled placeholder collider and
static body when collision was requested. Filling re-enables source collision,
preserving component handles; no placeholder enters physics. Terrain owns collider
enablement at empty/nonempty transitions; disable the actor to suspend collision
independently. This also works with source-only derived-chunk streaming. Provider
`TerrainPage` wire data does not expose holes; its edit API explicitly rejects
Hole/Fill modes. Remote hole paging remains outside this milestone.
Direct `TerrainAsset::createPatch` supports validated source masks and rejects
prepared meshes that fill holes. Supplied cut-patch LODs must match canonical
generated topology and vertex attributes; validation regenerates that bounded
chunk to check them. Older prepared cut patches may still omit LOD.

`TerrainEdit::holesChanged` marks topology edits. `withHoles` validates first,
reports a sorted dirty chunk list, shares unchanged meshes and rebuilds affected
geometry and full/chunk collision. Filling the last hole restores ordinary chunk
LOD. Source-only edits do not generate nonresident meshes. `applyTerrainEdit` and
`TerrainStream::apply` reject stale sources or incomplete hole dirty lists before
publication. Updates run at an idle owner-thread boundary; rebuilds are synchronous.

For an editor restore brush, `terrainRaycast(..., includeHoles=true)` intersects
uncut source triangles, including empty chunks. The default remains false and
physics never uses this authoring option. Main browser editor Details → Terrain
has **Cut hole** and **Restore ground**. Retained native/shared authoring uses
brush modes 4 and 5. EditorSession commits a whole stroke once, supports cancel,
and preserves exact collision snapshots across consecutive undo/redo commands.

## Authored mesh sections

[Terrain mesh lakes](terrain-mesh-lakes.md) prepare a joined basin, fitted water
surface and finite polygon/bottom region. The shared demo includes a small pond
in the Grand Chamber, with surface/underwater views and grouped persistence.


Static mesh sections also support [joined stalagmites and stalactites](terrain-mesh-formations.md)
through `buildTerrainMeshFormations`. This shared CPU operation refines a local
surface patch, produces tapered growth with bend/seed variation, preserves seam
locks, and regenerates LOD. The completed base supplies collision and ordinary
scene persistence. The cave demo uses two floor and two ceiling formations.


```cpp
// mesh is an immutable ordinary Mesh: vertices, indices, materials and parts.
scene.adoptChild(engine::makeMeshTerrainActor("Cave",mesh,true,stoneSurface));
```

`makeMeshTerrainActor(name, mesh, collision=false, surface={})` returns a detached
ordinary Actor with a MeshComponent and, optionally, a static triangle collider
and rigid body. Keep physical instances at scene root with unit scale; bake scale
into vertices. Calls validate before publishing any actor. Source and each supplied
LOD are static (no skin/morph), finite, nondegenerate triangles with at most 100,000
vertices and 100,000 triangles per section. Material parts cover the whole index
buffer in order. At most 16 flat LOD candidates have decreasing positive pixel
thresholds and matching material slots. Resources remain caller-published immutable
shared Mesh/Material objects; no additional resource owner or scene graph is added.

The helper does not partition, simplify, import or stream arbitrary meshes itself.
An existing importer can supply Mesh resources; applications place multiple
sections and choose residency. Collision always uses the base triangles, never
the selected render LOD. Existing layered/triplanar materials work on both types.
Triplanar affects surface textures; painted weights still require suitable mesh UVs.

`terrainGround` / `terrainRaycast` remain heightfield-only. For movement and hybrid
world queries use the existing `scene.physics().raycast`, sweep and character motor.
Start queries from the relevant elevation: a cave has both a floor and a roof.
No highest-height query can represent all surfaces at the same X/Z position.

Scene/asset/application archives write version 36. Version 35 added the hole mask to
TerrainData. Older versions default to no holes; mesh sections use existing mesh,
collider and body records. Old engines cannot read version 36. Rebuild and deploy
matching browser host/worker artifacts; this change does not advance consumer pins.

## Automatic seams and material transitions

Include `engine/terrain_seam.hpp` and prepare the section before publishing it:

```cpp
engine::TerrainSeamSettings settings;
settings.terrainOrigin={-16,0,-12}; // Source sample (0,0) in mesh-local metres.
auto joined=engine::buildTerrainSeam(*terrain,authoredMesh,settings);
scene.adoptChild(engine::makeMeshTerrainActor("Joined cave",joined.mesh,true));
```

The builder discovers edges separating holes from solid source cells. It snaps
nearby mesh vertices, inserts source boundary samples into nearby triangle edges,
and subdivides the resulting faces for a smooth transition. Corner samples take
priority over segment projection. Opposing undersides are excluded from matching
and blending, so nearby ceilings remain intact. Unmatched edges stay open; a mesh
must already approximate the intended opening. There is no arbitrary intersection,
automatic cave generation, long-gap bridge or guarantee for intersecting inputs.

`terrainOrigin` is a translation into mesh-local space; axes and scale must align.
Defaults are 0.25 m weld distance, 1.25 m blend width and 0.75 m maximum edge length.
Weld distance must be less than half the minimum source cell spacing. Blend width
must cover weld distance. The synchronous work budget defaults to 8 million units
(maximum 64 million), with at most 2,048 source boundary edges and 30,000 output
triangles / 90,000 vertices. Inputs are full source assets, not patches, with at
most 4,096 triangles and no authored LOD. Invalid input or exceeded budgets throw
before publishing anything. Result counters report boundary edges, moved vertex
occurrences, inserted samples and output triangles. A source without boundaries
returns the original mesh. `lodLevels=3` defaults to up to three derived candidates;
zero disables mesh LOD. Generation exactly welds duplicate attributes, locks all
vertices with a nonzero seam blend weight and every open/material boundary, then
simplifies only eligible interiors. Thresholds are 180/80/32 projected pixels;
target triangle ratios are 0.5/0.2/0.06 with relative errors 0.002/0.008/0.02.
Constraints may prevent these targets; only strictly reducing levels are retained.
Each level shares materials, preserves locked positions/normals/UVs and removes
zero-area collapse faces. No vertex positions are recomputed. LOD-enabled inputs
have at most 64 parts, bounding the three per-part passes separately from the seam
matching work counter. Geometry/collision budgets apply per level; derived levels
add retained memory and preparation time. Full source geometry owns collision.

The generated material uses existing `MaterialLayers`: heightfield layer zero,
mesh layer one, with a clamped linear weight texture and smoothstep distance
weights encoded in the generated UVs. Normals approach the heightfield normal at
the boundary. World triplanar tiling preserves source scale/offset and blends
albedo, roughness and packed layer normals using the existing four renderer paths.
Supported inputs are opaque, nonmetallic, nonemissive PBR or unlit solid colors,
or unweighted single-layer triplanar materials. Both sides must use the same shader
family and, when both are triplanar, sharpness. Conventional UV textures, painted
multilayer weights and height-based layer blending reject rather than silently
losing authored material data. Mesh UVs become seam weights. Material instances on
actors are not sampled: resolve them into input materials before preparation.

Rebuild explicitly after height, hole, mesh or source material edits. Publication
and collider replacement belong to the caller at an idle owner-thread boundary.
The demo prepares terrain, generated mesh and static shape before publishing them
together; undo/redo retains these immutable snapshots.

### Automatic LOD restoration (milestone 5)

`Mesh::lodRecipe` optionally owns a `MeshLodRecipe`: version 1, requested candidate
count (1–3), `lockBorders` (default true), and an empty or per-base-vertex 0/1 lock
mask. `buildTerrainSeam` attaches this recipe whenever LOD is enabled. It saves
explicit blend-band locks; loading never infers them from materials or UV values.
The algorithm uses the thresholds/ratios/errors above and retains only strictly
reducing candidates. Dense locked geometry can legitimately yield no levels.

`MeshOptimizer::validateLodRecipe` checks the recipe and static base mesh.
`MeshOptimizer::regenerateLods` synchronously builds a staged cache and swaps it
only on success. Without a recipe it is a no-op. It never changes base vertices,
indices, materials or collision. Version 1 is limited to 90,000 vertices, 30,000
triangles and 64 parts; animated/skinned/morph meshes reject. Optimize before
constructing the vertex-indexed recipe: `optimize` rejects recipe-bearing meshes.
An explicit `generateLods` call replaces the cache and clears a previous recipe;
use `regenerateLods` to retain the saved constraints.

Archive v36 stores the recipe, not derived mesh buffers. Scene and asset loading
regenerate each shared resource once, before publishing the staged document.
Application snapshots inherit this behavior through their scene documents.
Recipe decoding checks version, levels and a 90,000-byte lock cap before allocation.
Decode charges `512 * vertices + 64 * indices + 16 * parts * vertices` bytes against
`ArchiveLimits::maxAllocatedBytes` as a conservative cache/temporary/work allowance,
in addition to ordinary decoded allocations. Multiple resources share that aggregate
budget. This is bounded synchronous CPU preparation, not background streaming.
Unsupported recipes, invalid masks, corrupt files and exceeded budgets reject the
staged load; the running application remains owned by its caller.

Heightfield LOD continues to regenerate from saved source settings. Full mesh and
heightfield collision are restored independently of render detail. Versions 1–35
still load: their ordinary meshes have no recipe and no automatic mesh LOD rebuild.
Reprepare an older seam with `buildTerrainSeam`, or attach explicit validated locks
on a mutable base copy and regenerate before publishing; no heuristic migration
is performed. Newly saving that resource preserves the recipe.

For other static meshes, `MeshLodConstraints` is an optional second argument to
`MeshOptimizer::generateLods`. `lockBorders` defaults false; `lockedVertices` is an
empty span or one 0/1 byte per source vertex, borrowed only during the synchronous
call. Validation rejects incorrect masks before replacing the existing LOD chain.
Use exact vertex optimization before constructing the mask, not afterwards.
The ordinary no-argument-constraints path retains its existing behavior.

## Shared demonstration and builds

`examples/hybrid_terrain/scene.hpp` owns authored content and the explorer's path
policy. Both hosts expose overview, interior and overhead views, walking in/out,
detail selection and reset. Movement uses CharacterMotorComponent and Application's
fixed steps. Native F11 toggles fullscreen; Esc cancels a stroke or leaves brush
mode (use the window close control to exit). The demo is intentionally
a small authored scene, not a mesh sculpting editor or a large-world benchmark.

For free flight, right-drag the scene to rotate the camera. Click the browser
scene to focus it, then use **WASD** to fly relative to the view, **Q/E** to
descend/ascend, and **Shift** for faster movement (6 m/s normally, 18 m/s boosted).
Pitch is clamped near vertical and diagonal movement is normalized. **Escape**,
focus loss and resize release held controls. Form input does not move the camera;
left-drag remains the hole brush gesture. Flight is an inspection camera without
collision, independent of the walking explorer. View buttons reset its pose.
Checkpoint saves retain the camera pose, while held input and drag state reset.
`flight.hpp` consumes existing input actions on the Application input clock;
`flight.js` and DesktopHost bindings supply browser/Windows device input.
The browser adapter uses canvas focus and pointer capture, without pointer lock.
See [camera control validation](hybrid-terrain-flight-validation.md).

Choose **Cut hole** or **Restore ground**, then drag the surrounding ground.
Radius is 0.5–4 metres, default 1.5. One drag is one history entry; Escape, lost
capture, focus loss and resize cancel it. The demo interpolates stamps along the
drag. History holds at most 32 commands and a new edit clears redo.

Entrance width is 4–10 metres in 2-metre steps, ceiling height 2.5–4.5 metres
(default 4.3), and depth 6–14 whole metres (default 10). **Apply entrance** in the
browser or **Apply brush / entrance** on Windows validates and prepares both the
mesh and hole footprint, then publishes them together. Undo/redo restores both.
The entrance controls own that footprint; brush edits affect surrounding cells.
Resizing preserves manual holes, including those temporarily covered by the cave.
Starting an edit stops movement and returns the explorer outside. Walking uses
the edited depth. Width/depth follow the source grid.

The current demo (2026-10-23) uses a single continuous indexed cave loft, with
approximately 20,000 base triangles at the default 6 m × 4.3 m × 10 m entrance settings.
`examples/hybrid_terrain/cave_loft.inc` creates one connected surface from the
exact grid-sampled terrain perimeter through a collar and arched mouth, along
varying rounded chambers and connecting passages, to a closed rear dome. Shared circumferential
rings define the floor, sides and ceiling together. The centre floor remains
flat for walking; cross-section width and rock contours vary along the tunnel.
The separate overhang sample still uses its existing authored geometry.

The enlarged system spans about 54 m along z including the approach; the curved walking
route is longer. Chamber centres alternate from x=0 to x=−6 m and x=+6 m.
Three connected chambers have different sizes and floor elevations:

| Chamber | Nominal width / height | Floor elevation | Centre z (default) |
| --- | --- | --- | --- |
| Entrance cave | 6 m / 4.3 m | 0 m | 6 m |
| Grand chamber (x=−6 m) | 10.8 m / 6.5 m | −3 m | 21 m |
| Lower grotto (x=+6 m) | 14.2 m / 10.3 m | −8 m | 42.46 m |

Two descending passages follow a smooth lateral centreline: first left, then
right. Their nominal narrow sections are 3 m wide and 2.5 m high (2.2 m high
at the minimum entrance ceiling), clearly smaller than all three chambers.
The chamber rings share this centreline and remain one indexed surface. Motor
steering follows its tangent with bounded lateral correction, in either direction.
Cameras and lights use the same route offsets. Older straight layouts retain
straight steering based on their saved base geometry. See
[curved layout validation](hybrid-terrain-curves-validation.md). Entrance width scales
room widths, while entrance depth shifts the deeper rooms. The grand chamber's
full-width section is 6 m long; the grotto is now 12.93 m long; curved transitions extend each
room into its passages. One shared indexed surface supplies all three chambers
and both tunnels. Ordinary static mesh ground continues the plateau past the
heightfield tile, and two point lights illuminate the deeper rooms. This ground
extension is authored mesh, outside heightfield brush/query ownership.

**Grand chamber**, **Lower grotto**, and **Walk all caves** expose the new views
and full motor route on both hosts. **Walk outside** climbs back out. Camera
flight uses the existing controls. See [system validation](hybrid-terrain-system-validation.md).

The source fits the existing 4,096-triangle seam input limit. Preparation uses
`maxEdgeLength=1.25`, a 32-million work budget and deferred LOD generation.
`sculpt.hpp` then performs up to two uniform subdivision passes with shared edge
midpoints, reserving room within the existing 30,000-triangle limit for local formations, the pond and the grotto basin. It adds small
continuous rock displacement, preserves the floor and fully blended terrain rim,
recomputes normals across coincident positions, welds matching attributes, and
regenerates the existing three constrained render LOD levels. One limestone
material supplies periodic 256×256 albedo and packed normal/height textures.
Full-detail base geometry supplies collision independently of render LOD.

Entrance/hole edits rebuild this loft. Checkpoints retain their exact geometry
and regenerate LOD normally; older checkpoints keep their older cave until an
entrance or hole edit rebuilds it. Archive formats and engine defaults are
unchanged. This is procedural demo geometry, not an interactive freeform sculpt
or Boolean/voxel editing API. See [continuous cave validation](hybrid-terrain-loft-validation.md).

**Seam close-up** shows the join;
**Toggle material blend** selects the original mesh layer for comparison without
changing geometry, collision or the normal transition. This is a material-only
comparison, not an unwelded-geometry view.
**Toggle detail** switches full/coarse camera geometry while keeping shadows full.
**Distance / auto LOD** moves the camera farther away and enables automatic camera
and shadow selection. Returning to a near view retains automatic selection.
The shared renderer uses projected size and hysteresis; collision, hole picking,
editing and motor movement are independent of the selected detail. Camera and
shadow policies remain independently configurable with `MeshLodPolicy`.

**Save checkpoint / Load checkpoint** persists and replaces the live scene.
Browser storage uses the origin-local key `webengine.hybrid.checkpoint.v5`, with a
4 MiB decoded save cap. Native Windows atomically saves
`build/hybrid-terrain-checkpoint.wescene` relative to the launch directory (smoke
runs use their evidence directory). A checkpoint is an ordinary v36 scene file
with application-owned entrance dimensions, manual-hole ownership, view/detail,
blend mode and brush radius in `applicationData`. The application envelope is
`HYBRID11` for the mesh-shaped contact rim and enlarged grotto (views 0–11);
`HYBRID10` retains the earlier grotto with a rectangular contact rim;
`HYBRID9` cliff, `HYBRID8` pond and `HYBRID7` curved systems, `HYBRID6` straight systems and
`HYBRID5` single-cave files remain readable. The saved base bounds distinguish
curved versus straight route behavior, independently of render LOD. The origin-local storage key stays unchanged to retain existing saves.
The loaded mesh establishes whether chamber navigation is available. Older scenes
receive the optional extension/lights in the staged document and keep their saved
cave geometry until an entrance change rebuilds it. The scene retains camera pose,
explorer pose, materials, base geometry and collision. No engine policy owns those
demo settings. Loading validates a separate document, rebinds saved node identities
and host views, activates it, then releases the old scene. Movement commands,
gestures, tool mode and undo/redo history reset; subsequent editing remains enabled.
A failed load retains the live scene. Browser page reload can reopen the checkpoint;
no network, cloud sync or automatic save occurs. **Check scene save/load** remains
a separate in-memory integrity check.

Use the repository-local Emscripten version from `.emscripten-version`:

```powershell
./.tools/emsdk/upstream/emscripten/emcmake.exe cmake -S . -B build/web-release -G Ninja -DCMAKE_BUILD_TYPE=Release -DWEBENGINE_BUILD_HYBRID_TERRAIN_DEMO=ON
cmake --build build/web-release --target hybrid_terrain_demo webengine -j 6
python scripts/serve.py --directory build/web-release/hybrid-site --port 8157
# http://localhost:8157/?backend=webgpu or ?backend=webgl2

cmake -S . -B build/windows -G "Visual Studio 18 2026" -A x64 -DWEBENGINE_BUILD_WINDOWS=ON -DBUILD_TESTING=ON -DWEBENGINE_BUILD_HYBRID_TERRAIN_DEMO=ON
cmake --build build/windows --config Release --target webengine_hybrid_terrain_windows hybrid_terrain_tests terrain_seam_tests terrain_hole_lod_tests --parallel 6
./build/windows/Release/webengine_hybrid_terrain_windows.exe --backend dawn
# --backend d3d12 selects the direct renderer.
```

For native screenshots and traversal add `--smoke build/evidence/hybrid-terrain/dawn`
(use a separate directory for direct D3D12). Run CTest `terrain_hole_lod`, `terrain_seam`, `hybrid_terrain`, `terrain`,
`terrain_paging`, `terrain_page_edit`, `scene_archive`, `terrain_lod_archive` and `render_frame` after
building those targets. Browser `scripts/hybrid-terrain-smoke.cjs` uses `HYBRID_URL`,
`BROWSER_EXECUTABLE`, `NODE_PATH` (Playwright/pngjs), and optional `EVIDENCE_DIR`.
It exercises actual buttons, motor traversal, pixels, in-memory archive validation,
resize and intentional renderer recovery on both forced backends.

See [milestone 1 evidence](hybrid-terrain-validation.md) and
[milestone 2 evidence](hybrid-terrain-editing-validation.md) and
[milestone 3 evidence](hybrid-terrain-seams-validation.md) and
[milestone 4 evidence](hybrid-terrain-lod-validation.md) and
[milestone 5 save/load evidence](hybrid-terrain-save-load-validation.md) and
[sculpted cave validation](hybrid-terrain-sculpt-validation.md). Future work includes
arbitrary mesh intersections, geomorphing and painted multilayer transitions,
provider hole paging and nondestructive mesh modifiers. Runtime terrain destruction
and Nanite-style rendering are not delivered by this milestone.

## Enlarged grotto and basin (2026-10-23)

The last room has roughly ten times its previous volume: width, height and full
room length each scale by cbrt(10), about 2.1544. The floor remains −8 m, while
the nominal ceiling rises to +2.34 m beneath the plateau at +5 m. The preceding
passage retains its narrow throat and widens into the room. The rear dome extends
to z=51.19 m at default entrance depth. The complete walking route now finishes
at z=46.77 m, and **Walk outside** follows the curved route back out.

A dry, irregular rock basin is sculpted into the same connected floor beside the
route. At defaults its centre is (9.15, −8, 42.46), its outline is approximately
2.7 m × 3.38 m, and its bed reaches −8.94 m. A .4 m smooth bank and local
.31 m refinement preserve the bowl; its footprint scales with entrance width.
The central walking floor stays at −8 m. **Grotto basin** gives a close-up;
**Lower grotto** frames the enlarged chamber. The separate Grand Chamber pond
retains its existing water. The lower basin is rock geometry without water.

Existing immutable connected-floor preparation generates the bowl; the demo
publishes only its terrain result. Full base collision follows the depression,
seam/shoreline LOD locks survive, and ordinary checkpoints keep exact geometry.
HYBRID10 adds the dedicated basin view; HYBRID11 adds the shaped entrance rim.
Both retain previous envelopes.
Older saved caves retain their original geometry until an edit rebuilds them.

Entrance hole ownership now comes from the source mesh's terrain-contact rim,
using [mesh-shaped cuts](terrain-mesh-holes.md), before seam preparation. Painting,
resizing and history combine this footprint with independently retained manual
holes and the cliff footprint. Inset corner transitions are vertices of the
authored rim, retaining four corner cells rather than cutting its whole bounding
rectangle. Apply entrance also upgrades a legacy rim when dimensions are unchanged.
Seam scans follow every derived boundary edge, with probes on both sides.
See [validation](hybrid-terrain-grotto-validation.md).

## Joined open-path cliffs

The shared demo includes a 5 m, 85-degree limestone cliff beside the caves.
**Cliff / terrain join** opens its close view, and **Toggle material blend** now
applies to both the cliff and cave. `buildTerrainMeshCliff` returns a paired
heightfield cut and static mesh with exact grid perimeter geometry/normals,
ground/rock blending, protected LOD and full-base collision. Open ends taper back
into the source. Mesh-owned cells survive cave edits, brush restore and checkpoints.
See [cliff contracts](terrain-cliffs.md) and [evidence](terrain-cliffs-validation.md).
