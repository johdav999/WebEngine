# Fleet mesh detail and shadow cost

## Implementation

`MeshOptimizer::generateLods(Mesh&)` generates three immutable shared static
levels before publication. It simplifies each material part independently with
meshoptimizer's attribute-aware metric (positions, normals and UVs), then runs
the existing lossless cache/fetch optimizer. The original geometry and material
resources are retained. Skin and morph assets are rejected rather than losing
deformation streams. Each nonempty material part survives; simplification can
stop above its requested triangle target when topology/seams constrain it.

The ship stress demo prepares the shared chain once when loading its asset.
Other static assets can opt in through the same generator. LODs are derived
runtime data, not part of the current scene archive format; applications must
regenerate them on restored assets before publication if they need this policy.
Collision meshes and authoring/source geometry do not change.

The selector projects the original local AABB through the object's world matrix
and each view's view-projection matrix. It uses the larger projected pixel
dimension, accounting for perspective/orthographic projection, FOV, viewport,
scale, rotation and parent transforms. Invalid bounds or a near-plane crossing
retain full detail. A component's frustum-culling opt-out also disables bounds-
based detail selection and shadow culling for custom deformation safety.

| Level | Target triangle fraction | Relative error ceiling | Nominal maximum diameter |
|---|---:|---:|---:|
| Original | 100% | 0 | Above 180 pixels |
| LOD 1 | 50% | 0.002 | 180 pixels |
| LOD 2 | 20% | 0.008 | 80 pixels |
| LOD 3 | 6% | 0.020 | 32 pixels |

Each camera and shadow light retains separate 10% hysteresis history, keyed by
source asset and generation-checked component identity. Selection precedes the
existing opt-in instance reordering. Compatible selected meshes/material ranges
still batch; stable frames retain staging capacity and perform no object writes.
All shared levels upload once with the original, avoiding upload stalls on zoom.

Shadow selection uses the light's projection and actual map resolution, measured
in texels. Casters outside the light's full XYZ volume are rejected using
conservative transformed bounds. Camera visibility never removes shadow casters.
Selected shadow geometry uses the same instance/object slots as the camera;
skinned, morphed, mirrored and alpha-masked paths remain supported at full detail.
Opaque shadow pipelines omit the fragment stage; cutouts retain alpha testing.

Shadow resolutions are 256, 512, 1024 and 2048. Filtering uses either one linear
comparison sample or nine comparison samples (3x3 PCF); one sample is not a raw
nearest-depth lookup. Resolution changes recreate the depth array, rebind every
view and reset shadow-detail history. This prevents a previous resolution's
hysteresis state from contaminating a quality comparison. The default remains
1024 with nine samples.

## Controls and measurement

Expand **Hansa ship stress test → Mesh detail and shadows**. Camera LOD, shadow
LOD, casting, receiving and light-volume culling can be independently disabled.
Resolution/filter controls apply to all shadow maps. **Sun shadows** disables
both map passes and receiving work. Each quality/control change resets both
measurement windows while retaining warmed GPU resources.

Disabling casting still clears the maps and leaves receiving/filter work enabled.
Disabling receiving retains caster geometry and map generation. These are
diagnostic isolation modes, not equivalent optimizations. Reducing map resolution
can also select a coarser shadow LOD, so its timing effect is not solely raster
resolution. Disable shadow LOD when isolating only map resolution.

Performance exports include `detailSettings`, `shadowTriangles`,
`cameraLodParts`, `shadowLodParts`, `shadowCulledParts`, `shadowMapResolution`
and `shadowFilterTaps`. Part counters count submitted/culled material instances,
not unique ships; shadows accumulate across active lights and views.
`triangles - shadowTriangles` includes other color/particle/GUI geometry, not
just ships. GPU shadow timing was already separate from color-layer timing.

## Reproduction and acceptance profile

- Product: local WebGPU engine, desktop Chrome, Release, no account required.
- Native: `cmake --build build/detail-native`; `ctest --test-dir build/detail-native --output-on-failure`.
- Browser: isolated `build/web-detail-release`, copied web shell/assets in
  `build/detail-browser-site`; loopback server at port 8103.
- Automation: `scripts/mesh-detail-smoke.cjs`, with `DEMO_URL`, `NODE_PATH` and
  `BROWSER_EXECUTABLE` configured as for existing browser smoke scripts.
- Default comparison: paused 100/1,000 ships, fixed 1440x1040 browser viewport,
  device scale 1, 30 warm frames, reset, full 120-frame and 120-GPU-sample windows.
  Do not run other browser benchmarks or builds concurrently with timing capture.
- Evidence: `build/evidence/mesh-detail/results.json` and paired canvas PNGs;
  existing shadow regression evidence in `build/evidence/detail-shadows/`.
- Scope: generated static geometry, camera distance/scale transitions, independent
  shadow detail/culling, cast/receive isolation and quality controls.

| ID | Flow | Acceptance | Status |
|---|---|---|---|
| DETAIL-01 | Shared fleet LOD | Reduced triangles, retained materials/silhouette, full detail near camera | Verified: native plus paired Chrome captures; near captures pixel-identical |
| DETAIL-02 | Independent shadows | Off-camera casters survive; outside-light casters rejected; skin/cutout correct | Verified: native and final Chrome shadow suite |
| DETAIL-03 | Cost isolation | Identical camera/count; separate cast/receive/off runs; warmed windows | Verified: 16 matched runs, all 120 CPU/GPU samples |
| DETAIL-04 | Quality changes | Correct rebinding/memory; identical geometry for filter comparison | Verified: all four resolutions and both filters across two views |

Native inspection of the unfitted Knarr produces 40,956 / 21,024 / 10,746 /
6,904 triangles. Preview fitting and floating-point execution can cause small
differences in generated counts; browser exports are authoritative for measured
submissions. These levels are shared, so fleet count does not multiply their
asset storage. Additional LOD buffers do increase the fixed asset payload.

This is not evidence for mobile/Safari or an unconditional 60 FPS guarantee.
Timing results apply to this browser, machine, canvas and scene, and include the
concurrent instancing implementation already present in this checkout.

## Measured results — 2026-10-10

Chrome 154.0.8037.99, Release, actual canvas 1030x848 at device scale 1.
Measured Wasm SHA256:
`84A17926C2BD28DFE08CCE73198239862C78EE0CC17B178E8799168E8709D3A9`.
This is an isolated built snapshot of the shared checkout; unrelated concurrent
application/camera/selection work continued afterward. GPU timestamps are
quantized, and this was a shared desktop, not a dedicated benchmark machine.
Repeated full-detail baselines changed by +3.1% at 100 ships and -0.7% at 1,000.

All figures below are mean GPU render-pass milliseconds, not measured FPS.

| Mode | 100 ships | 1,000 ships |
|---|---:|---:|
| Full geometry, 1024 / nine samples | 14.38 | 143.97 |
| Automatic camera + shadow detail, 1024 / nine | 9.95 | 46.20 |
| Automatic detail, casting disabled | 6.62 | 27.89 |
| Automatic detail, receiving disabled | 8.58 | 47.12 |
| Automatic detail, all shadows disabled | 6.29 | 29.11 |
| Automatic detail, 512 / nine | 7.45 | 46.71 |
| Automatic detail, 1024 / one | 8.92 | 46.15 |
| Full geometry repeated | 14.83 | 142.94 |

- At 100 ships: 8,191,658 → 3,179,258 submitted triangles with default shadows.
- At 1,000 ships: 81,912,458 → 13,936,458 triangles (83.0% reduction), with
  shadow triangles 40,956,000 → 6,968,000. GPU p95: 152.11 → 50.33 ms.
- Both default comparisons use 17 draws including the ground/grid, because the
  existing fleet instancing implementation is enabled. The speedup above isolates
  detail selection; it is not a before/after measurement of instancing itself.
- At 1,000 ships, shadow generation remains about 17.91 ms and color passes about
  28.30 ms. Receiver/filter changes show no clear gain at this load. Even without
  shadows, this fleet exceeds a 16.7 ms GPU budget.
- 512 reduces fixed shadow-array payload from 16 MiB to 4 MiB. It helps the
  100-ship test partly by selecting a coarser shadow LOD. At 1,000 ships the
  coarsest available level is already selected, and lowering resolution does not
  meaningfully improve GPU time. Keep 1024/nine as the quality default.
- Visual inspection of paired 100/1,000 fleet images retains hull/sail silhouettes.
  The 100-ship pair changes 0.218% of canvas pixels above the RGB-sum threshold of
  30; this is a viewport difference measure, not a silhouette error guarantee.
  The near-camera automatic/full geometry comparison is pixel-identical.
- Paused warmed runs upload zero bytes and have zero frame-storage growth.
- Final full native suite: 31/31 passed, including detail, shadows, fleet,
  batching, frustum/camera/views, animated/morph paths, optimizer and caches.
  Browser quality, shadow and fleet lifecycle suites all passed without WebGPU
  errors. Quality evidence: `build/evidence/detail-quality/`; final regressions:
  `build/evidence/detail-shadows-final/` and `build/evidence/detail-fleet-final/`.

Concurrent selection/application/camera test corrections were preserved; the final
full-suite run above includes them. This work did not modify those test files.
