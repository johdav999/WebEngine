# Dense foliage validation — M6

Checks performed on 2026-10-28 in the shared checkout. This extends the
[M1–5 evidence](foliage-validation.md); those earlier results are historical.
Contracts, API defaults, build commands and limits are in [foliage.md](foliage.md).

## Contracts and builds

- Windows x64 Release, VS2022/MSVC 19.44, direct D3D12 enabled and Dawn disabled:
  `foliage_tests`, `foliage_demo_windows`, `render_frame_tests`,
  `frustum_culling_tests`, `selection_tests`, `instancing_tests` and
  `transform_cache_tests` built. The six selected CTest cases passed **6/6**.
- Repository-local Emscripten **6.0.11**, Release: `foliage_tests` and
  `foliage_demo` built; `foliage_contracts` passed **1/1** in 0.98 seconds.
- New HLSL wind/color/shadow entries compile with pinned DXC and are embedded
  in the native executable. WebGL2 shader link and WebGPU WGSL compilation passed.

The extended shared contract fixture checks authored weights/surface gradients,
immutable instance-set rejection, instance-key triangle picking, conservative
wind bounds, color/shadow batch participation, unchanged wind-only object uploads,
pause, transient archives and an exact **10,000-instance** cell represented by one
bulk component and one compatible batch without camera clipping. It also checks
GPU harvesting, eviction/re-entry, invalid save rollback and CPU-reference save/ID
compatibility. The original generation, streaming, collision and CPU wind cases
remain in the same test executable.

## Browser rendering

Headless Microsoft Edge **156.0.4314.50**, Playwright, 1100×750, actual WebGL2 and
WebGPU contexts. `scripts/foliage-browser-smoke.cjs` passes on both backends:

- Nine visual and nine independent collision cells: initially 423 plants,
  54 bulk components and 23 trunk shapes; zero CPU-deformed vertices.
- Visible wind, explicit pause/resume, harvest/save/regrow/restore, rejected
  incompatible saves, visual focus changes without collision changes and reload
  persistence all pass.
- Frozen instancing on/off image comparison and induced context/device loss
  restore the same paused foliage within the 100-pixel acceptance threshold.
  No JavaScript, renderer or GL validation errors were recorded.
- Wind changed 57,966 pixels on WebGL2 and 36,732 on WebGPU. Streaming changed
  98,224 and 93,009 respectively. These counts depend on the sampled wind time.

The WebGL backend packs portable object staging into 176-byte attribute records
because WebGL limits stride to 255 bytes. Context-loss checks also cover the
interval before the asynchronous lost-context event reaches the renderer.

## Fixed-seed dense comparison

`scripts/foliage-dense-smoke.cjs` and the native example's `--benchmark` use the
same seed, flat 100×100-metre source and **10,000 eight-vertex masked clumps**.
The benchmark disables shadows and uses unlit foliage to isolate geometry parity
from CPU triangle normals versus GPU gradient-based normals. The normal clearing
separately exercises shadow rendering. Fixed-time images use **0.75 seconds**.
Camera clipping splits visible runs, so rendered draw counts exceed one; every
resident plant still belongs to one bulk component. Geometry draw counts include
the floor. The CPU reference uses independently deformed per-plant streams.

| Backend | Mode | CPU callback mean / p95 (ms) | Geometry draw calls | Changing geometry/wind bytes per warm frame | GPU mean / p95 (ms) |
| --- | --- | ---: | ---: | ---: | ---: |
| Browser WebGL2 | GPU instances | 48.55 / 51.80 | 28 | 32 | unsupported |
| Browser WebGL2 | CPU reference | 667.83 / 731.70 | 9,736 | 3,738,240 | unsupported |
| Browser WebGPU | GPU instances | 50.12 / 63.00 | 28 | 32 | 2.00 / 2.22 |
| Browser WebGPU | CPU reference | 322.83 / 353.60 | 9,738 | 3,840,000 | 4.73 / 6.57 |
| Native D3D12 | GPU instances | see wind-only timing below | 29 | 32 | 7.14 / 8.00 |
| Native D3D12 | CPU reference | see wind-only timing below | 9,737 | 3,738,624 | 169.42 / 172.37 |

Browser CPU callbacks include application advance and renderer submission,
excluding fixture state export; 30 samples per mode. Native measured the wind
presentation call separately over 60 ticks: GPU **0.003805 / 0.004400 ms** versus
CPU **16.621 / 19.4612 ms** mean/p95. These CPU metrics measure different scopes;
do not compare native wind-only timing with the browser's full callback timing.
All GPU modes report **zero changed object uploads** on warm wind-only frames.
GPU timestamps are the existing renderer's asynchronous rolling measurements,
with cold samples retained; they are not synchronized per-callback measurements.

Frozen CPU/GPU image differences above a summed RGB threshold of 25 were:
WebGL2 **2 pixels**, WebGPU **1 pixel**, native D3D12 **2 pixels**. Each is below
0.001% of the frame; the acceptance limit is 1%. Native adapter diagnostics report
Intel Iris Xe hardware D3D12. These are local development measurements with
different backend render/material costs, not a controlled cross-backend ranking
or a sustained frame-rate guarantee. The browser's roughly 50 ms CPU callback
also makes the remaining per-plant descriptor/culling cost visible.

## Native rendering and recovery

The real hidden SDL/Win32 D3D12 clearing smoke passes pause, harvesting/storage,
independent visual/collision streaming, wind disable/resume and shutdown.
`scripts/foliage-native-pixels.cjs` converts PPM captures and verifies visible
changes plus dense CPU/GPU parity. The smoke also removes the selected D3D12
device while paused, reacquires generation 2 and verifies the same scene,
timestamp, motion and captured foliage after GPU resource reconstruction.
The paused before/recovered native captures match exactly (**0 changed pixels**).

The direct D3D12 CPU-reference pose-cache bound is raised from 4,096 to 16,384
entries to admit the 10,000-plant diagnostic; its upload and draw budgets remain
bounded. GPU wind uses shared immutable bend buffers and one revision-cached
32-byte wind buffer instead of those per-plant pose buffers.

## Evidence and remaining coverage

Ignored local evidence is under `build/foliage-m6-browser`,
`build/foliage-m6-dense`, `build/foliage-m6-native`,
`build/foliage-m6-native-gpu` and `build/foliage-m6-native-cpu`.
The checked-in smoke scripts reproduce the scenarios.

Native Dawn was **not built or run for M6**. The validated native configuration
uses direct D3D12; browser WebGPU shares the WGSL implementation but does not
establish native Dawn acceptance. Physical Android/iOS, sustained mobile load,
larger species/content sets and broad GPU/driver coverage remain unverified.
This milestone does not add GPU culling, indirect submission, compact GPU TRS
records, interactive bending, impostors or automatic foliage authoring.

Repository and current-user installed engine skills were updated and validated.
The two AGENTS-required Johan/CatGame skill folders still reject writes under the
current Windows token despite user authorization. Complete merged copies are
prepared in `build/foliage-skill-update/johan-installed` and `catgame`; their skill
frontmatter and reference hashes validate, with original guidance preserved.
Installing those two documentation copies remains outstanding.
