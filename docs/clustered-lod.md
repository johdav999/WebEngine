# Clustered LOD for static meshes

Prepare clustered data before publishing a static mesh. The builder accepts
engine `Mesh` data, including meshes decoded from GLB or FBX.

```cpp
#include "engine/cluster_lod.hpp"
#include "engine/mesh_optimizer.hpp"

engine::MeshOptimizer::generateLods(mesh); // Optional whole-mesh fallback.
mesh.clusterLod = engine::ClusterLod::build(mesh);
```

Build runs synchronously and returns an immutable result. Prepare large assets
outside frame execution. A failed build throws before assignment and preserves
any existing representation. Call `Mesh::invalidateBounds()` after editing
source geometry and rebuild before reuse. Attach the result only to its source
mesh. Cluster data is transient. Scene archives retain the base mesh and any
existing whole-mesh LOD recipe, and clustered assets require explicit rebuilds.

Each material part has a binary hierarchy. The builder partitions triangle
centroids along the widest axis, targeting 128 triangles per leaf through depth
eight. Leaves preserve the source triangles. Each parent simplifies its patch
with meshoptimizer, preserving exterior edges and weighing normal and UV
attributes. When simplification cannot reduce the combined child index count,
the parent retains that combined representation. Errors are monotonic toward
the root. Bounds cover the source triangles and all indexed approximations.
Parts with non-manifold indexed edges remain intact.

An asset admits up to 300,000 source vertices, 300,000 source triangles, 64
explicit parts and 6,000,000 packed indices. Packed geometry uses one copied
vertex stream. Index ranges are ordered by depth, so adjacent selections can
merge without dynamic index uploads. Renderers cache by shared asset identity.

By default, the camera and each shadow map independently choose groups on the
CPU and can cull subtrees outside their frusta. The selector refines when estimated projected
error exceeds one pixel or shadow texel after the configured detail scales.
Near-plane intersections refine conservatively. Selected ranges reuse the
source transform and material. The CPU path admits up to 64 coalesced ranges per
part and 128 cluster ranges total. A part whose complete cut exceeds the
remaining range budget uses ordinary mesh detail for that pass.

Automatic detail uses clustered LOD for opaque static World draws with valid
CPU bounds. Fixed levels, disabled detail, non-World layers, masked or blended
materials, water, and deformed geometry use existing paths. Planar reflections
use retained ordinary batches. Physics and CPU picking use the source geometry.
Resident and streamed assets use indexed hardware rasterization on all
supported backends. Browser WebGPU and native Dawn can opt into the GPU
camera hierarchy path described below.

## Stream fine pages

Include `engine/cluster_stream.hpp`. Run `ClusterPageAsset::cook` on a prepared
resident hierarchy. Its writer receives the numbered fine-page files. Publish
the returned WECL v1 manifest alongside them. The manifest embeds the complete
coarse root, group bounds and errors, page-local index ranges, byte counts and
content fingerprints. Each page stores a compact vertex stream and local
indices. The cooker groups ranges by depth and targets 512 KiB per fine page.
Groups are indivisible. Each page is capped at 32 MiB and each manifest at
40 MiB. Publish the files as one version to keep the manifest and pages paired.
Use `ClusterLod::build(mesh, maxPatchError)` to cap local-space simplification
error when the coarse root must hold up near the camera. The demo cooks with a
0.065-unit limit.

`ClusterPageAsset::open` validates the manifest and decodes the root.
Construct a `ClusterPageStream` with a loader factory that publishes
`asset->decode(page, bytes)` as a `Mesh`. Attach it to
`mesh.clusterStream` before publishing the source mesh. The attachment belongs
to the scene owner thread. Drive `stream.update()` once before rendering
each frame, including paused presentation frames. Camera and shadow traversal
request fine pages only where refinement needs them. The parent patch draws
until both child pages are decoded and admitted to the active renderer. A cut
that exceeds its range budget can still use the pinned root. Source geometry
continues to define ordinary rendering, physics and picking.

The browser demo uses `browserAssetLoader` for asynchronous HTTP and bounded
owner-thread decoding. Windows uses `DesktopAssetExecutor` for asynchronous
file I/O and worker decoding. Pending and resident fine payloads share a
16 MiB default budget, with up to four in flight and cold eviction after
120 owner updates. The browser demo uses 60 updates to make eviction easy to
observe. A page larger than the configured residency budget stays coarse.
Each renderer separately admits up to four new fine uploads and
2 MiB per frame, allowing one oversized page when the budget is empty.
The root is exempt from the fine-page budgets. These budgets track logical
payloads and admissions. Transport buffers and resources retained for frames
already in flight have separate lifetimes.

Missing, malformed or mismatched fine pages leave their parent drawable and
suppress repeat requests. Call `clear()` to cancel pending loads, release fine
residency and retry on subsequent demand. Already planned draws retain the
selected immutable meshes for the duration of their frame. Scene archives
retain only the ordinary source geometry and its existing whole-mesh recipe.

`ClusterPageStream::generation()` starts at one and advances on every
`clear()`, including an empty clear. Delayed feedback can call
`demand(page, priority, capturedGeneration)` on the owner thread. A stale
generation, invalid page, NaN or nonpositive priority returns false without
adding demand. Positive infinity remains valid for conservative near-plane
error. The existing two-argument `demand` uses the current generation.

## Stone arch comparison

- Browser: open `?scene=stone-arch`. Streamed LOD is the default. Choose Full
  mesh, Whole-mesh LOD or resident Clustered LOD for comparison, and select
  one of the three fixed viewpoints. Clear fine pages to observe a cold
  refinement. The inspector reports residency and pending or failed pages.
  **GPU cluster submission** enables GPU culling and indirect draws.
  **GPU LOD traversal** also enables camera hierarchy selection and page
  feedback. Open `?scene=stone-arch&gpuHierarchy=1` to enable both, and add
  `&shadows=off` to observe camera requests independently of CPU shadow
  requests. **Occlusion test wall** shows the visibility effect.
- Windows: run `webengine_lighting_windows.exe --backend dawn --stone-arch clustered`
  or use `--backend d3d12`. Pass `--stone-arch streamed` for page streaming.
  Pass `--arch-gpu-hierarchy` to enable GPU hierarchy selection and GPU
  submission on Dawn, or `--arch-gpu` for culling with a CPU-selected cut.
  `--arch-no-shadows` isolates camera demand, and `--arch-occluder` enables
  the test wall. Press **F**, **W**, **C**, or **L** for the geometry modes,
  **R** to clear fine pages, **G** for GPU submission, **H** for GPU LOD,
  **O** for the wall, **Left/Right** to cycle cameras, and **Esc** to close. In streamed
  mode, `--capture output.ppm` saves both the cold and refined views and emits
  JSON metrics.

The procedural source and cooker are shared across the examples. To regenerate
the packaged fixture, build the native `stone_arch_cook` target and run it with
`assets/stone-arch-lod` as its output directory. The browser asset target and
Windows lighting target copy the manifest and numbered pages into their assets.

## GPU culling and indirect draws

Set `RenderQuality::gpuClusterCulling` to opt in. Browser WebGPU and native
Dawn share the compute implementation. With `gpuClusterHierarchy` disabled,
the CPU selects a resident cut, requests pages and supplies conservative
bounds for coalesced ranges. Just
before the first clustered World draw, the renderer builds a max-depth
pyramid from depth already written by earlier opaque draws in that view.
Compute projects each range and tests the covered pixels conservatively.
Ranges near the camera clip plane remain eligible to draw. The pass compacts
surviving indices in order and writes one indirect command per compatible
consecutive page and object/material slot. Existing materials and hardware
rasterization draw the resulting ranges.

This path uses a single-sample World depth attachment. WebGL 2, direct D3D12,
MSAA, surface captures, shadow maps, planar reflections and World passes split
for scene-sampling water use CPU submission. Exceeding the depth or batch
limits, or deferring a required source upload, also keeps CPU submission for
that view. The pyramid pads each axis to a power of two and caps the padded
base at 8,388,608 texels and 4096 per axis. Compaction admits up to 64 bins,
128 CPU-selected candidate ranges and 8 MiB of output indices per view. It can cache a
32-bit index copy for clustered meshes. The renderer preserves normal draw
order across bins and ordinary meshes. Occluders drawn later in the World
pass still occlude in hardware depth testing.

In the stone arch inspector, turn on **GPU cluster submission** and toggle the
**Occlusion test wall**. On Windows use **G** and **O**, or launch with
`--stone-arch streamed --arch-gpu --arch-occluder`. The current
`gpuClusterBins`, `gpuClusterCandidates` and `gpuClusterCandidateTriangles`
counters describe encoded work. The separate `gpuClusterVisibleTriangles`
and `gpuClusterSampleTriangles` report an asynchronous sample of the first
eligible view, at most four times per second. The ordinary triangle counter
includes the candidate upper bound for indirect draws.

## GPU hierarchy selection and page feedback

Set both `RenderQuality::gpuClusterCulling` and
`RenderQuality::gpuClusterHierarchy` to enable the optional camera path.
Both flags default to false.
Browser WebGPU and native Dawn submit the pinned root and structurally
enumerate the immutable hierarchy. Compute chooses the cut using projected
error, frustum tests and page-readiness flags. Camera preparation does not
run the CPU hierarchy selector or issue its page demands in this mode.
Shadow maps independently retain CPU selection and can request pages.

A page is GPU-ready only after its decoded mesh and source indices have been
admitted by the active backend. Compute retains the parent until both child
pages are ready. Selected node flags feed the same index compaction and
indirect drawing path, so residency, selection and occlusion remain separate
bounded decisions. An unsupported view or unavailable preparation can draw
its coarse root or use the existing CPU path.

Each part has at most 511 nodes through depth eight. A view admits up to
64 hierarchy plans and 4,096 total enumerated nodes. Hierarchy compaction
allows up to 512 candidates per bin, 64 bins and 8 MiB of output indices.
The single-sample, World-layer and material eligibility requirements of
GPU submission also apply. WebGL 2 and direct D3D12 continue to select and
submit their cuts on the CPU.

Page requests are read back for at most one canvas view per frame, rotating
through the canvas views, with at most two readbacks pending. GPU priorities
encode the projected error as `ceil(error * 256)`, clamped to the finite
range 1..16,777,215. The owner divides by 256 before applying demand, keeping
GPU requests and CPU shadow requests on the same priority scale.

Readbacks hold weak stream and source references plus the captured stream
generation. The renderer also checks its feedback epoch, scene identity,
component and current source mesh, and requires the camera to remain active
in a canvas view. Clearing the stream, replacing its mesh, changing GPU
hierarchy or submission settings, or recovering the device prevents stale
feedback from reviving earlier page work. All demand mutation remains on the
owner thread.

`gpuHierarchyPlans` describes current encoded plans. The asynchronous
`gpuHierarchyVisited`, `gpuHierarchySelected`, `gpuHierarchyRequests`
and `gpuHierarchySampleSerial` fields describe the most recently accepted
feedback sample. The ordinary indirect triangle counter remains a candidate
upper bound; use the existing visible-triangle sample to inspect occlusion.

## Checks

Native CTest `cluster_lod` checks boundaries, leaf coverage, mixed detail,
distance, culling, transform staging, independent shadows, fixed levels and
failed rebuilds, plus GPU root markers without camera-side CPU page demand.
CTest `cluster_stream_feedback` checks generation guards, cancellation,
late completions, invalid feedback and residency budgets.
Run `scripts/stone-arch-smoke.cjs` with the browser playground
served locally and Playwright and pngjs on `NODE_PATH`. Run
`python scripts/stone-arch-windows-smoke.py` after building the Windows lighting
example with Dawn and direct D3D12 enabled. The smoke scripts compare pixels and
submissions and save captures and JSON under `build/evidence`.

Run `scripts/stone-arch-gpu-smoke.cjs` for WebGPU and WebGL 2 and
`scripts/stone-arch-gpu-windows-smoke.py` for Dawn and direct D3D12. These
compare color pixels against CPU submission, check draw consolidation,
and verify from indirect arguments that the wall reduces submitted triangles.

Run `scripts/stone-arch-hierarchy-smoke.cjs` for browser WebGPU and the WebGL 2
fallback. It covers delayed reads with shadows disabled, CPU/GPU cut
comparisons, near and far cameras, occlusion, missing-page fallback and retry.
Run `python scripts/stone-arch-hierarchy-windows-smoke.py --exe build/windows/Release/webengine_lighting_windows.exe`
for native Dawn and direct D3D12. It captures coarse and refined images with
shadows disabled and compares the GPU hierarchy result with CPU selection.

The integrated Emscripten 6.0.11 browser check passed on Chromium WebGPU and
WebGL 2. With shadows disabled and fine HTTP reads held, WebGPU drew the
coarse arch and generated page requests through GPU feedback. Releasing the
reads loaded all 15 fine pages. The near cut visited 343 nodes and selected
172, submitting 41,280 sampled visible triangles; the far preset submitted
10,080. CPU and GPU near captures had identical image bytes. Missing a
required page preserved the coarse image, and clearing
and retrying restored the refined image. The wall/reveal checks passed.
WebGL 2 passed these scenarios through CPU fallback with zero GPU hierarchy
plans. Evidence is in `build/evidence/stone-arch-hierarchy-browser`.

The integrated native Windows Release check passed on Dawn and direct D3D12.
With shadows disabled, Dawn GPU feedback loaded all 15 pages from disk,
visited 345 nodes and selected 173, and sampled 41,520 visible triangles.
CPU and GPU refined captures had identical image bytes on both native
backends. Direct D3D12 used CPU fallback. The native
suite passed `cluster_lod`, `cluster_stream_feedback`, `mesh_optimizer`,
`render_view` and `frustum_culling` (five tests). The isolated lighting
executable and capture evidence are in `build/cluster-hierarchy-native-proof`
and `build/evidence/stone-arch-hierarchy-windows`. These are desktop browser
and native Windows checks; they do not establish sustained mobile performance.
The existing GPU submission and occlusion browser regression also passed on
WebGPU and WebGL 2 after requiring its asynchronous sample to match the
current candidate geometry.

For streaming, run `scripts/stone-arch-stream-smoke.cjs` on browser WebGPU
and WebGL 2 and `scripts/stone-arch-stream-windows-smoke.py` on native Dawn and
direct D3D12. The browser harness delays fine HTTP reads, simulates a missing
page, retries and observes idle eviction. The native harness captures the
pinned root before pumping worker reads, then captures the refined image in
the same process. CTest covers split siblings, delayed GPU admission, invalid
payloads, cancellation and stale completions.

## Recorded milestone 1–3 evidence

In the native Dawn comparison, the unoccluded frame used 23 total draw calls
with GPU submission and 49 with CPU submission. The wall reduced the GPU
sample from 41,520 candidate triangles to 19,440 surviving triangles and the
captured pixels matched CPU submission. The direct D3D12 comparison passed
through the CPU fallback with 49 draws in both wall captures.

In Chromium WebGPU with streamed pages, the unoccluded comparison reduced
total draws from 54 to 31. The wall reduced the sample from 41,280 candidate
triangles to 15,600 surviving triangles. Removing the wall restored the
unoccluded image and triangle count. WebGL 2 preserved CPU submission and
rendered the arch and wall. The delayed-page, failed-page, retry and eviction
regression also passed on both browser backends. Native Dawn additionally
captured streamed GPU submission, growing from one coarse bin to eight bins
with all 15 fine pages resident.

With the Emscripten 6.0.11 desktop browser build, both WebGPU and WebGL 2
submitted 4,825 triangles while the first fine read was held. The near view
progressively loaded all 15 fine pages (4,937,952 bytes) and settled at 61,693
triangles. A simulated missing page restored the identical coarse submission
and image. Clearing and retrying restored the refined image, and switching
away released the fine pages after the idle window.

The native streaming build captured the coarse arch before requesting any
fine files and captured the refined arch after loading all 15 pages from disk
on both Dawn and direct D3D12. The focused Windows suite passed all four tests.
These captures use a staged build because later shared-checkout render-surface
and lighting work interrupted the integrated rebuild.

The milestone 1 resident-mode Chromium WebGPU and WebGL 2 checks used the repository's Emscripten 6.0.11
build. Both reported 61,453 non-shadow triangles in the near whole-mesh shot
and 41,293 in the near clustered shot. At the far preset, the counts were
30,733 and 10,093. Total near-view draw calls rose from 4 to 51. Both tests
checked shadow submissions and restored full-detail pixels after switching
modes. These are desktop browser measurements.

Native Windows Release passed four focused CTests and the stone arch capture
on both Dawn and direct D3D12. Native Dawn submitted 61,453 and 41,533
non-shadow triangles in the near whole-mesh and clustered shots, with 4 and
49 total draw calls. Direct D3D12 submitted 61,452 and 41,532 non-shadow
triangles, with 3 and 48 total draw calls. Both native renderers reduced
shadow submissions from 61,440 to 20,400 triangles. The capture harness uses
fixed camera presets and verifies rendered pixels along with submission counts.
