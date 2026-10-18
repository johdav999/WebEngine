# Terrain

`engine/terrain.hpp` provides optional portable heightfield terrain. An immutable
`TerrainAsset` owns float samples, one shared material and optional immutable chunk LOD chains.
`makeTerrainActor` prepares a detached actor with one `TerrainComponent` per chunk
and optional full-resolution static triangle collision. Adopt the completed actor
directly under a Scene when using physics.

## Create and query

```cpp
#include "engine/terrain.hpp"
#include "engine/scene.hpp"

engine::Scene scene;
engine::TerrainData data;
data.samplesX=65;data.samplesZ=65;
data.spacing={0.5f,0.5f}; // Metres between X/Z samples.
data.chunkCells=32;      // Cells, not vertices, along a chunk edge.
data.textureMeters=4;    // Metres per material texture repeat.
data.lodLevels=3;        // Up to three derived levels; zero disables generation.
data.heights.resize(65*65,0);
auto asset=engine::TerrainAsset::create(std::move(data));
auto actor=engine::makeTerrainActor("Ground",asset,true);
actor->transform.position={-16,0,-16};
auto* terrainActor=actor.get();
scene.adoptChild(std::move(actor));
scene.synchronize(); // Register optional physics without running a variable tick.

auto local=asset->ground(2,3);
auto world=engine::terrainGround(scene,2,3);
auto ray=engine::terrainRaycast(scene,{2,20,3},{0,-1,0},30);
```

Local origin is sample `(0,0)`, +Y is up, and +Z follows increasing image row.
Samples use `heights[z*samplesX+x]`, in metres. Each cell splits into upward-facing
triangles `(x,z),(x,z+1),(x+1,z)` and `(x+1,z),(x,z+1),(x+1,z+1)`.
Queries interpolate this exact piecewise-planar surface, rather than bilinear
interpolation. Partial edge chunks are supported. Shared border vertices use the
same positions, cross-chunk source normals, tangents and metre-based UVs.

`TerrainAsset::ground` is an O(1) local-grid query. World queries visit active
terrain chunks, reject ray/AABB misses, then test candidate triangles on the CPU.
They return a two-sided closest ray hit or highest world-space vertical hit, with
geometric normal, distance and scene-scoped `ComponentId`. `terrainGround` distance
is from an internally chosen point above the terrain; use its position for height.
Ray direction is normalized internally. Invalid inputs/zero directions throw;
misses return `nullopt`. Disabled/pending-destroy nodes are excluded. Render masks,
camera visibility, culling, interpolation and material opacity do not affect ground.
Queries can target a scene or subtree, include ancestor transforms, and do not
create physics. Visual instances support invertible transforms. No BVH query
accelerator is provided. With streaming, world queries cover resident chunks only;
`asset->ground()` still samples the full source, including absent chunks.

## Heightmaps and appearance

Decode an image with `Texture::loadMemory(bytes,name,limits)` and pass it to
`TerrainAsset::fromHeightmap(image,minimum,maximum,spacing,chunkCells,textureMeters,material)`.
RGBA8 **red**, without gamma decoding, maps linearly to the height range; other
channels are ignored, and no implicit image flip occurs. This is 8-bit precision,
including higher-bit source images decoded into RGBA8. Use float samples for higher
precision. Equal minimum/maximum makes flat terrain.

An ordinary immutable `Material` with repeat-wrapped base-color texture supplies
the single-texture surface. Optional `Material::layers` blends four tiled layers.
Per-chunk `MaterialInstance` overrides use existing mesh APIs.
Defaults are unit spacing, 32 cells per chunk, four metres per texture repeat and
the standard material. Both renderer backends reuse ordinary mesh uploads, bounds,
culling and recovery. WebGL 2 retains its documented lighting/PBR/shadow limits.

Validation requires 2–4,097 samples per axis, at most 1,048,576 total samples,
1–128 cells per chunk and at most 4,096 chunks. Spacing is at least 0.0001 metres;
horizontal extents and absolute heights are at most 1,000,000 metres. Texture repeat
size is 0.0001–1,000,000 metres. Finite data and a valid non-null material are required.
Validation precedes asset construction. There is no in-place editing.

## Four-layer material blending

`MaterialLayers` is an optional ordinary-material feature on PBR and unlit meshes;
it is also available independently of terrain. Each of its four `MaterialLayer`
entries has an albedo `texture`, linear `color` multiplier, `roughness` in [0,1],
`uvScale` and `uvOffset`. `weights` is an RGBA8 texture sampled as **linear data**.
RGBA select layers 0–3; weights normalize after filtering. An all-zero texel selects
layer zero. Missing color textures use white; absent weights select layer zero.
Use clamp-wrapped weight textures to keep filtering at the outer boundary local.

```cpp
auto appearance=std::make_shared<engine::Material>();
appearance->roughness=1; // Multiplies the weighted layer roughness.
appearance->layers.enabled=true;
appearance->layers.layers[0].texture=grass; // Shared decoded Texture assets.
appearance->layers.layers[1].texture=sand;
appearance->layers.layers[2].texture=rock;
appearance->layers.layers[3].texture=soil;
appearance->layers.layers[1].uvScale={1.5f,1.5f};
appearance->layers.layers[2].roughness=0.65f;
appearance->layers.weights=blendWeights; // RGBA, with wrapU/wrapV=Clamp.
const float width=(data.samplesX-1)*data.spacing.x;
const float depth=(data.samplesZ-1)*data.spacing.y;
appearance->layers.weightScale={data.textureMeters/width,data.textureMeters/depth};
data.material=appearance; // Configure before TerrainAsset::create.
```

Terrain vertex UVs are source-local metres divided by `textureMeters`. A layer's
metre repeat can therefore be set with `uvScale=textureMeters/layerRepeatMetres`.
`weightScale/weightOffset` map those UVs across the whole heightfield, rather than
restarting per chunk. Global material UV transforms apply before layer transforms.
The same source UVs and border vertices are retained at every LOD.

Colors blend in linear light; layer texture alpha also blends before normal
material alpha/mask rules. Material base color, instance tint, scalar metallic and
scalar emissive still apply. Layering uses four albedos, one weight map and four optional packed normal/height
maps. Conventional normal/metallic-roughness/occlusion/emissive maps are inactive
while layering is enabled; scalar metallic/emissive still apply. Water materials reject layering. Optional
`MaterialInstance::layers` replaces the whole layer definition for an instance.
Validation completes before `setMaterial` replaces a valid override.

Both backends blend color/alpha. WebGPU uses weighted roughness with existing PBR,
IBL and shadows; WebGL 2 retains its basic lighting and does not implement PBR maps,
roughness lighting or shadow maps. Backend caches include layer textures, semantic
roles and all blend parameters. Upload admission and device/context recovery use
the existing immutable-resource paths.

## Surface relief and projection

Each layer optionally supplies `normalHeightTexture`: linear RGB tangent normals
(+Y/OpenGL convention) and linear alpha height. Use decoded RGBA textures; normal
mip generation renormalizes RGB while averaging alpha linearly on WebGPU. WebGL2
uses its existing linear base-level filtering. Missing maps have flat normals and
height zero. `normalScale` defaults 1 (0 disables relief, range 0..16);
`heightScale` defaults 1 (0..16), `heightBias` defaults 0 (-16..16).

```cpp
appearance->layers.layers[2].normalHeightTexture=rockNormalHeight;
appearance->layers.layers[2].normalScale=.8f;
appearance->layers.heightBlend=.25f;
appearance->layers.triplanar=true;
appearance->layers.triplanarScale=1.f/3; // World-space repeats per metre.
appearance->layers.triplanarSharpness=4;
```

`heightBlend` defaults zero, preserving normalized linear weight blending. Positive
values (up to 1) define a transition width: score each layer as authored weight plus
scaled/bias height; subtract the maximum active score minus width, clamp to zero,
multiply by authored weights, then normalize. Zero-weight layers stay excluded.
The resulting weights blend color, alpha, normals and roughness together. This is
surface shading, with no parallax, displacement, collision or query change.

`triplanar` defaults false. Enabled projection samples three signed world-axis
planes, weighted by `abs(geometricWorldNormal)^triplanarSharpness` (range 1..16).
Scale defaults 1 and accepts .0001..1,000,000 repeats per world unit. Global material
and per-layer UV transforms apply to these planes. Weight maps always retain mesh
UVs, including per-page texel-center transforms, so painting does not move. Plane
normal perturbations are reoriented before blending; flat maps retain the geometric
normal. World projection remains continuous across chunk/page boundaries and LODs,
and stays fixed in world space when an actor moves. It costs three color and three
relief samples per layer rather than one each; applications choose where to enable it.

Both backends apply layered normals to their existing lighting. WebGPU retains PBR,
IBL and independent shadow detail; WebGL2 retains basic lighting without shadows.
Nine material textures plus seven existing frame textures fit the WebGPU minimum
16 sampled textures per stage; no extra passes or render targets are introduced.
Materials, instance overrides and collected assets retain packed maps and settings
in archive format 23. Older archives load linear blending, mesh UVs and no layer
normal maps. Match host and worker codec artifacts when deploying paged terrain.

The demo Surface selector compares linear mesh UVs, normals, height blending,
triplanar and the combined default. See [surface validation](terrain-surface-validation.md)
and `scripts/terrain-surface-smoke.cjs`.

## Replace layer definitions

Existing terrain can replace its immutable layer definition using
`auto edit=asset->withLayers(layers)`, then `applyTerrainEdit(*actor,edit)` (or the
owning stream's apply path) at an idle boundary. The edit validates the complete
material, shares all existing geometry/LOD chains, preserves source/patch metadata
and publishes with `paintChanged=true`, without replacing collision. Unchanged
definitions return an unchanged edit. The editor uses this for per-layer texture
assignment while retaining paint weights and the other layer parameters.

## Seamless camera and shadow LOD

New `TerrainData` defaults to three derived candidates with interior grid steps
2, 4 and 8. `lodPixels={240,120,60}` gives descending projected-diameter thresholds;
generation retains only candidates that reduce triangles. Set `lodLevels=0` to
keep full detail. Partial/small edge chunks can have shorter chains.

Every level preserves **every full-resolution perimeter vertex and edge segment**.
Coarse interior cells connect to those edges through triangle fans. Neighboring
chunks can choose arbitrary different levels without skirts, T-junctions, holes or
neighbor-state negotiation. The fixed perimeter has a cost at very low detail;
this approach prioritizes reliable seams and existing render integration. Interior
transitions are discrete with hysteresis; there is no geomorphing.

The existing `RenderFrame` selector uses source bounds and per-camera projected
pixels, with separate per-light projected shadow texels and 10% hysteresis. It
never replaces a component's authoritative mesh. `MeshComponent::lodPolicy` is an
optional generic presentation policy:

```cpp
engine::MeshLodPolicy policy;
policy.cameraLevel=-1; // Automatic; 0 means full detail, 1+ forces an available level.
policy.shadowLevel=0;  // Keep shadows at full detail independently of the camera.
policy.cameraScale=1;
policy.shadowScale=2;  // Higher projected-size scale retains finer automatic detail.
policy.validate();
for(auto* chunk:terrainActor.getComponents<engine::TerrainComponent>())
    chunk->lodPolicy=policy;
```

`terrainActor` denotes the instance actor. Scales must be finite, greater than zero
and at most 64; fixed levels are -1 through 16 and clamp to each available chain.
Global renderer/quality LOD switches gate these policies. Camera-free collection
and disabled LOD retain full detail. Culling opt-out and deforming meshes retain
the existing safety restrictions. Shadow history is per light and independent of
camera history; off-camera casters still participate. WebGL 2 selects camera LOD
but reports shadow maps unavailable, retaining the authored shadow policy in saves.

Ground, selection and collision always use source-resolution geometry regardless
of camera/shadow LOD, visibility or interpolation. Terrain caches own the immutable
chains; GPU levels upload on demand. Extra levels increase retained asset memory;
fewer submitted triangles alone do not establish an FPS improvement.

## Sculpting and painting

The browser scene editor exposes these same transactions through **Quick Add →
Terrain** and **Details → Terrain**, with complete-instance sculpt/paint strokes,
one editor history command per gesture, heightmap creation and ordinary scene
persistence. See [editor terrain controls](scene-editor.md#terrain-editing).
Source-only streams/paged patches retain their existing separate owner below.

```cpp
engine::TerrainBrush brush;
brush.mode=engine::TerrainBrushMode::Raise;
brush.center={8,8}; // Source-local X/Z metres; convert world input into this space.
brush.radius=3;
brush.strength=0.5f; // Signed height delta for Raise; negative lowers.
auto edit=asset->edit(brush); // Old source and published meshes remain unchanged.
if(edit.changed()) {
    engine::applyTerrainEdit(*terrainActor,edit); // Idle hierarchy boundary.
    scene.synchronize(); // Reconcile replaced collision before querying/stepping it.
    asset=edit.after;
}
```

`terrainActor` denotes the complete actor from `makeTerrainActor`. Each stamp uses
smooth radial falloff, zero at the brush edge and one at its center. `Flatten`
blends toward `targetHeight`; `Smooth` blends toward the frozen source's 3×3 sample
mean, with edge neighbors clipped. These and `Paint` require strength in `[0,1]`.
Raise accepts a finite signed delta; results outside the existing ±1,000,000 m
height limit are rejected before publication. Radius is finite, 0.0001–1,000,000 m;
center and target must be finite. Zero-strength and outside stamps return unchanged
source pointers. Brush creation/copying scans bounded source data synchronously;
there is no worker or time-budget guarantee.

`TerrainEdit` holds before/after assets, `affectedChunks`, and height/paint flags.
Height changes rebuild only intersecting base/LOD chains plus a conservative
two-sample halo for cross-chunk normals and coarse interpolation. Unaffected chunk
pointers remain identical. `applyTerrainEdit` rejects stale source versions and
retains component handles, transforms and camera/shadow policies. Independently
replacing inherited `mesh` remains unsupported. A custom instance must publish all
its terrain chunks together; use `TerrainStream::apply` for streaming instances.

Paint uses `layer` 0–3 on enabled layered materials. It blends normalized RGBA
weights toward that channel, quantizes to bytes with a total of 255, and replaces
the immutable texture/material. All-zero source weights mean layer zero. Existing
weight-map dimensions, positive material/weight scales and their offsets map texels into source-local
metres; single texels represent the map center. A missing map creates a grass map
matching source dimensions with each texture axis capped at 4096, mapped across the
full terrain. Painting clamps texture wrap and requires
positive X/Z weight and material UV scales. Nonpositive scales and disabled layers are rejected.
It rebuilds no geometry or collision; all chunk bindings receive the new weights.
Material overrides otherwise remain instance-owned. Inspect blended appearance
after using isolated-layer diagnostic overrides.

Edits returned by `TerrainAsset::edit` are version-specific transactions, not an
arbitrary topology replacement protocol. Applications own gesture sampling,
history limits and cancellation policy. Keeping old versions also keeps their
source and changed resources alive. The browser example bounds history to 16 stamps.

## Collision and lifetime

Collision is opt-in (`makeTerrainActor` defaults to `false`). An enabled instance
owns one static rigid body and one triangle-mesh collider using all source cells,
independently of render detail/culling. Adopt directly under Scene, retain unit
actor/scene scale and identity chunk transforms, and follow [physics ownership](physics.md).
Translation and rotation are supported. Source dimensions/heights are baked into
geometry. Moving individual visual chunks or shader displacement does not move
the collider. Ground queries sample the original CPU triangles.

The scene owns instances; chunks share immutable asset meshes and materials.
Replacing a chunk's inherited `mesh` independently is unsupported and rejected by
terrain validation, queries and saving. Apply an edit transaction, or build/adopt
a replacement actor at an idle boundary. Delayed users retain scene-scoped handles;
full-source terrain introduces no callbacks or additional simulation scheduler.
Optional paging workers use the separate adapter and ownership contract below.

Height edits to a complete `makeTerrainActor` instance replace its one full-source
collider; painting retains the exact shape pointer. Streaming instead replaces
only affected resident chunk colliders. Neither mechanism moves gameplay bodies
onto new ground. Applications choose wake/reposition policy for sleeping bodies
and keep actors within resident collision coverage; the demo wakes its ball bodies
after sculpting. Existing triangle-collider size/degeneracy limits still apply.

## Bounded streaming

```cpp
auto source=engine::TerrainAsset::create(data,false); // Source-only: no chunk meshes.
engine::TerrainStreamSettings settings;
settings.radiusChunks=1;
settings.maxResident=9;
settings.buildsPerUpdate=2;
settings.collision=true; // Opt-in; false by default.
engine::TerrainStream stream(scene,source,settings); // Scene outlives stream.
stream.update({12,12}); // Application-selected source-local focus; call each frame.
auto edit=stream.asset()->edit(brush);
stream.apply(edit);
// stream.clear() or destruction removes owned actors. Keep these at idle boundaries.
```

Defaults are radius 1, at most 9 chunks, at most 2 builds per update, no collision,
and zero translation. Limits: radius 0–32, resident count 1–256 and builds 1–64.
`origin` is a finite translation (±1,000,000 m per axis); rotation/scale are excluded
from this owner. Focus may lie outside the grid and selects the nearest edge cell.
Radius chooses a clipped square of candidate chunks, sorted by physical center
distance with chunk-index tie breaking; `maxResident` caps that set. Settings are
immutable for an owner; prepare a replacement owner for a new configuration.

Each update prepares at most `buildsPerUpdate` admissions, evicts chunks outside
the desired set, then adopts the prepared scene-root actors and synchronizes.
Residents never exceed `maxResident`; staging can temporarily hold up to that
count plus one admission batch. Each actor owns one ordinary `TerrainComponent`
and, when requested, one static full-resolution chunk body/collider. Dirty resident
chunks rebuild on edit; editing absent chunks generates nothing until admission.
Cold admission includes all configured LODs and synchronous collision cooking.
No source IO, worker, frame-time guarantee or invisible eager geometry cache is
involved. `chunks()` is empty for source-only assets; `chunkCount()` and
`buildChunk(index)` provide metadata and explicit caller-owned generation.

Eviction destroys actors, invalidates their handles and releases CPU geometry.
Previous render frames and backend weak caches release GPU resources normally.
External retained mesh pointers can extend that lifetime. Streaming bounds derived
residency; the entire heightfield and global weight map stay resident under their
existing sample/texture caps. Source copies/history and collider/driver overhead
are outside `stats().geometryBytes`, which counts resident visual vertex/index
payload across all LODs. Stats also expose resident/pending, cumulative builds and
evictions, and `lastRebuilt` for the latest edit. Camera/shadow LOD never
chooses residency. Applications must include all gameplay, shadow and query areas
they require, and decide what to do with bodies when their ground is evicted.

## Source paging and asynchronous generation

`engine/terrain_paging.hpp` adds a separate path for worlds whose source cannot
remain fully resident. It preserves the editable full-source APIs above. A
`TerrainPageLayout` contains grid dimensions, spacing, chunk cells, texture metres
and LOD settings, without allocating the grid. Dimensions are 2–65,537 per axis,
at most 1,048,576 pages, with the same spacing/extent/height/LOD limits as ordinary
terrain. Each page owns one chunk plus a clipped **two-sample halo** on all sides.
Heights and optional sample-sized RGBA weights are row-major, including the halo.
An immutable nonzero 64-bit revision identifies a coherent provider dataset.

`layout.region(page)` supplies the interior rectangle, global sample offsets and
page ID; `dimensions(page)` supplies the actual halo source dimensions. Use these
to author `TerrainPage`, then `encodeTerrainPage`/`decodeTerrainPage` for bounded
little-endian **WETP v2** files with layout/page/revision/generation/counts and CRC32.
Optional physical mappings use WETP v3; neutral pages retain v2. See
[physical terrain and page contracts](physical-surfaces.md#terrain-pages-and-workers).
Version 1 base files remain readable with generation zero. Decode
rejects malformed, nonfinite, oversized (>256 KiB), truncated or trailing data
before generation. `tools/terrain_demo_pages.py --output <directory>` demonstrates
offline authoring of global samples into static HTTP page files.

```cpp
#include "engine/terrain_paging.hpp"
#include "engine/terrain_browser.hpp" // Browser host, engine_assets_browser only.

engine::TerrainPageLayout layout;
layout.samplesX=layout.samplesZ=1025;
layout.spacing={0.5f,0.5f}; // chunkCells defaults to 24.
auto appearance=std::make_shared<engine::Material>();
appearance->layers.enabled=true; // Configure four shared tiles; no global weights.
engine::BrowserTerrainOptions io;
io.collision=true; // Match residency.collision below.
auto loaders=[layout,appearance,io](std::uint32_t page) {
    return engine::browserTerrainPageLoader(
        "assets/terrain/"+std::to_string(page)+".weterrain",
        layout,page,1,appearance,io);
};
engine::TerrainPagingSettings settings;
settings.residency.collision=true;
settings.maxInFlight=2;
settings.maxSourceBytes=64*1024;
engine::TerrainPagedStream pages(scene,layout,1,loaders,settings);
pages.update({256,256}); // Idle owner boundary every frame; never waits for IO.
auto ground=pages.ground(256,256); // Missing until that page is resident.
```

The page material template must not retain a global weight map. Generation shares
its four tiles and creates a clamp-wrapped page mask. Global material UV scale
must be positive when weights exist; texel-center transforms include global UV
offsets. Positions, tiled UVs, border normals and every LOD perimeter stay in global
source space. No renderer-specific terrain representation is added.

`generateTerrainPage(page,material,collision)` is pure CPU work suitable for an
application executor: it returns immutable patch geometry/LODs and optional raw
triangle collision, without Scene, Jolt-world or GPU ownership. Native hosts
provide IO/executors using `TerrainPageLoader`, a factory of ordinary `AssetLoader`
callbacks completing `TerrainPageResult`. Capture owned resources and completion
contexts, never scene pointers. There is no implicit native pool or scheduler.

`browserTerrainPageLoader` performs HTTP, decoding and generation in isolated
workers. Ship **terrain-worker.js, terrain-storage.js, asset_decode.js and asset_decode.wasm** from the
same build; respect worker CSP/CORS and relative URLs. No pthreads, shared memory
or cross-origin isolation is needed. The adapter caps two active workers and 32
queued/running requests per main module. Defaults are a 256 KiB response cap,
30 s timeout (maximum 120 s), and 8 MiB result cap. The owner validates/copies
prepared transport and reconnects shared tiles without generating topology again.
Optional Jolt shape cooking/registration and GPU uploads remain on the owner and
renderer paths; this is not a hard frame-time guarantee. Each in-flight job also
has bounded worker runtime, material, transport and hydration overhead.

`TerrainPagingSettings` reuses the explicit radius/resident/admission/translation
settings of `TerrainStream` (collision off by default), plus 1–8 in-flight jobs
(default 2) and resident source budget (default 4 MiB, maximum 64 MiB, at least one
worst-case page). Candidate distance/index ordering reserves height **and** RGBA
capacity, even for providers without masks. Budget/count cap the desired set;
ready results remain in bounded pending slots until admitted. Resident source
bytes count float heights and RGBA bytes; geometryBytes counts visual chains.
These exclude shared tiles, colliders, transport, worker heaps, external pointers
and driver overhead, so neither is a total-process memory budget.

Idle `update` cancels obsolete jobs, evicts unwanted pages, then admits at most
`buildsPerUpdate` ready results and launches up to `maxInFlight`. It validates
layout/ID/revision, source/mesh/collision consistency and overlapping neighbor halo
heights/weights before adoption. Failures preserve other residents and remain
bounded to the desired set; `retryFailed()` explicitly permits another attempt.
Stats expose pending/queued/failed/resident counts, bytes, cumulative started,
completed/cancelled/evicted, worker completions and a bounded error string.
Cancellation terminates browser work; weak AssetLoadContexts safely discard late
reports after focus changes or owner destruction. Scene must outlive the owner;
update/clear/destruction require idle boundaries. No automatic camera policy is
added. Eviction removes solid ground: applications must keep gameplay inside the
resident region or choose their own loading barriers.

`TerrainAsset::createPatch` creates one immutable chunk from bounded halo samples;
its source queries accept global source-local metres and only its interior.
`TerrainPagedStream::ground` queries resident source data; world queries still
visit active Scene geometry. Unloaded pages return no ground and queries never
trigger IO. Direct patch `edit` remains rejected; use the paged stream transaction
API below. Prefetch caches and changing dataset revisions in place remain future
work. Replace the owner for another dataset revision. Full-source editing and
`TerrainStream::apply` retain their existing behavior.

## Paged sculpt/paint and dirty-page writeback

Opt in with `TerrainPagedStream::enableEditing(writer,settings)`. A loader and
writer must address the **same** dataset revision and stored page generations.
The default paged stream still rejects edits when no writer is configured.

```cpp
// A separate editable owner, using the layout/material/settings from above.
engine::BrowserTerrainOptions editableIO;
editableIO.collision=settings.residency.collision;
editableIO.storageNamespace="my-terrain-v1";
auto editableLoaders=[layout,appearance,editableIO](std::uint32_t page) {
    return engine::browserTerrainPageLoader(
        "assets/terrain/"+std::to_string(page)+".weterrain",
        layout,page,1,appearance,editableIO);
};
engine::TerrainPagedStream editablePages(scene,layout,1,editableLoaders,settings);
editablePages.enableEditing(engine::browserTerrainPageWriter(editableIO.storageNamespace));
engine::TerrainBrush brush;
brush.center={264,264}; // Global source-local metres, not actor-translated world X/Z.
brush.radius=2;
brush.strength=0.5f;
if(editablePages.edit(brush)) {
    // Accepted, not necessarily applied. Continue idle update(focus) each frame.
}
// stats().editPending / edits / editError report publication or rejection.
editablePages.flush(); // Explicit save/retry. Automatic writeback defaults on.
```

`edit` validates the brush and queues at most one stamp. Zero/outside stamps return
false. The required set includes every page whose source halo can contain a
changed sample, even when its visible interior is unaffected. Source/resident and
write-staging limits must accommodate required pages plus existing dirty pins;
otherwise the request rejects before changing source. `update` prioritizes those
pages, evicting clean focus pages as needed. It waits for all required source
pages, then evaluates global samples once from the frozen pre-stamp source. Smooth
uses clipped global 3×3 neighbors. Raise/lower, flatten and paint follow the brush
strength/falloff semantics above. Paged masks use sample centers; missing weights
become grass before painting. Shared heights and RGBA halo copies change together.

All candidate resources validate before idle publication. Height changes rebuild
affected base/LOD chains and optional chunk colliders, preserving scene handles and
policies; painting retains exact geometry/collision pointers. Unchanged pages share
their old resources. Queries and collision see the committed stamp together.
Source load failure or invalid height leaves old source intact and reports
`editError`. Focus changes or `cancelEdit()` cancel only the not-yet-published
stamp. Completion of a queued stamp is independent of pointer release; applications
own gesture policy. The demo cancels unpublished stamps on contact cancellation.

Brush evaluation and edit reconstruction run synchronously on the owner at the
idle boundary, bounded by the required pages; `buildsPerUpdate` limits admissions,
not this indivisible publication. Load-time generation remains asynchronous.
There is no hard frame-time guarantee for large brushes. App-owned undo/history
and automatic conflict merges are not supplied for paged edits.

`TerrainPage::revision` identifies immutable dataset layout/content provenance;
`generation` identifies each mutable page version, initially zero for WETP v1
source files. A committed edit increments every changed page's generation. Dirty
pages stay pinned within the existing resident/source caps until their saved
generation catches up. This can reduce the new focus's coverage while storage is
blocked; ground is never evicted merely to make room for more dirty work.

`TerrainPageEditingSettings` defaults to `autoWriteback=true` and an 8 MiB
`maxWriteBytes` batch cap (allowed 256 KiB–64 MiB). One write batch can be in flight,
capturing all dirty page sources plus their expected saved generations. New edits
may continue while it writes; an acknowledgement only advances the versions in
that batch and leaves newer edits dirty. Pending batch source/encoding and edit
staging are additional bounded memory outside resident source counters. A write
budget includes WETP headers/CRCs and batch framing, not just sample payload.

Native `TerrainPageWriter` is a factory of ordinary AssetLoaders receiving an owned
immutable `TerrainPageWrite` and completing `TerrainPageWriteAck{token}`. It must
compare every expected generation and replace **all** batch pages atomically,
including halo-only changes. Acknowledgement means storage transaction completion;
dispatch alone is insufficient. `encodeTerrainPageWrite`/`decodeTerrainPageWrite`
provide bounded WETW v1 transport with token, up to 256 WETP pages, expected
generations and CRC32 (maximum 64 MiB). Hosts supply their own filesystem/server
storage adapter; there is no portable database dependency or remote HTTP writer.

The browser adapter `browserTerrainPageWriter(namespace,timeout=30000)` uses
**terrain-storage.js** on the host and workers. Namespaces contain 1–128 ASCII
letters/digits/`_.:-`. All page records in one batch compare-and-replace inside one
IndexedDB `readwrite` transaction requesting strict durability, with success only
on `oncomplete`. Workers read that origin/namespace overlay before HTTP and validate
CRC/source metadata before generation. Dataset layout/revision metadata rejects
incompatible records. Writes are capped to 64 MiB and timeout at 30 s by default
(maximum 120 s). Corrupt/blocked/unavailable storage errors surface; they do not
silently fall back to stale base pages. Storage is origin-bound browser data and
can still be cleared/evicted by browser/user policy.

Failures, quota errors and generation conflicts retain local dirty pages and stop
automatic retries. `flush()` explicitly saves/retries. Conflicts never overwrite
a competing committed generation; applications may keep local data for manual
merge/export or reload the stored version with `clear(true)`. `clear()` rejects
dirty data/pending writes by default. `clear(true)` explicitly cancels interests
and discards unsaved state; owner destruction also discards it, so Scene must
outlive the owner and applications must flush/await clean state before releasing
it when edits must survive. Abort/cancellation cannot roll back a write that
already committed. The browser demo blocks reset/return/reconfiguration while
dirty or writing and provides an explicit discard action.

Stats add `editing`, `editPending`, `edits`, `lastAffected`, `lastRebuilt`,
`dirty/dirtyBytes`, `writePending/writing`, `writeFailed/writeError`,
`writes/writtenPages` and `editError`. Clear/discard resets transient errors and
residency; cumulative counters remain diagnostic. These counters do not establish
total process memory or FPS.

## Save/load

Built-in archive format **15** stores shared terrain source/material, chunk
references, mesh instance settings/overrides and existing collider/body state.
Eager chunk meshes regenerate once per shared asset during staged load; collision uses
the existing collision-shape representation. Saved identities/resource sharing
survive; runtime handles/GPU objects are recreated. Application archives inherit
support through their scene archives. No custom component codec is needed. Format
12 adds layer definitions/weight sharing, terrain LOD generation settings and mesh
camera/shadow policies. Format 14 adds eager/source-only mode; source-only assets
regenerate only saved resident component meshes, charging each generation to the
allocation budget. Derived meshes/hysteresis/GPU objects are not serialized.
Edits save as current source heights/weights. Resident actors/collision survive;
stream owners, focus, budgets, pending admissions and undo/redo do not. Loaded
stream snapshots are static until the application explicitly creates its owner
and replaces the restored resident actors. Asset-library source-only entries load
without generating terrain. Formats 10–13 retain eager generation. Format 15 adds
optional patch interior/global sample offsets and page ID. Paged snapshots retain
only resident source halos, materials and actors/collision; they regenerate at the
same global positions without the provider. URLs, provider revision/configuration,
jobs and paging owners are not saved. Restored patches remain static snapshots.
Format 15 already stores edited patch heights/weights; no scene archive version
change is needed for writeback. Page generations, dirty flags and write requests
are storage/controller state, excluded from scene saves. Saving a scene neither
flushes the provider nor restores an editable owner; recreate it against the
intended persisted dataset and replace restored resident actors explicitly.

`AssetLibrary::terrains` holds uninstantiated sources. `collectAssets` deduplicates
terrain sources and omits their generated meshes from the mesh list. Browser asset
import creates visual terrain instances, with physics remaining opt-in. Formats
1–14 remain readable; format-10/11 terrain retains full-detail-only generation,
single-texture appearance and neutral policies. Archive allocation/array limits still apply, including existing
collision index arrays. Generated mesh allocations are charged before reconstruction.
Malformed loads preserve the displayed scene. Physics restoration is not exact
solver rewind.

## Demo and checks

Open `?scene=terrain&renderer=webgpu` or `?scene=terrain&renderer=webgl2`.
The **Terrain demo** section offers open/return/reset, ball drops, coordinate queries
and local heightmaps, four layer isolation/weight diagnostics, separate camera and
shadow detail/scales, mixed-neighbor inspection and an overhead orthographic view.
Navigate mode uses click/tap for a yellow marker and existing drag/orbit, pan and
zoom. Editing modes own pointer gestures: choose Raise/Lower/Flatten/Smooth/Paint,
radius, strength, target height or layer, then click or drag the terrain. Stamps
are separated by at least radius/4 and capped at eight per input frame; cancellation
or release stops further stamps and keeps those already committed. **Apply at
Ground X/Z** uses the same brush. **Undo/Redo stamp** retains at most 16 operations.
Switch to Navigate to move the camera; Reset camera frames the full source.

Streaming controls choose chunk radius, resident limit and builds per frame. Enable
it around Ground X/Z; **Move focus to Ground X/Z** changes only its focus. Applying
settings replaces the owner, clears balls/history and releases the old terrain.
Disabling it rebuilds the full terrain. Readouts distinguish affected/rebuilt
chunks, pending/resident chunks and source height. Edits to absent chunks remain
in the source and appear on admission. New admissions inherit the chosen camera/
shadow detail and surface inspection mode. Query/drop controls require resident ground.
**Open paged world / apply budgets** switches to an editable 1025×1025 sample,
512×512 m provider with 1,849 static page files. It releases the full source and
history, then fetches/generates only desired pages. Radius/resident/admission fields
also apply here; pending-job and resident-source limits are separate controls.
Move focus with Ground X/Z, use **Frame resident focus** to view that region,
inspect requests/bytes/errors, and use **Retry failed
pages** after resolving provider failures. **Return to editable hills** restores
the original hills dataset. Paged edits save automatically to IndexedDB and survive
eviction/reload. **Save dirty pages / retry writeback** retries a failed batch;
**Discard unsaved page edits** reloads the last stored versions. Reset/return or
budget changes wait for dirty/pending writes to complete or explicit discard.
**Clear saved page edits** is available outside paging to remove the stored
overlay. The 16-stamp undo/redo controls apply to full-source hills; paged history
is application-owned. Saving archives the resident
snapshot, not the unseen world or provider configuration.
**Scenes & assets → Save scene / Open scene** preserves terrain, collision, ball
poses/velocities, query marker, camera and pause policy. Saved scenes use the normal
viewer; queries and collision remain available. Return to playground before using
demo-only import/drop/reset controls.

The default is a 97x97 float grid with 0.5 m spacing and 24-cell chunks (16 chunks),
grass/sand/rock/soil layers and an orange 0.55 m radius ball. Demo imports accept local
2–257 pixel images, mapping red to 0–8 m. Successful import replaces mesh/collision
together, clears balls/markers and reframes. Failed imports retain valid terrain;
stale asynchronous reads are discarded after scene changes. Reusable APIs have
broader limits and configurable height ranges/materials.
Reset returns the original blend/policies and perspective framing. Return restores
the previous scene lighting/sky, camera projection and orbit. WebGL 2 disables the
shadow-detail controls and explicitly reports its capability limit.

Follow README/local Emscripten 6.0.11 build instructions. Native validation uses
`cmake --build build/native-physics` and
`ctest --test-dir build/native-physics --output-on-failure`.
`terrain_tests` covers seams/partial chunks/winding, image mapping, triangle queries,
transforms/subtrees, physics contact/rays, lifecycle, shared scene/library archives
and invalid data. `node scripts/terrain-smoke.cjs` uses the existing `NODE_PATH`,
`BROWSER_EXECUTABLE`, `DEMO_URL` and optional `EVIDENCE_DIR` setup. It exercises both
forced backends, screenshots, camera gestures, Wasm contact, query/collision agreement,
real downloads, fresh-page restore, imported source saves and malformed inputs.
`terrain_tests.exe <output.wescene>` additionally writes a native terrain fixture;
set `NATIVE_TERRAIN_PATH` for the browser smoke to verify it in each backend.
`scripts/terrain-detail-smoke.cjs` checks four isolated layers, normalized/zero
weights, actual camera/shadow submission independence, exact full/coarse/mixed
overhead coverage, resize, archive restoration and paused GPU/context recovery.
`scripts/terrain-edit-smoke.cjs` checks visible brushes, partial rebuild counts,
real pointer gestures/cancellation, edited mixed-LOD seams, bounded admission and
eviction/cache payloads, absent-chunk edits, source/resident queries, archives and
recovery on both renderers. Native tests compare partial rebuilds against complete
regeneration and check weak-resource expiry and stale-handle rejection.
See [recorded validation](terrain-validation.md) for this implementation's evidence.
See [stage-two validation](terrain-detail-validation.md) for detail/layers and
[stage-three validation](terrain-edit-validation.md) for editing/streaming.
CTest `terrain_paging` checks page codec/halos, full-source mesh/LOD equivalence,
worker transport, patch archives/physics, background completion, budgets, eviction,
cancellation, stale revisions and explicit retry. `scripts/terrain-paging-smoke.cjs`
uses real HTTP/workers on both forced backends and checks waiting-frame progress,
source residency, LOD/layer pixels, collision, focus cancellation, HTTP/checksum
failures, retry, reset teardown, saves and recovery. See
[stage-four validation](terrain-paging-validation.md) for current evidence.
CTest `terrain_page_edit` and `scripts/terrain-page-edit-smoke.cjs` add cross-page
brush/halo transactions, delayed source acquisition, shared paint geometry, dirty
pinning/budgets, generation-aware acknowledgements, atomic compare-and-replace,
failure/retry/discard, persistent reload, real gestures, archives and recovery.
See [stage-five validation](terrain-page-edit-validation.md).

Geomorphing, holes, overhangs, remote page storage adapters
and dedicated heightfield physics shapes remain extensions.
Physical Android/iOS and sustained device performance need separate evidence.


Physical terrain surfaces are independent of visual material layers.
`TerrainData`/`TerrainPage` support shared fallbacks and row-major cell maps;
`TerrainAsset::withPhysicalSurfaces` produces a validated edit sharing visual meshes
and replacing collision maps. Complete-source editor range assignment uses history;
paged sculpt/paint preserves authored physical maps through workers and writeback.
Physical page painting remains future work. See [Stage 2 contracts](physical-surfaces.md).
