# Ship instancing investigation

Investigated on 2026-10-10 against the current engine and bundled Knarr asset.

## Confirmed cause

The fleet shares one immutable mesh and its materials/textures. The imported ship has seven material/index ranges and 40,956 triangles. All corresponding parts across ships have identical mesh, index range, effective material binding, texture, winding, skin and morph state. All seven ranges enter the opaque rendering path and cast shadows.

`Scene::collect` appends all seven parts of ship 1, then all seven of ship 2, and so on. `RenderFrame::collect` only merges the immediately preceding batch and requires contiguous original object slots. Consequently, compatible ranges across different ships cannot combine. `sameMaterialBinding` is already suitable: per-instance tint and opacity are excluded from binding identity.

## Verified counts

A native probe loaded the actual FBX through the engine importer and used `ShipStressDemo` and `RenderFrame`, with no frustum and all ships visible:

| Ships | Current color draws | Current draws per shadow map | Compatible groups | Possible instances per group |
|---:|---:|---:|---:|---:|
| 1 | 7 | 7 | 7 | 1 |
| 100 | 700 | 700 | 7 | 100 |
| 1,000 | 7,000 | 7,000 | 7 | 1,000 |

The screenshots' 1,403 and 14,003 total draws are consistent with seven ship color draws and seven ship shadow draws per ship, plus three other draws. Ship geometry alone accounts for 8,191,200 and 81,912,000 triangles with one shadow pass. Instancing would retain these triangle counts.

The probe verified every draw against the equivalent part of the first ship, including material compatibility and opaque/shadow participation. Warm object changes were zero at every fleet count. The existing `instancing` and `ship_stress` tests both passed after rebuilding the current native engine. Probe source and raw results are in `build/evidence/ship-instancing/` (ignored build artifacts).

## Recommended implementation

Add explicit permission to batch opaque components whose draw order may change, and enable it for the fleet. The current general renderer deliberately preserves traversal order, including equal-depth precedence; tests assert this behavior. A global material sort would change that contract.

Group eligible draws by layer, shared mesh, index range, effective material binding, texture, mirrored winding, skin pose and morph pose. Keep grouping within reorderable runs, with order-sensitive draws acting as boundaries. Transparent/additive mesh submission retains its camera-sorted FX path.

Use a reusable mapping from GPU instance slots to original scene draw indices, with contiguous grouped object staging. A batch needs its source draw for mesh/material lookup and its GPU first-instance address separately: the current single `firstInstance` field serves both purposes. Update camera and shadow submissions, dirty-range uploads, visibility splitting and any transparent draws using original slots consistently. Retain off-camera shadow casters and independent cast/receive flags.

Expected fleet submission with every ship visible is seven color draws plus seven draws per shadow map, independent of fleet size. This is a submission target, not a measured timing result. Geometry, shading and shadow rasterization work remain.

Validation should cover actual multi-material fleets at 100 and 1,000 ships; transforms/tints and mirrored state; material overrides; culled ships and off-camera casters; shadow participation; order-sensitive and transparent boundaries; changed-only uploads and zero warm allocations. Compare rendered fleet pixels with batching disabled, then measure a stable Release run on the same browser/GPU, viewport, camera and lighting as the baseline.

## Implemented fix

The fleet now sets `MeshComponent::allowInstanceReordering`, an opt-in runtime
hint that defaults to false for other components and after archive loading.
`RenderFrame` sorts compatible parts within contiguous opted-in opaque runs and
keeps a reusable GPU-slot-to-scene-index mapping. Object staging and camera/shadow
resource lookup use that mapping; transparent slots keep their original indices.

Native coverage verifies actual Knarr fleets, material and mirrored boundaries,
per-instance transforms/tints, shadow participation, unchanged uploads and zero
warm allocations. All 26 native suites passed after a clean rebuild to remove
stale objects from concurrent mesh-layout changes. Release WebGPU comparisons
at 100 and 1,000 ships gave 10 color-only or 17 shadow-enabled total scene draws,
with exact matching pixels and triangle counts versus individual submissions.
Browser evidence is saved under `build/evidence/ship-instancing-browser/` and
`build/evidence/ship-instancing-browser-combined/`. The combined build comparison
also passed with animation explicitly paused and geometry/shadow detail fixed.
