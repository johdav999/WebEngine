# Open-path terrain cliffs

`engine_core` provides `buildTerrainMeshCliff` in `engine/terrain_cliff.hpp`.
It replaces a bounded strip of heightfield terrain with an ordinary static mesh,
including a steep rock face, upper/lower shoulders and tapered open ends. The
left side of the directed X/Z path is the high side. All values are in source
terrain-local metres. The first implementation supports steep faces, 60–89 degrees
from horizontal, with gentle bends; it does not create vertical overhangs.

```cpp
#include "engine/terrain_cliff.hpp"

std::array<engine::Vec2, 3> path{{{8, 16}, {16, 17}, {24, 16}}};
engine::TerrainCliffSettings settings;
settings.upperHeight = 5;
settings.lowerHeight = 0;
settings.faceAngle = 85;
auto built = engine::buildTerrainMeshCliff(*terrain, path, rock, settings);

// Prepare full-base physics before publishing either resource.
auto cliff = engine::makeMeshTerrainActor("Cliff", built.mesh, true,
    terrain->data().physicalSurface);
cliff->transform.position = terrainActor.transform.position;
// At an idle scene boundary, publish the paired edit and prepared mesh actor.
// The application owns rollback if its larger publication transaction fails.
engine::applyTerrainEdit(terrainActor, built.terrain);
scene.adoptChild(std::move(cliff));
```

Use matching translation and unit physical scale for the terrain and cliff. The
builder owns no actors, scene mutation, background work or retained callbacks.
Prepare all dependent resources before publication. Preserve `built.footprint`
separately from brush/cave hole ownership when later recomposing a hole mask.

## Continuous connection to regular terrain

The builder encloses the path and shoulders in a rectangular, grid-aligned collar.
Only cells inside that collar are removed. The replacement mesh keeps every
original perimeter vertex and edge, without splitting or skipping boundary
segments. Its boundary heights and vertex normals match the source heightfield.
There are no overlapping source ground triangles or hidden heightfield colliders
under the replacement. Unrelated source holes remain unchanged.

Interior edge splits are shared by both incident faces. Lip/toe guide edges are
inserted before refinement; a continuous signed-distance field handles path bends.
The prescribed drop and face angle determine the face's horizontal run. Open ends
fade into the original source over `endFade`; shoulders blend back to the source
over `shoulderWidth`. A coarse outer collar avoids subdividing the heightfield's
perimeter edges. `maxEdgeLength` bounds interior refinement, with relaxed edge
lengths in that outer collar. Very narrow bends and endpoint transitions are
approximated by the bounded tessellation.

Seeded relief perturbs only the face vertically and vanishes at the lip and toe.
Normals are reconstructed from the finished shared surface and transition to
source normals at the collar. Existing two-layer triplanar material rendering
blends grass/ground into rock: horizontal shoulders retain ground coverage,
steep faces receive rock, and the outer boundary is entirely source material.
The seam and cliff builders share the same source material-layer conversion.
World-scale triplanar textures avoid stretched rock UVs.

LOD recipes protect the boundary blend band and lip/toe neighbourhoods. Derived
levels may retain additional geometry when those locks prevent simplification.
Full base geometry always supplies collision, independently of camera/shadow LOD.
Use physics rays, sweeps and character motors for hybrid ground; existing terrain
height/ray queries intentionally omit the removed cells and remain heightfield-only.

## Inputs and limits

- A full eager or source-only `TerrainAsset`, up to 262,144 source cells by default.
  Provider patches reject. The cut rectangle contains at most 4,096 cells and must
  fit inside the source bounds.
- An open path with 2–16 finite points, coordinates within +/-1,000,000 m, segment
  length at least .25 m, adjacent turns below 60 degrees, and total length greater
  than twice `endFade`. Crossings and close folding approaches reject; extra forward path samples are allowed.
- Finite upper/lower elevations within +/-10,000 m and a drop of .1–100 m.
  Face angle defaults to 85 degrees; supported range is 60–89 degrees.
- Defaults: shoulder width 1.25 m, end fade .75 m, normal/material blend width
  .75 m, interior maximum edge length .5 m, relief .08 m and seed 17.
  Shoulder/end widths are .25–50 m; blend width is .1 m through shoulder width;
  edge length is .05–2 m; relief is 0–.2 m.
- Source and rock materials must be compatible opaque, nonmetallic, nonemissive
  PBR/unlit solid colors or unweighted single-layer triplanar materials, with
  matching shader families and triplanar sharpness where applicable. Conventional
  UV maps, painted multilayer weights and height-based blending reject.
- The collar may not overlap existing holes. Differing non-null per-cell physical
  surfaces inside it reject; the application supplies the common physical surface
  when creating its mesh actor. Unrelated holes/surfaces outside it remain intact.
- Output is capped at 30,000 triangles and 90,000 vertices. Work defaults to
  8 million preparation steps, maximum 64 million; refinement has 12 passes.
  `maxCells`, `maxOutputTriangles` and `maxWork` can lower their bounds.
  `lodLevels` defaults to 3; 0 disables derived levels, 1–3 requests that maximum.

Invalid input, geometric degeneracy or exhausted budgets reject before returning
a replacement. Source heights, holes and published meshes remain unchanged.
This is a bounded heightfield replacement, not an arbitrary mesh Boolean, closed
mesa tool, automatic residency system or nondestructive modifier stack.

## Persistence and demo

Ordinary scene archives retain baked base geometry, materials, colliders and LOD
locks, regenerating the derived LOD cache on load. No engine archive format change
is required. Applications that need parameter editing must save the original path,
settings and source separately; baked geometry alone preserves the visible result.

The shared browser/Windows hybrid terrain demo includes a 5 m, 85-degree limestone
cliff with a gently bent open path, beside the existing cave system. Select
**Cliff / terrain join** for a close view. Cave/brush edits preserve the cliff-owned
hole mask, and the restore brush skips its cells. Checkpoints save the ordinary
cliff component (`hybrid-cliff`) and recover its rectangular cut ownership from
its saved bounds. HYBRID9 adds the cliff; later combined demo versions retain it.
Older checkpoints remain readable with their saved geometry.

Build `terrain_cliff_tests`, `hybrid_terrain_demo` (browser) and
`webengine_hybrid_terrain_windows` (Windows). Run the portable tests on Windows
and Node/WebAssembly, `scripts/cliff-terrain-smoke.cjs` on forced WebGPU/WebGL2,
and the native demo with `--backend dawn|d3d12 --cliff-smoke <output-directory>`.
The focused native mode exercises the actual retained GUI view control, captures
full/coarse/restored frames, checks collar collision and LOD independence, replaces
the live scene from a checkpoint, and checks restore-brush footprint protection.
Keep the existing broader hybrid/cave/archive/seam regressions alongside these.

See [cliff validation](terrain-cliffs-validation.md).
