# Direct D3D12 milestone 3 validation

Milestone 3 adds sky, analytic height fog and HDR bloom/color grading to the
selectable native renderer. It reuses the existing Scene/RenderQuality APIs;
portable ownership, archive format and browser rendering implementations stay
unchanged. See [scope and reproduction](windows-d3d12.md).

## Native Windows

Validated 2026-10-22 on Windows x64 MSVC Release, Visual Studio 18 2026,
Windows SDK 10.0.26100.0, NVIDIA RTX 5060 Ti, SDL 3.4.18 and pinned DXC.
`WEBENGINE_D3D12_VALIDATION=1` enabled the actual D3D12 debug layer, with no
reported ERROR/CORRUPTION messages. FP16 HDR and independent HDR 4x MSAA support
were available on this adapter.

`scripts/windows-environment-smoke.ps1` exercises both native renderers through
the same portable static gallery and writes PPM/PNG captures, per-case state/pass
counters, verified JSON and binary hashes under
`build/evidence/d3d12-m3/final-dual/{dawn,d3d12}/`. Its 36 paired cases cover:

- Gradient, inward sphere, brightness, sRGB panorama/rotation, atmosphere,
  clouds, explicitly advanced wind and night sky.
- Height/uniform/start-distance fog, per-view sky/fog opt-outs, perspective and
  orthographic sphere equivalence, Overlay exclusion and additive mesh extinction.
- HDR with zero bloom, bloom/threshold, exposure, zero saturation, contrast and
  retention after invalid quality/sky/fog candidates. GUI pixel colors remain
  identical under scene fog, bloom and grading.
- HDR with 4x MSAA, odd 803x603 resize, 0.75 resolution scaling to 602x452,
  split views, a preserving-color masked camera, borderless fullscreen and return
  to the exact ordinary-rendering baseline.

The standard-library pixel verifier compares each matched image with a mean
absolute RGB-channel difference limit of .75 on the 0–255 scale. Per-capture
measurements are in `final-dual/comparison.json`. Pass counters prove that zero
bloom skips extraction, split views compose once, and post disable removes both
passes. This is local fixture agreement, not bit-identical rendering or full parity.

The existing static material/lighting suite passed on Dawn and direct D3D12 under
`build/evidence/d3d12-m3/static-regression/`, with a maximum mean channel difference
of .213. The shared vehicle/GUI/resize/fullscreen/capture suite passed on both
backends under `build/evidence/d3d12-m3/vehicle-regression/`. CTest
`desktop_api`, `desktop_api_d3d12`, `render_quality` and `adaptive_quality` passed.
The interactive gallery was captured and visually inspected; both retained
control panels fit the 1280x720 view, and the world shows sky and a bright bloom
source. Images are `build/evidence/d3d12-m3/gallery-{dawn,d3d12}.png`.

A direct-only configuration (`WEBENGINE_WINDOWS_DAWN=OFF`, D3D12 ON) also built
and ran the environment/pixel suite and `desktop_api_d3d12`; its evidence is
`build/evidence/d3d12-m3/direct-only/`. Normal handoff configuration enables both
backends. Direct-only selection does not load or link the Dawn renderer.

## Browser

The repository-local Emscripten 6.0.11 Release build was configured/built and
published into a fresh frozen `build/d3d12-m3-browser-site`, served on port 8349.
The actual served `webengine.wasm` SHA-256 is
`EA33075374476F5AEFD8F3C0F52FA58D1090AF3E54AD76D2DDB08AC3F319EC9F`.
The browser shader/runtime sources were unchanged, so this hash matches the
previous artifact; the following are new acceptance runs for this milestone.

Desktop Chrome 155.0.8059.85 passed each suite with actual forced WebGPU and
WebGL2, with empty recorded error arrays:

- `lighting-post-smoke.cjs`: spot/cascade shadows, HDR bloom, exposure/grayscale,
  invalid-setting retention, disable restoration, MSAA, resize and GPU recovery.
- `sky-smoke.cjs`: gradient/panorama, rotation/brightness, translation, view
  scissor/opt-out, orthographic rendering, layers, rejection, resize/recovery and
  panorama upload UI.
- `sky-weather-smoke.cjs`: sky sphere, atmosphere, cloud phase and day/night.
- `fog-smoke.cjs`: distance/height/start fog, layer exclusions, alpha/particle/
  additive behavior, per-view flags, MSAA, resize/recovery, archive round trip and
  water/underwater UI composition. Its stale exact archive-version check was
  corrected to accept v19-or-newer using the complete little-endian version.

Reports and images live in `build/evidence/d3d12-m3/browser-post/` and
`browser-{sky-smoke,sky-weather-smoke,fog-smoke}/`. WebGL2 retains basic material
lighting; these results do not establish its PBR/IBL support.

## Limits

HDR scene rendering still presents an SDR RGBA8 swapchain. No HDR10 monitor mode,
LUT grading, volumetric fog, new sky simulation or reflection capture is added.
Static World meshes and sky have direct fog; direct water, skin/morph deformation,
layered terrain, particles/ribbons, GPU instancing/compression and device recovery
remain outside scope. Existing browser/Dawn coverage of those features is separate.
Unsupported FP16/HDR-MSAA hardware fallback is queried and reported but was not
physically exercised on this supporting adapter. No other GPU vendor, physical
mobile device, mixed-DPI display, TDR/recovery or performance claim is established.
