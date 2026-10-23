# Direct D3D12 milestone 7 validation

Verified 2026-10-22 on this Windows x64 machine with an NVIDIA GeForce RTX 5060 Ti.
This record covers the delivered [native FX contract](windows-fx-rendering.md).
Earlier milestone records remain historical evidence.

## Native Windows

MSVC Release builds passed with both native renderers enabled and in a separate
`build/windows-direct-m7` configuration with Dawn disabled. DXC compiled all HLSL
entries with `-O3 -WX`. Explicit direct runs enabled
`WEBENGINE_D3D12_VALIDATION=1`; no ERROR/CORRUPTION messages were reported.

`windows_fx_smoke` and `verify_windows_fx.py` passed **102 framebuffer cases each**
on Dawn, direct D3D12 and the Dawn-free direct build. Evidence is under
`build/evidence/d3d12-m7/{dawn,d3d12,direct-only/d3d12}`. The paired Dawn/direct
maximum mean absolute channel difference was **0.2222222222 on a 0–255 scale**;
every case stayed below the verifier's 0.75 limit.

Verified actual pixels and corresponding counters for:

- Alpha/additive billboard depth, mixed mesh/quad sorting independent of creation
  order, Overlay ordering, velocity stretch and top-left flipbook frames.
- Perspective/orthographic soft fades, clear/far depth, alpha/additive emission,
  plane orientation, offset occlusion, ribbon and supplied-contact surface trails.
- Mesh geometry equivalence, material slots, texture inheritance/override, 3D
  rotation/spin, additive accumulation, masks and 128 live opaque mesh particles.
- Alpha fog haze and additive extinction, 1x/4x MSAA, HDR/bloom, ordered masked
  cameras, resize/restore, invalid-definition retention and scene destruction.
- Water/FX sharing one opaque snapshot, underwater/FX composition using two, and
  cached paused quads/ribbons uploading zero bytes on unchanged presentations.

The native gallery with retained preset/restart/playback/fog/HDR/MSAA controls was
captured and visually inspected on both backends. Captures include native text and
widgets. Fresh vehicle/GUI acceptance separately exercised native input, resize,
resolution changes, SDL borderless fullscreen and framebuffer capture lifecycle.

Fresh water regression passed 63 cases per backend (paired maximum mean difference
0.0759760200). Lighting/material/shadow/MSAA regression passed on both backends
(maximum 0.0043636068). Core/API CTest passed **9/9**: material, water, mesh_particle,
plane_particle, trail, fx, render_frame, desktop_api and desktop_api_d3d12. The
separate Dawn-free desktop_api_d3d12 test passed **1/1**.

Logs, reports, comparison JSON, framebuffer files, PNG previews and binary SHA256
records are under `build/evidence/d3d12-m7`; build/test logs use `build/d3d12-m7-*`.

## Browser

Built/published Release using repository-local Emscripten **6.0.11**, then froze
`build/d3d12-m7-browser-site` and served that directory on port 8357. The served
Wasm was checked byte-for-byte against the frozen output:

`4248bcb5a23d436ec71c094cee8d4f37f8b3c4699b93d01d669b6ebdcb0f5412`

All five suites passed on explicit WebGPU and WebGL2 in desktop Chrome 155:
`fx-milestone-smoke`, `trail-depth-smoke`, `plane-particle-smoke`,
`mesh-particle-smoke` and `surface-trail-smoke`. Their result JSON reports no browser
errors. Checks cover actual pixels, masks, resize, graphics recovery, current
archive round trips and representative effect/editor controls. Browser rendering
and shared FX simulation were unchanged by this native backend extension. Three
stale exact-v29 script assertions now accept the compatible current archive range
using a little-endian uint32 version read.

## Boundaries

This establishes the recorded desktop adapter's behavior, not other hardware,
physical mobile acceptance, sustained throughput or full renderer parity. General
opaque mesh instancing, compressed GPU uploads, detailed direct GPU timing and
direct device recovery remain outstanding. FX uses existing approximate center
sorting; soft depth excludes transparency and supplies no collision or volumetric
lighting. No new emitter, contact, archive or public renderer API was added.
