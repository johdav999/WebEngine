# Static collection cache and early culling

MeshComponent::renderMaterials() returns a borrowed vector of validated,
metadata-free effective draw materials. It caches one published mesh's resolved
slots and automatically observes component texture/blend changes and by-value
public materialOverrides edits, including nested water/layer/advanced maps.
Published mesh/material assets remain immutable. Replacement validates and stages
all allocations before publication; mesh clearing releases old material references
on the next collection. Color, world transform, visibility, skin/morph and renderer
clock state are evaluated normally and are not frozen by the cache.

Scene::collect and RenderFrame::collect accept a final bool cullUnused=false.
Complete collection stays the default for selection, GI and manual secondary-pass
users. Browser WebGPU/WebGL2 and native Dawn/D3D12 enable the optimized policy.
With a frustum, it rejects off-camera mesh/bulk-instance packets before material
partition expansion, LOD sorting and object staging when no active masked shadow
light or World planar-reflection requester requires secondary geometry. It keeps
mesh/cull counters and traversal order. It does not alter hierarchy, simulation,
physics, asset LOD or collision. Shadow/reflection scenes conservatively retain
complete geometry. Custom secondary-pass users must keep complete collection.

Physics presentation lookup skips hierarchy-only nodes during collection. Both
Windows native and browser renderers consume the same portable material and
visibility paths. No frame phase, archive format or rendering-platform type is added.

Native checks: static_collection, render_frame (warm zero allocations), material,
render_view and mesh_detail. Browser consumers must measure identical warmed
routes/views on WebGPU and WebGL2, and verify shadow/reflection regressions.
The measurements below separate CPU collection improvements from GPU limits.

## Validation on 2026-11-08

Native Windows portable checks passed: static_collection, render_frame, material,
render_view and mesh_detail. This includes direct override edits, map replacement,
invalid data, resource release, separate cameras, secondary-pass retention,
transform counters and 1,000 warmed optimized collections with zero heap allocations.
MSVC x64 direct D3D12 desktop_api_tests also passed using the same collection path.

Chrome 156.0.8078.80, NVIDIA RTX 5060 Ti (actual WebGPU adapter nvidia/blackwell;
WebGL2 ANGLE reports the same GPU), Balanced 100%, CSS 1440 × 900, render 1620 ×
1012. Sequential isolated contexts compared three warmed fixed views using identical
48-asset hashes, viewport, quality and adapter. These are static-view measurements,
not full-lap or sustained mobile acceptance.

| Renderer / view | Before FPS | After FPS | Before collection | After collection |
| --- | ---: | ---: | ---: | ---: |
| webgpu / dogleg | 39.7 | 48.4 | 9.77 ms | 4.88 ms |
| webgpu / quay | 36.1 | 48.7 | 10.40 ms | 3.83 ms |
| webgpu / finish | 39.9 | 47.1 | 10.25 ms | 6.20 ms |
| webgl2 / dogleg | 30.8 | 43.9 | 10.30 ms | 5.09 ms |
| webgl2 / quay | 39.3 | 48.1 | 10.32 ms | 3.73 ms |
| webgl2 / finish | 33.2 | 44.5 | 10.25 ms | 4.96 ms |

The average of the three view FPS values rose from 38.6 to 48.1 on WebGPU and
34.4 to 45.5 on WebGL2. Raw reports/images are in the RacingGame consumer under
`build/evidence/harbour-static-cache/nvidia-final/`. Baseline Wasm SHA256:
`cd8ed858d6136e478f6c8bd38e3d97ea1ff1133eef38f22081e5779d7a5f2826`; candidate:
`305f20dffb5827e5c4fcfa18db51f797c1964cd62e43e006a2522f1710b0283d`.

An earlier Intel run confirmed lower collection CPU cost but did not show uniform
FPS improvement; the fallback remains GPU-limited. Its measurements are retained
under `build/evidence/harbour-static-cache/report.json`. Unmatched driving paths
are excluded from speedup claims. The baseline automated full-lap attempt failed
before a completed lap; no clean-lap acceptance is claimed for this change.
