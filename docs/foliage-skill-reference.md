# Foliage integration

Verify the consumer engine revision before using `engine/foliage.hpp` and the
optional `WebEngine::Foliage` target (`WEBENGINE_BUILD_FOLIAGE=ON`). This is a local
engine extension, not a dependency-pin update. Read engine-root `docs/foliage.md`
for the complete contracts, `docs/foliage-validation.md` for M1–5 evidence and
`docs/foliage-dense-validation.md` for M6 checks and measured limits.

Publish immutable `FoliageCatalog::create(FoliageDefinition)` data. Species use
stable nonzero IDs, shared static meshes/materials, optional per-detail root-to-tip
bend weights, density/minimum spacing, height/slope/surface filters, scale, ground
alignment, tint and draw/shadow distances. Trees can opt into simple scaled trunk
capsules; grass defaults to no collision. Supply opaque trunks and masked,
double-sided cards with ordinary engine materials. The engine provides no art.

`generateFoliageCell` uses globally anchored seeded candidate lattices, including
negative coordinates. Full species/lattice IDs survive cell unload and render
detail changes. Spacing is per species across cell boundaries; realized density
can be lower because spacing, source filters and exclusions reject candidates.
Game biome composition and cross-species exclusion policy remain application-owned.

`FoliageSampler` synchronously returns optional height, normal and mask for world
X/Z. Null means absent/hole. It must query authoritative source surfaces, without
camera/LOD dependence or scene mutation. `foliageTerrainSampler` owns an immutable
TerrainAsset and supports translation/hole-aware full-source queries. Custom
samplers supply biome masks, mesh ground or other coordinate policies.

Construct an application-owned `FoliageStream` borrowing a longer-lived Scene.
Scene transform must be identity. At an idle pre-simulation boundary call
`update(visualFocusXZ, gameplayFocusXZ)`; these have separate visual/collision
residency, radius, hysteresis and hard limits. Incremental cell admissions are
bounded and prioritize collision. Gate gameplay movement until required coverage
is ready; ordinary scene reconciliation owns physics registration. This library
does not create another scheduler or move characters.

Call `present(visualFocusXZ, accumulatedSimulationSeconds, wind)` after simulation.
Freeze that supplied timestamp during pause/suspension. Rendering defaults to
`FoliageRendering::GpuInstances`: one generic `MeshInstancesComponent` per
cell/species/detail, shared immutable source meshes and `MeshBend` sidecars, plus
one 32-byte explicit-time `MeshWindState` per stream. Wind-only ticks preserve
placement matrices and object uploads. WebGPU/Dawn, WebGL2 and direct D3D12 have
matching color/shadow wind shaders. Maximum-amplitude bounds keep CPU culling
conservative; shaders correct normals/tangents from authored weight gradients.
Source instance records are 64 bytes with cached matrices; CPU draw descriptors
still expand per plant. GPU object slots remain 256 bytes; WebGL packs the 176
attribute bytes to obey its 255-byte maximum stride. This is direct instancing,
not GPU culling or indirect drawing. Generic bulk sources are static opaque/masked,
without skinning, source morph targets, water, blending or projected decals.

Explicit `FoliageRendering::CpuMeshes` retains per-plant MorphPose CPU wind and
triangle-rebuilt normals/tangents for comparison. Rendering/visibility never
advances wind or changes gameplay collision. Distance details have hysteresis.
Rigid draws retain mesh LOD; weighted draws use explicit foliage topology/details.
Preserve source lattice parameters and IDs across presentation quality choices.
There is no impostor generation, interactive bending or million-blade throughput
guarantee. Profile the actual consumer/device.

Scene owns generated archive-transient root actors. Retain handles rather than raw
projection pointers, and do not manually mutate/reparent the generated subtree.
`clear()` removes projection and preserves removals. The stream destructor releases
its own resources only; explicitly clear before discarding a stream when its scene
continues to live. Methods require a live scene and idle hierarchy. No worker,
async callback or late cell completion is retained by this implementation.

`replaceSource` and `setRemoved` stage complete resident replacements before
publishing; preparation/adoption failures preserve the old projection. Source
changes are explicit, without an automatic terrain subscription. These whole-set
operations may cost more than incremental update's admission limit.

Map a bulk pick via `placement(hit.surface, hit.instanceKey)`, then call `remove(id)` under
game policy. Save `saveRemoved()` in the game's versioned envelope and restore
with `loadRemoved()`. WEFO 1 carries world/seed/full sorted removal IDs and rejects
unknown versions/content/species, duplicate IDs and malformed/budget-exceeding data.
Storage, regrowth/rewards, destruction state and migration remain game-owned.
Bump the game-owned world identity when lattice/species policy changes; matching
world/seed is compatibility rather than a content hash. Do not serialize runtime
scene handles or local instance keys as durable harvest state. Bulk keys are
ephemeral and scoped by component generation; component-only lookup remains for
the explicit CPU mode and returns no ID for ambiguous bulk components.

Validation/build flags: `WEBENGINE_BUILD_FOLIAGE_TESTS=ON` creates `foliage_tests`
and `foliage_contracts`; `WEBENGINE_BUILD_FOLIAGE_DEMO=ON` creates browser
`foliage_demo` or Windows `foliage_demo_windows`. Use engine-root README setup
commands and repository-local Emscripten version; native/browser build trees are
separate. Browser smoke: `node scripts/foliage-browser-smoke.cjs` with FOLIAGE_URL,
NODE_PATH and BROWSER_EXECUTABLE. Native smoke: `foliage_demo_windows.exe --d3d12
--smoke output-directory`. Shared sample policy/art lives in `examples/foliage`.
The fixed-seed 10,000-clump comparison is `scripts/foliage-dense-smoke.cjs`;
native uses `--benchmark --smoke output-directory`, plus `--cpu` for the reference.
Record native and browser backend results separately; do not substitute portable
tests for rendered acceptance or desktop emulation for physical mobile evidence.
