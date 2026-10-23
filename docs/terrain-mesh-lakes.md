# Terrain mesh lakes and cave ponds

`buildTerrainMeshLake` in `engine/terrain_mesh.hpp` prepares a basin in an existing
static terrain mesh, a fitted non-solid water surface, and a finite `WaterRegion`.
It is shared CPU functionality in `engine_core`, used by browser and Windows hosts.

```cpp
engine::TerrainMeshLakeSettings settings;
settings.floorAnchor = {0, 0, 0};
settings.outline = {{-1,0}, {0,-1}, {1,0}, {0,1}};
settings.waterLevel = -.08f;
settings.depth = .5f;
settings.bankWidth = .3f;
auto lake = engine::buildTerrainMeshLake(sourceMesh, settings);

scene.adoptChild(engine::makeMeshTerrainActor("Basin", lake.terrainMesh, true));
auto water = engine::makeMeshTerrainActor("Pond", lake.waterMesh);
auto* surface = water->getComponent<engine::MeshComponent>();
surface->transform.position.y = lake.waterLevel;
engine::setWaterRegion(*surface, lake.region); // engine/water_region.hpp
scene.adoptChild(std::move(water));
```

All coordinates are terrain mesh-local metres. The source must be an oriented,
static triangle mesh. `floorAnchor` selects the connected upward-facing floor;
nearby roofs or disconnected floors are excluded. `outline` is a strictly convex,
counterclockwise XZ polygon with 3–16 vertices containing the anchor. Irregular
convex outlines are supported; concave outlines, crossings and holes reject.
The water level lies at or below the anchor (within one bank width). Depth is the
maximum depression below the water level, .02–100 m; bank width is .05–10 m.
The mesh is lowered smoothly through the shoreline into the lakebed, blending
back into the source outside the bank band. Existing material slots and UVs are
retained; conventional UV maps stretch, while triplanar stone keeps texture scale.

Conforming shared-edge refinement defaults to .12 m edges; an explicit length is
.02 m through bank width. Exact face/edge/vertex anchors use the same stable local
remeshing as formations. Existing protected vertices cannot overlap the basin or
bank. Their original attributes survive tangent reconstruction. The shoreline
band and basin anchor are locked for LOD; up to three strictly reducing levels are
derived, with zero disabling LOD. Constraints can yield fewer candidates.

Defaults cap the source/result at 30,000 triangles, 90,000 vertices and 64 parts,
and the separate water surface at 8,192 triangles. Preparation has an 8-million
step budget (maximum 64 million) and bounded refinement depth. Invalid source,
outlines, settings, protected overlap or exhausted budgets reject without changing
the input. The water mesh is clipped to the actual submerged bed and the authored
outline, then projected into its local Y=0 plane. Its component needs translation
by `waterLevel` alongside the same terrain transform. No solid water collider is
created; physical movement uses the full-detail basin, independently of render LOD.

An optional caller-supplied `waterMaterial` must use `MaterialShader::Water`.
Otherwise the builder provides a calm water material with zero geometric wave
amplitude. The cave pond uses gentle normal ripples, refraction, subdued planar
reflection, underwater absorption/fog and caustics. Applications own material
style, simulation clocks, placement, clearance and any wading/swimming behavior.

## Finite water regions

`WaterRegion` in `engine/water.hpp` describes a convex XZ prism, with the mean
surface at local Y=0 and a finite negative `bottom`. `validate`, `contains` and
inward half-plane queries provide portable numeric data. Attach a validated region
to the water **MeshComponent itself** through `setWaterRegion` in
`engine/water_region.hpp`; `waterRegion` reads it and `std::nullopt` removes it.
The setter stages common component state and preserves unrelated tags/resources.

The reserved `WebEngine.WaterRegion.v1:` annotation uses bounded hexadecimal
float-bit fields in ordinary component tags. Existing archive/clone/history paths
retain it without a new scene format or custom component codec. Malformed,
duplicate or unknown reserved annotations reject when read; they never fall back
to unbounded optics. Without an annotation, existing rectangular bottomless water
retains its original behavior.

`underwaterView` checks the finite polygon and bottom as well as visibility,
transforms, surface bounds and waterline epsilon. `underwaterDistance` clips ray
segments against the same planes. Browser WebGPU, WebGL2, native Dawn and direct
D3D12 use the same 16-plane bounded uniform data for underwater attenuation and
caustic receivers. Dry bounding-box corners and rooms below the region remain dry.
Nonuniformly scaled and rotated water retain local-space region coordinates.

The region is a finite convex prism, not an exact signed-distance model of the
sloping bed. Ground collision provides the detailed bed; the bottom cap prevents
optics extending into lower chambers. Overlapping regions retain first-match
selection, and planar reflections still capture one water plane per view.

## Cave demonstration

`examples/hybrid_terrain/pond.hpp` owns a small irregular pond beside the dry route
in the Grand Chamber. The level is -3.08 m, maximum depth .45 m, with a .28 m bank
and .16 m refinement edges (.27 m for large source meshes to retain the geometry budget). Both hosts expose **Cave pond** and **Underwater view**.
The terrain and fitted water surface rebuild together after entrance/hole edits;
snapshots restore both meshes, region and water level through undo/redo.
Checkpoints retain baked basin/collision and water metadata; loading does not
regenerate a different pond. Older checkpoints without ponds remain readable.

The builder owns no scene, storage adapter, gameplay policy, fluid simulation or
automatic subscriptions. Publish basin/collider/water/region together at an idle
scene boundary. Baked geometry and bounded metadata persist through existing
archives; save procedural settings separately if later parameter editing is needed.
See [lake validation](hybrid-terrain-lake-validation.md).
