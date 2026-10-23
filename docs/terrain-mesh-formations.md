# Terrain mesh formations

`engine_core` provides `buildTerrainMeshFormations` in `engine/terrain_mesh.hpp`.
It prepares stalagmites, stalactites and similarly tapered protrusions directly
from an existing static mesh surface. A formation refines a connected attachment
patch, inserts its tip sample and extrudes the shared surface with a rounded tip
and a smooth, broad base. It does not add an overlapping cone or a detached mesh.
Direction points into free space: upward for floors and downward for ceilings;
other surface orientations work through the same operation.

```cpp
#include "engine/terrain_mesh.hpp"

engine::TerrainMeshFormation growth;
growth.anchor = floorHitInMeshLocalSpace;
growth.direction = {0, 1, 0};
growth.length = 1.4f;
growth.baseRadius = .65f;
growth.bend = {.08f, 0, 0};
growth.seed = 17;
auto result = engine::buildTerrainMeshFormations(mesh, {&growth, 1});
scene.adoptChild(engine::makeMeshTerrainActor("Cave", result.mesh, true));
```

Use a point on an oriented source triangle, in mesh-local metres; scene physics
hits need conversion when the actor is transformed. The direction must face out
of the triangle. Length is .02–100 m, radius .02–50 m, bend is tangential and no
longer than half the radius, and irregularity is 0–.2 (default .08). A seed controls
repeatable subtle shape variation; the engine owns no random placement policy.
The caller must choose sufficient free space and preserve walking/head clearance.

The builder processes at most 32 placements. Same-facing footprints cannot
overlap. Only the connected, appropriately facing surface around an anchor is
selected, so a nearby disconnected floor/roof is not also extruded. A footprint
must not cross an open edge or an existing protected LOD vertex. Skinning and
morph targets reject. Material slots/parts stay intact, UVs interpolate during
refinement, and coincident attribute splits share refinement positions and smooth
formation normals. Conventional UV textures stretch with extrusion; triplanar
stone materials maintain world-scale texture detail without an atlas change.

Default edge length is radius/5; an explicit length must be positive and at most
radius/2. Refinement splits shared geometric edges on both incident faces, keeping
the surface continuous even across UV/material splits. Near-edge anchors split
both incident faces; very close vertices move to the exact anchor with locally
interpolated attributes. This avoids thin tip triangles and unstable collision rays. Output is capped at 30,000
triangles, 90,000 vertices and 64 parts. `maxOutputTriangles` can lower the triangle
cap. `maxWork` defaults to 8 million bounded preparation steps (maximum 64 million);
refinement has a 12-pass limit. Invalid input, excessive work and exhausted geometry
budgets throw before returning a replacement. The source remains unchanged.

The result reports the prepared immutable `mesh`, formation count, inserted vertex
count and output triangle count. Existing derived geometry is discarded. Default
`lodLevels=3` regenerates constrained LOD, preserving inherited explicit locks and
formation tips; 0 disables LOD and 1–3 chooses the requested maximum. Constraints
can prevent a strictly reducing candidate. Original protected normals are retained.
Tangents and bounds are rebuilt after geometry/normal reconstruction.

Publish the resulting ordinary mesh and full-base collider together at an idle
scene boundary. `makeMeshTerrainActor` and ordinary mesh/collider replacement
already support it; render LOD never changes formation collision. Existing v36
archives save the base geometry and LOD locks and regenerate derived levels on
load. No new archive version or custom component is needed. Procedural placement
descriptors are caller-owned; save them in application metadata if later parameter
editing is needed. Saving geometry alone preserves the exact baked formations.

The shared browser/Windows cave demo uses two floor formations and two ceiling
formations. `examples/hybrid_terrain/formations.hpp` owns placement and samples
the actual sculpted first-chamber surfaces. `sculpt.hpp` calls the reusable builder
after refinement/rock shaping and before final LOD/collision publication. Entrance
and hole edits rebuild them; undo/redo and checkpoint loading preserve the resulting
mesh and collision. The centre walking lane stays clear. Older checkpoints retain
their saved geometry until an entrance/hole edit rebuilds the cave.

This is a bounded CPU surface operation, not Boolean union, a volumetric sculpting
editor, procedural scattering, runtime growth, or a guarantee against intersection
with other scene geometry. Placement and authoring UI remain application policy.
See [formation validation](hybrid-terrain-formations-validation.md).
