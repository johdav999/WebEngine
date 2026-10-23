# Mesh-shaped heightfield holes

`engine_core` exposes `buildTerrainMeshHoleMask` and `cutTerrainHolesFromMesh`
in `engine/terrain_mesh.hpp`. Both are portable synchronous CPU operations used
by the browser and Windows cave demo. They derive a cell mask from closed mesh
boundary loops that touch the **uncut source heightfield**. Buried chamber roofs
do not cut the terrain above them merely because they overlap in XZ projection.

```cpp
engine::TerrainMeshHoleSettings settings;
settings.meshToTerrain = engine::inverseAffine(terrainActor.worldMatrix())
                       * entranceActor.worldMatrix();
auto edit = engine::cutTerrainHolesFromMesh(*terrain, *entranceMesh, settings);
engine::applyTerrainEdit(terrainActor, edit); // idle scene boundary
terrain = edit.after;
```

Supply the authored entrance mesh, with its outer rim resting on terrain source
samples. Prepare this cut **before** building the terrain seam, sculpting or
generating render LOD. A prepared render mesh can contain extra open contact edges
from material/seam refinement and need not satisfy the authoring-rim contract.
The demo uses `caveMesh` for this operation and then prepares the final cave from
the cut terrain. It no longer duplicates a hardcoded rectangular cut policy.
The current source rim has inset corner transitions at grid resolution. Its
default footprint cuts 74 cells, retaining four corners that the old 78-cell
rectangle removed. The same vertices define the mesh contact seam. HYBRID11
checkpoints retain this ownership; older envelopes keep their original rim/mask
until an entrance or hole edit rebuilds them.

The transform maps mesh-local metres to heightfield-local metres. It must be
finite, affine and nonsingular. Boundary endpoints and midpoints must lie within
the source bounds and within `contactTolerance` of the interpolated uncut source
height (default .05 m, allowed 0–1 m). Existing holes never erase the underlying
source samples used for this check. Full-source assets, including lazy/source-only
assets, work; terrain patches reject.

Exact coincident positions across attribute splits and collinear boundary
T-junctions are reconciled. Local projected surface coverage discards internal
contact edges. The remaining boundary must form closed loops without projected
crossings. Concave outlines, disjoint openings and nested islands use even/odd
filling. A closed mesh or wholly buried boundary produces no additional cut;
partial contact chains and malformed boundaries reject.

Rasterization conservatively cuts cells with positive-area outline intersection.
A cell touched only at its corner stays solid. Coordinates within 1e-5 cell of a
gridline snap to it to avoid floating-point transform slivers. Holes retain the
heightfield's cell resolution: this is not an exact curved Boolean edge, voxel
excavation or arbitrary closed-volume intersection. Author a matching collar or
use a sufficiently fine source grid for the desired entrance silhouette.

`buildTerrainMeshHoleMask` returns full row-major `holes` and `footprint` masks,
plus loop, footprint-cell and newly-cut-cell counts. `holes` unions existing cuts;
`footprint` records mesh ownership alone. Applications replacing an entrance can
combine the new footprint with their independently retained painted holes, so
resizing restores old entrance cells without destroying user edits. The convenience
function returns `TerrainAsset::withHoles(result.holes)` and preserves existing
cuts. Neither function publishes scene state or replaces a mesh collider.

Sources must be finite static triangle meshes, at most 30,000 triangles and
90,000 vertices; skinning and morph targets reject. Default budgets are 2,048
boundary edges, 262,144 cells and 16 million work steps; hard caps are 8,192 edges,
4,194,304 cells and 64 million steps. Invalid indices, collapsed rim edges,
invalid transforms and exceeded budgets reject without mutating either input.
Only full base triangles participate; camera/shadow LOD never changes cuts or
collision. Publishing the returned terrain edit uses existing terrain render,
query, full/chunk collision, history and archive contracts.

Build/run `terrain_mesh_holes_tests` natively and its Node/Wasm target with the
repository SDK. Coverage includes concavity, conservative cells, islands,
disjoint openings, ownership, transforms, attribute splits, T-junctions, sloping
nonuniform terrain, source-only data, patch rejection, query/collision removal and
atomic invalid-input rejection. `scripts/hybrid-entrance-shape-smoke.cjs` checks
retained corner ground/collision versus the open mouth, resizing, repeated
storage load, same-dimension upgrade of older saves and underwater rendering
on both browser backends. See [hybrid terrain](hybrid-terrain.md) and
[current demo validation](hybrid-terrain-grotto-validation.md).
