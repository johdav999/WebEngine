# Height fog validation — 2026-10-16

Implemented and tested the material-shader fog contract in [fog.md](fog.md).
This report covers the development WebEngine checkout, not a CatGame engine pin.

## Build identity

- Local Emscripten **6.0.11**, Release build `build/web-fog-release`.
- Chrome **155.0.8059.80**, Windows desktop, forced WebGPU and WebGL 2.
- Main Wasm SHA-256: `bd7e27b661c2707ded6c88366925c7ca5b4fdd2ffc5036c1af3a8bbc1ea2276e`.
- Worker Wasm SHA-256: `b5385bcaa295ad75effde50f78e8aa8a8c8eb18ebe32cbbc4eb8e3cd4ccb2a34`.
- HTTP-served main/worker JS/Wasm and fog-control JS matched `dist` bytes.
  Hashes are in `build/evidence/fog/build-identity.json`.

## Executed checks

All eight focused native suites passed: `fog`, `sky`, `water`, `scene_archive`,
`persistence`, `render_frame`, `render_view`, `fx`. The native log is retained in
`build/evidence/fog/native-tests.log`. Fog checks compare analytic and numerical
integration, height/distance/start behavior, extreme-height stability, invalid
settings preserving state, orthographic/view opt-out payloads, save/load and clone.
Existing sky/water/persistence tests also exercise older archive layouts.

`scripts/fog-smoke.cjs` passed on both backends with no console/page/GPU errors:

- Actual near/far and low/high marker attenuation, alpha surfaces and particles.
- Background mesh and Overlay pixels unchanged, and start-distance exclusion.
- Invalid input preserved the rendered scene; dense fog extinguished additive
  particles without adding haze-colored light.
- 1x/4x MSAA, perspective/orthographic and ordered per-view fog opt-out.
- Resize, paused GPU/context recovery and v16 scene save/load preserved pixels.
- Real fog checkbox and water/underwater composition.

The height test changed 600,472 pixels on each backend. Near/far uniform-fog red
samples were 116/100 on WebGPU and 115/100 on WebGL 2; the elevated marker retained
more light. These are scoped image checks, not image-equivalence or performance
claims. Screenshots/results are in `build/evidence/fog/`.

Existing regressions passed on this same build:

| Suite | Scope |
| --- | --- |
| `water-planar-smoke.cjs` | Both backends: reflection pixels/clipping, MSAA, cameras, resize/recovery and archives |
| `water-underwater-smoke.cjs` | Both backends: depth/absorption, caustic-compatible composition, layer isolation, optional pass/storage, paused uniform caching and archives |
| `terrain-smoke.cjs` | Both backends: rendered terrain, source/collision queries, import, archive/reload and demo controls |
| `sky-weather-smoke.cjs` | Both backends: sphere/fullscreen, orthographic/multiple cameras, atmosphere/clouds/cycle and recovery |
| `material-smoke.cjs` | WebGPU: PBR/unlit material variants, alpha/additive, normals, skeletal/mirrored paths, IBL, edits and caching |

Archive assertions and older synthesized fixtures were adjusted for v16. Native
and browser suites were rerun after those corrections. Documentation/link checks,
JavaScript syntax checks and the focused tracked-file whitespace check passed.
Installed and CatGame-local engine skills and their three affected references were
updated and verified; CatGame's dependency/pin guidance was preserved.

No physical Android/iOS, non-Chromium browser, sustained mobile performance or
coupled volumetric air/water transport acceptance is claimed. Local volumes and
volumetric lighting are outside this implementation.
