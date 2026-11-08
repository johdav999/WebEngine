# Volumetric fog profiling

The profiling fixture isolates light-path extinction and geometric filtering
on browser WebGPU and WebGL2 and native Windows Dawn and D3D12. Run a Release
build with the repository-local Emscripten version and matching browser host
files. The native executable is `webengine_lighting_windows`.

Each comparison uses ABBA ordering with a fixed paused scene at 960×540,
1× MSAA and 512-pixel shadow maps. Warm-up lasts at least two seconds and
measurement lasts at least three seconds. Frame floors can lengthen either
period. No screenshots are taken during measured windows. GPU work runs
sequentially across backends. Reports retain the adapter, extent, case
parameters, sample counts, counter checks and source statistics.
The profiling fixture disables the engine demo GUI, and image checks hide
host DOM overlays, so changing HUD text cannot contaminate fog comparisons.

The default run warms 120 frames and measures 180 frames per window,
with three repeats and both 16- and 32-step budgets. Explicit frame floors
of 30 support slow GPU workloads while retaining the duration floors.
Read the reported sample counts when comparing those runs.

| Scenario ID | Workload |
| --- | --- |
| 0 | One box and one spotlight using the camera grid |
| 1 | One box and four spotlights using the camera grid |
| 2 | Four overlapping boxes and four spotlights using direct integration |
| 3 | Clear-air PBR receiver with four spotlights crossing four fog boxes |
| 4 | Spotlight fog in a planar water reflection |
| 5–9 | The corresponding workload with faded box edges |

The `extinction` comparison toggles spotlight self-shadowing, or surface
attenuation for scenario 3. The `soft` comparison isolates geometric shadow
filtering. The `cache` comparison enables the optical-depth cache while
preserving direct extinction, geometry, lights, shadow policy and step budget.
`FOG_PROFILE_MOTION=1` moves the first spotlight continuously to measure
rebuilding. Camera integration, geometric shadow sampling and light-space
extinction are distinct costs.

Run these PowerShell commands with a matching local browser server.

    $env:NODE_PATH = (Resolve-Path 'build/detail-browser-tools/node_modules').Path
    $env:BROWSER_EXECUTABLE = 'C:/Program Files/Google/Chrome/Application/chrome.exe'
    $env:DEMO_URL = 'http://localhost:8168'
    $env:FOG_PROFILE_SCENARIOS = '2,7'
    $env:FOG_PROFILE_STEPS = '16,32'
    $env:FOG_PROFILE_COMPARISONS = 'cache'
    $env:FOG_PROFILE_REPEATS = '3'
    node scripts/fog-profile.cjs browser
    node scripts/fog-profile.cjs windows build/windows-desktop/Release/webengine_lighting_windows.exe
    node scripts/fog-profile.cjs summarize build/evidence/fog-profile

Serve a matched snapshot of the selected browser build when other tasks publish
to `dist`. Set `EVIDENCE_DIR` to preserve independent runs. Native raw JSONL
and GPU traces are retained alongside the aggregate report. The
native hidden fixture keeps presentation active independently of desktop
focus while simulation stays paused. Its `progress.jsonl` records readiness,
suspension and presented-frame counts for diagnosing startup stalls. The
`summarize` command also accepts a raw native `profile.jsonl` or report JSON.
Incomplete comparisons are marked and excluded from paired deltas.
`FOG_PROFILE_BACKENDS` optionally narrows a rerun to `webgpu,webgl2`
for browser runs or `dawn,d3d12` for Windows runs.

WebGPU and Dawn use asynchronous GPU timestamps. Their World timing includes
the camera fog grid, light-cache builds and planar reflection capture.
D3D12 uses full command-stream `gpuProfile.frameMs` and retains named
`Volumetric fog`, `Fog light cache` and reflection spans. The legacy
D3D12 `gpu.totalMs` covers a smaller set of passes.

The browser harness optionally instruments WebGL2 with
`EXT_disjoint_timer_query_webgl2`. It samples a bounded three-query ring at
100 ms intervals around callbacks that issue draw commands. Disjoint and stale
results become unavailable, with diagnostics retained. Set
`FOG_PROFILE_GL_TIMER=0` to compare instrumentation overhead.
Frame cadence includes scheduling and presentation pacing. CPU timings and
wall durations describe their exported scopes and should be read separately
from GPU durations.

## Initial screening on 2026-11-07

The first native screening used an RTX 5060 Ti, driver 32.0.16.1088, on an
i9-13900K. One ABBA block used 16 steps, 120 warm-up and measurement frames,
and the duration floors above.

| Backend | Box edges | Extinction off GPU ms | Extinction on GPU ms | Added GPU ms |
| --- | --- | ---: | ---: | ---: |
| Dawn | Hard | 5.461 | 12.013 | 6.552 |
| Dawn | Faded | 4.062 | 13.680 | 9.618 |
| D3D12 | Hard | 1.838 | 3.850 | 2.012 |
| D3D12 | Faded | 1.822 | 4.799 | 2.977 |

These observations justified implementing an opt-in cache. Cadence remained
near 20 ms in these cases. The figures describe GPU work rather than an FPS
improvement. The shader audit also identified repeated prefix integration in
the raster camera grid. It executes 136 or 272 camera steps per column at
16 or 32 steps respectively. The compute producer executes 16 or 32.

The same screening isolated the optional geometric soft-shadow filter. Dawn
added about 0.297 ms for hard boxes and 0.085 ms for faded boxes. D3D12 added
0.057 ms for hard boxes; its faded comparison differed by -0.011 ms, which
does not establish a meaningful filtering cost. These are single ABBA blocks,
not universal backend costs. The optical cache preserves geometric filtering
and camera integration work.

## Cache decision on the tested desktop hardware

The final 16-step comparisons below use one complete ABBA block per workload.
Values are paired window GPU means in milliseconds, direct → cached. Browser
runs use the Intel gen-12lp adapter (the host's UHD 770); Windows runs use the
RTX 5060 Ti. Browser and native timing scopes differ as described above.
Compare treatments within each cell, rather than ranking backends by these
different scopes and adapters. Disabled WebGPU/Dawn shader variants compile
cache sampling out, preserving the fast direct reference.

| Workload | Browser WebGPU | Browser WebGL2 | Windows Dawn | Windows D3D12 |
| --- | ---: | ---: | ---: | ---: |
| One-light grid | 1.266 → 1.904 | 3.084 → 3.296 | 0.053 → 0.089 | 0.072 → 0.081 |
| Four-light grid | 1.446 → 2.257 | 9.184 → 10.465 | 0.098 → 0.127 | 0.140 → 0.170 |
| Four hard boxes | 92.369 → 144.556 | 57.505 → 61.979 | 12.012 → 16.216 | 4.052 → 3.338 |
| Surface extinction | 29.515 → 41.463 | 23.375 → 24.422 | 1.069 → 1.055 | 1.156 → 1.129 |
| Planar reflection | 11.477 → 14.179 | 7.865 → 8.820 | 0.191 → 0.253 | 0.237 → 0.261 |
| Four faded boxes | 122.882 → 144.638 | 78.288 → 62.020 | 13.314 → 17.408 | 5.236 → 3.362 |

These results justify an optional cache for heavy faded-overlap workloads on
WebGL2 and D3D12. They do not justify enabling it generally. Keep it disabled
on the tested WebGPU/Dawn configurations and on cheap grids unless an intended
scene's own measurements establish a benefit. Hardware support establishes
functionality, not a performance gain. Camera integration, geometric shadow
sampling, field lookup, allocation and rebuilding still cost GPU work.

Two ABBA repeats of four faded boxes at 32 steps produced these paired
window statistics. The percentage describes GPU duration, not FPS.

| Backend | Mean direct → cached ms | Window p95 direct → cached ms | Mean change | GPU samples per window |
| --- | ---: | ---: | ---: | ---: |
| Browser WebGPU | 231.488 → 271.623 | 231.653 → 271.827 | +17.3% | 15–16 |
| Browser WebGL2 | 130.566 → 101.364 | 130.702 → 101.440 | -22.4% | 19–29 |
| Windows Dawn | 21.078 → 32.724 | 25.723 → 33.751 | +55.3% | 31–45 |
| Windows D3D12 | 8.978 → 5.966 | 9.383 → 6.248 | -33.5% | 42–45 |

Both repeats agreed on the direction of each result. WebGL2 reported no
disjoint or dropped queries. Native D3D12 reported no dropped samples.
One moving-light ABBA block at 16 steps included rebuilding every frame.
Its direct → cached means were 122.894 → 146.761 ms for browser WebGPU,
69.691 → 55.759 ms for WebGL2, 13.273 → 17.510 ms for Dawn and
5.259 → 3.411 ms for D3D12. The favorable WebGL2/D3D12 decisions therefore
also held in this moving-light fixture.

Static and moving results include their exported GPU scopes. Cold first-step
CPU and wall reports also include pipeline/resource creation and are not
isolated GPU construction times. Inspect the retained D3D12 `Fog light cache`
spans for its producer cost. Frame cadence often remained presentation-limited
on native Windows; these results do not establish sustained FPS gains.

Raw final static reports are retained under
`build/evidence/fog-profile/texture-browser-static/` and
`build/evidence/fog-profile/texture-native-static-final/`. Tested artifact
hashes and host information are in `build/evidence/fog-profile/final-build.json`.
Repeated and moving reports are in the corresponding `texture-browser-stress32`,
`texture-browser-motion`, `texture-native-stress32-final` and
`texture-native-motion-final` directories. Some completed native JSON/JSONL
artifacts are losslessly gzip-compressed to preserve disk space. `summarize`
accepts `.json.gz` and `.jsonl.gz`, finds compressed reports in directories,
and honors the measurement floors recorded in a native raw header.

## Image and lifetime checks

Run `node scripts/fog-cache-smoke.cjs browser` and
`node scripts/fog-cache-smoke.cjs windows <webengine_lighting_windows.exe>`.
The fixtures compare direct and cached images for every scenario and both
step budgets. They also check stable reuse, camera movement, optical edits,
light movement, view opt-out, zero steps, cache disable and browser recovery.
Each browser backend gets an independent browser process so deliberate GPU
recovery cannot affect the following backend's startup. `FOG_CACHE_BACKENDS`
can select `webgpu` or `webgl2` for a focused rerun.
`node scripts/fog-cache-fallback-smoke.cjs` additionally forces the adapter's
reported sampled-texture limit to 16, verifies a default-limit WebGPU device
and renders the cached policy through direct integration. This is a controlled
capability test on the desktop adapter, not a physical low-limit device test.
Reported image error includes mean and maximum display and linear-light
differences and the footprint of large errors. Thin media and sharp boundaries
can expose lattice approximation.

The 2026-11-07 final run passed 20 direct/cache pairs per backend (80 total),
with a maximum error of one display code. Both browser recovery pairs matched
exactly. Camera reuse, light/density edits and all quality/view fallback stages
passed with effective cache-slot assertions. Native effective-slot assertions
were additionally replayed against all 48 retained diagnostics per backend.
Dedicated water checks passed perspective, orthographic and 4× MSAA on all
four backends; existing grid/direct/reflection self-shadow checks passed too.
The forced 16-texture WebGPU capability test passed through direct integration.
Focused native fog, render-quality and performance unit tests passed (3/3).

Use `fogLightCacheBuilds` for current-frame builds,
`totalFogLightCacheBuilds` for builds retained by the shared measurement
window, and `fogLightCacheBytes` for cache allocation. The cumulative field
retains builds that occur between the browser's 250 ms diagnostic reports.
Cache memory, creation time and moving-light rebuilds belong in the decision
to enable the feature for a scene.

`fogLightCacheSlots` reports the effective cached spotlight slots consumed by
the current frame. It stays zero while optional creation is pending or the
renderer selects direct integration. The profiling harness waits for a
positive value before starting a cache measurement window.

Physical Android and iOS performance requires measurements on those devices.
