# Cloth milestone four: triangle surfaces and swept contact

`WebEngine::Clothing` extends [M3 collision](cloth-collision.md) with optional
vertex/triangle self-contact, explicit environment triangles and bounded linear
sweeps. The portable CPU implementation and shared demo run through the existing
browser/Windows dynamic vertex paths. No new physics dependency or frame owner.

## Authoring and integration

`ClothDefinition::surfaceTriangles` holds indexed garment faces (up to 2048).
Indices must reference the solver's rest vertices; faces must be nondegenerate and
unique, including reversed duplicates. A nonempty surface requires a positive
`selfCollisionDistance`. For ClothingItem, each face must also occur in the garment's
mesh indices (winding may differ). Author shared/welded vertices and copy the actual
mesh topology; the solver does not infer sewing or weld duplicated import vertices.

```cpp
definition.selfCollisionDistance=.05f;
definition.sweptCollision=true;
for (std::size_t i=0;i<mesh.indices.size();i+=3)
    definition.surfaceTriangles.push_back({mesh.indices[i],mesh.indices[i+1],mesh.indices[i+2]});

std::array<engine::ClothTriangle,1> ramp={
    engine::ClothTriangle{{-1,0,-1},{1,0,-1},{0,1,1}}
};
outfit.advanceCloth(scene,simulationDelta,bodyCapsules,{0,-9.81f,wind},
                    {worldPlanes,worldBoxes,ramp});
```

`ClothEnvironment::triangles` is a trailing borrowed span, preserving old calls.
It accepts up to 256 two-sided world-space triangles per update. Coordinates must
be finite within +/-1e6; squared cross-product area must be at least 1e-12. Supplied
triangles are thin surfaces, not closed-solid volume tests: a particle initially
deep inside a triangle shell is not automatically expelled. Apps transform collision
geometry to world space and own its lifetime, selection and movement snapshots.
No scene mesh, render LOD or Jolt body is queried or retained by the solver.

Empty surface topology and `sweptCollision=false` are defaults. M3 particle
self-contact still works without surface topology. With topology, any particle
belonging to a face or directly linked/rest-near to one of its vertices is excluded
from contact with that face. Triangle edges also establish direct-neighbor exclusions.
This avoids local constraints fighting the surface thickness.

## Response and sweeps

Closest-point triangle queries include face interiors and edges. Self-contact uses
barycentric weights to distribute separation over the particle and face vertices
according to their inverse masses; pinned vertices remain fixed. Collapsed runtime
faces fall back to their closest edges instead of dividing by zero. The same unit
particle mass, tether, iteration and fixed-step contracts from M2 apply.

When `sweptCollision` is enabled, each solver iteration checks linear motion from
the start of the current 1/120-second substep to the candidate positions:

- Vertex/triangle self-contact includes linear movement of all triangle vertices.
- Environment triangles and capsules are fixed at the supplied snapshot during the
  call. Distance-based conservative advancement uses a relative-speed bound, up to
  64 queries and a 1e-5 distance tolerance. Exhaustion retains a conservative contact
  plane at the last checked position, which can cause early/false contacts.
- Expanded AABB boxes use slab entry tests. Starting inside still uses M3's nearest
  face projection. Infinite planes retain their half-space endpoint projection.

Contact response projects the candidate onto the detected contact plane; self faces
receive the barycentric counterpart. Environment contact uses existing friction
and velocity limiting. This preserves tangential movement but can over-constrain
motion around edges or rapidly deforming faces. Sweeps include predicted motion
and iterative corrections relative to the substep start; they do not reconstruct
curved motion between animation samples or collider movement between calls.

Order: links, tethers, particle self-contact, surface self-contact, capsules, boxes,
environment triangles, planes. Later constraints can disturb earlier contacts;
overlaps/conflicting constraints may not converge within the iteration budget.
All input and candidate simulation/render streams stage before publication.
Zero simulation delta, inactive characters, resets, ownership and outfit persistence
remain unchanged. Reset rebinds without collision until a later simulated step.

M4 is **vertex/triangle contact**, not complete mesh/mesh CCD. [M5](cloth-edges.md)
adds optional edge/edge self-contact and linear edge sweeps. Inter-garment collisions
and swept particle-pair contacts remain unimplemented. Fast
moving external colliders, nonlinear motion, finite iterations and floating-point
conditioning mean this is not a universal nonintersection guarantee. Tearing,
fitting, GPU simulation and two-way body coupling remain outside this milestone.

Broad-phase swept bounds reject distant vertex/face pairs. Worst-case work still
scales with particles times faces times iterations/substeps, with bounded searches
for nearby pairs. Use small collision meshes; 1024 vertices/2048 faces are safety
limits, not a dense-character performance target. No visual LOD controls simulation.

`surfaceContacts` counts applied self/environment triangle projections;
`sweptContacts` counts applied sweep projections (including initial contact).
They are cumulative diagnostics, not unique impacts, and are aggregated by
`clothStats(scene)`. Reset retains counters; re-equipping creates new state.

## Demo and validation

Both host examples enable the cape's actual surface topology and swept contact.
The visible block now supplies its transformed mesh triangles rather than an AABB
proxy; browser diagnostics and native smoke require surface contact. Existing
build options and commands in [clothing.md](clothing.md#build-and-validate) are unchanged.
Run `clothing_contracts`, the WebGPU/WebGL2 smoke script, and native Dawn/direct
D3D12 smoke separately. See [M4 evidence](cloth-surfaces-validation.md).
