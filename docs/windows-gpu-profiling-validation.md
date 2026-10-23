# Direct D3D12 GPU profiling validation

Verified 2026-10-22 in this working checkout. See [contracts](windows-gpu-profiling.md).
Evidence is under `build/evidence/gpu-profile`; these are new runs, separate from
the earlier direct-renderer milestone reports.

## Native Windows

MSVC x64 Release on the installed Windows graphics stack; direct adapter:
NVIDIA GeForce RTX 5060 Ti. `WEBENGINE_D3D12_VALIDATION=1` enabled the real D3D12 debug layer;
no ERROR/CORRUPTION messages were reported.

| Configuration | GPU profiling on/off | Focused tests |
| --- | --- | --- |
| Both backends compiled, explicit direct D3D12 | Seven cases per mode, exact PPM equality | Six of six CTest tests passed |
| Both backends compiled, explicit Dawn | Seven cases per mode, exact PPM equality | Same six-test suite |
| Dawn disabled, direct D3D12 only | Seven cases per mode, exact PPM equality | Five of five CTest tests passed |

Cases cover above-water, underwater, split cameras, 703x503 resize, actual
selected-device removal/recovery, zero bloom and non-HDR 1x scene copy. Each
includes retained GUI and asynchronous framebuffer capture. The clock remains
paused across recovery. New samples identify their own extent/MSAA/HDR/frame.

Direct query evidence confirms nonnegative nested intervals, parent containment,
self-time partitioning, real queue frequency (1000000000 Hz), per-view and
per-shadow attribution, opaque/transparent FX, reflection/refraction/underwater,
sky, MSAA, bloom/composition, GUI and sampled framebuffer-copy scopes. Each
enabled direct run exceeded 120 samples: the final report retains exactly 120
from 125 completed samples, with zero invalid samples and no dropped spans.
Capture-copy evidence is saved before intentional rolling eviction.

Disabled runs report null GPU durations, zero samples and zero query-readback
bytes, while maintaining identical pixels. Scene/view/quality/attachment resets
and device replacement omit stale-epoch history; shutdown exports an empty trace.
CPU trace exports include the direct renderer's owner-thread scope. Trace timestamps
are GPU-relative and remain separate from CPU timing.

The focused native tests are performance, performance_timeline, desktop_recovery,
desktop_fallback and desktop_api_d3d12, plus desktop_api in the Dawn-enabled build.
The smoke target is `windows_gpu_profile_smoke`; use
`scripts/windows-gpu-profile-smoke.ps1` to reproduce enabled/disabled comparisons.

## Browser

Fresh repository-local Emscripten 6.0.11 Release build, served on port 8359 and
verified byte-for-byte against the frozen artifact. Wasm SHA256:
`ec6f562adbc6b278d2a854d58a3a244bb016d7fce47db1a4c229da8de8c1cb54`.

- `performance-smoke.cjs`: actual asynchronous WebGPU timestamps, unchanged shared
  summary schema, CPU windows, reset, physically saved JSON export, asset stages,
  explicit GPU timing off and simulated absent optional timestamp support passed.
- `performance-timeline-smoke.cjs`: WebGPU and forced WebGL2 animation, physics/Jolt,
  AI and import spans, hierarchy/self times, pause/resume, filters, pinning, bounded
  history, trace export and compact controls passed.
- `render-fallback-smoke.cjs`: forced WebGL2 context recovery, GUI/input, skeletal
  animation/morph/FX/views and automatic API/adapter/device-failure fallback passed.

All three suites report no browser errors. The performance fixture explicitly
disables sky drawing, counts its retained 126768-byte sphere, and preserves the
fleet fixture's retained shared textures. Original exact geometry/submission
checks remain; no runtime cache policy changed for these test corrections.

Native executable/library hashes and the browser identity are saved beside the
reports. Both engine-skill copies are updated with the APIs, controls, limits,
timing definitions, build commands and separate native/browser validation guidance.

## Limits of this evidence

This verifies one installed GPU/Windows stack and desktop Chrome, not other
adapters, physical mobile devices, driver TDRs or sustained performance. Device
removal used the actual selected-device diagnostic, not an OS/driver reset.
Query allocation failure, readback failure, a deliberately saturated readback
ring and span overflow were not fault-injected on hardware. The bounded source
paths are present; their existence is not hardware fault-injection acceptance.
No CPU/GPU clock correlation, per-shader occupancy, GPU utilization or throughput
improvement is claimed. Full-frame and layer-only totals have different boundaries.
