# Rendering fallback

The browser renderer supports WebGPU and WebGL 2. `Renderer` is a facade with
no WebGPU, GLES or Emscripten types in its public header. The implementations
and their GPU resources live in `src/webgpu_renderer.*` and
`src/webgl_renderer.*`; both compile into the browser executable. Native builds
still supply the portable core and tests, rather than a native graphics backend.

## Selection and failure

```cpp
engine::Renderer renderer(scene, camera);
renderer.initialize(); // Automatic: prefer WebGPU, fall back to WebGL 2.
// Alternatively initialize with RenderBackendPreference::WebGpu or ::WebGl2.
```

Automatic mode selects WebGL 2 when the WebGPU API/secure context is absent,
when initial adapter/device acquisition fails, or when initial acquisition has
not completed within ten seconds. WebGPU surface creation is delayed until a
device is acquired. A fallback switch happens in `serviceRecovery()` at a host
frame boundary; it invalidates outstanding WebGPU callbacks and releases its
resources before creating the WebGL context. Late acquisition cannot revive
the abandoned backend. The host must service recovery even while not ready.

The playground supplies the optional preference from `?renderer=webgl2` or
`?renderer=webgpu`. Omitting it uses automatic mode. Query parsing is application
policy; the reusable facade accepts the explicit enum. Repeated initialization
does not change an acquired backend. Requiring WebGPU disables automatic fallback.
If neither backend can initialize, the page reports a clear failure and offers
retry; it does not publish rendered frames.

A canvas cannot acquire both WebGPU and WebGL contexts. Once WebGPU acquires
the canvas, later loss/exhausted recovery retains its existing bounded WebGPU
reacquisition and explicit retry. It does not replace the DOM canvas, reload the
page or lose input listeners to switch backends. WebGPU surface/pipeline failures
after acquisition also retain this recovery path. WebGL 1 is not supported.

## Delivered fallback capabilities

| Contract | WebGPU | WebGL 2 |
| --- | --- | --- |
| Existing scene hierarchy, cameras, masks, frustum culling and camera LOD | Supported | Supported |
| Layered terrain and seamless chunk camera LOD | Four albedo/roughness/packed normal-height layers, RGBA weights, height blending, optional world triplanar, independent shadow detail | Same color/alpha, layer normals, height blending, projection and chunk LOD with basic lighting; shadow maps remain unavailable |
| Terrain sculpt/paint and bounded derived streaming | Shared portable edit/chunk/body ownership; cached meshes/weights refresh | Same portable edits, resident geometry/collision and weight refresh; existing basic lighting limits apply |
| Terrain source paging / asynchronous generation | Isolated workers fetch/decode/generate bounded patches; owner publishes ordinary meshes and optional collision | Same worker/source/residency contracts and recovery; basic lighting and no shadow maps |
| Paged terrain sculpt/paint and dirty writeback | Atomic source/halo edits, affected rebuilds, shared paint geometry and IndexedDB generation-checked batches | Same portable edit/residency/writeback contracts and recovery; lighting/shadow limits unchanged |
| Ordered views, Background/World/Overlay and canvas GUI | Supported | Supported |
| Fullscreen/mesh sphere skies, gradient/panorama/atmosphere, clouds and day/night light updates | Supported | Supported (IBL remains unavailable) |
| RGBA textures, UV transforms, color/opacity, alpha mask, double-sided/mirrored meshes | Supported | Supported |
| Skeletal/morph geometry | GPU skinning, morph streams | CPU deformation into backend vertex buffers |
| Alpha/additive particles, transparent mesh ordering and flipbooks | Supported | Supported |
| Shaped CPU emission, drag/wind/gusts, vector curves, velocity stretch and soft particles | Supported; instanced 80-byte billboards and resolved opaque depth | Same simulation and fading; CPU billboard vertices and individual draws |
| Lighting | PBR maps and environment lighting | Ambient, directional/point diffuse lighting, unlit and emissive factors |
| Reflective animated water | Gerstner waves, ripple normals, Fresnel, highlights, optional IBL | Same waves/ripples, analytic horizon/zenith reflection and highlights |
| Underwater rendering | Depth-based RGB absorption and fog, transformed footprint, post-World capture/composite, 1x/4x | Same behavior using RGB8/DEPTH_COMPONENT24 snapshots and GLSL reconstruction |
| Interaction ripples and wakes | Up to 16 world-space ring/V sources; normals and crest highlights, caller clock | Same bounded field and per-camera masks in fragment uniforms |
| Underwater caustics | Procedural depth-reconstructed receiver lighting before fog, shared water clock | Same cellular pattern, depth fade and receiver rejection in GLSL |
| Planar water reflections | One single-sample opaque/masked World capture per view, transformed plane clipping and mirrored winding | Same bounded capture, RGBA8 color and DEPTH_COMPONENT24 depth |
| Depth-aware water refraction/absorption and shoreline foam | Per-view opaque snapshot, 1x/4x color/min-depth capture | RGB8/DEPTH_COMPONENT24 snapshot and same-size color/depth blit, 1x/4x |
| Shadow maps and instancing | Supported | Unavailable |
| Authored BC7/ETC2/ASTC uploads | When device supports them | Retained RGBA pixels |
| GPU timestamp measurements | When supported/enabled | Unavailable |

`RenderCapabilities` identifies the backend and shadow/PBR/instancing support.
The inspector and performance report include backend identity, effective limits,
canvas extent and capability flags. Unsupported shadow/compression controls are
disabled in the fallback UI. Water has its own optional normal/radiance/highlight path; see [water contracts](water.md).
Full PBR maps, environment lighting and shadow policy
do not affect fallback pixels. Applications should choose whether its basic
lighting meets their visual requirements.

The fallback consumes the same `RenderFrame`, `FxFrame`, shared immutable assets,
GUI draw list and camera coordinate conventions. Its vertex shader maps the
engine's zero-to-one clip depth into WebGL's minus-one-to-one range. Scissor
coordinates convert the shared top-left viewports to GL coordinates. Each layer
clears depth within the active view; later layers compose above previous layers.
GUI renders once afterward with the existing logical/CSS coordinate conversion.

The fallback submits individual draws and uploads CPU-deformed geometry; it does
not claim WebGPU's batching or changed-only skeletal upload costs. Performance
draw/triangle counts reflect submitted work. Fallback upload counters count buffer
and texture payload uploads; they do not count GL uniform calls. Encoding CPU time
includes fallback resource preparation. GPU durations remain unavailable/null.

## Ownership and restoration

The caller owns scenes, cameras and GUI and keeps active references alive through
renderer use. The facade forwards scene/view/quality controls with validation
before publication. Neither backend advances animation, changes authoritative
simulation transforms or constructs replacement scenes.

WebGL context loss prevents submission and cancels held browser input through
the existing quality/lifecycle gate. Its callback drops invalid GPU handles while
retaining CPU resources/settings. Context restoration queues rebuilding at the
next host frame boundary. Programs, buffers and textures rebuild/lazily upload;
pause state, scene handles, animation and GUI state survive. Renderer destruction
unregisters context callbacks before releasing resources and destroying its
context. The existing composed visibility/freeze/page-away suspension contract
continues to govern simulation.

## Build and verification

Use the pinned repository-local Emscripten 6.0.11 setup from the README. The browser
target links Emdawnwebgpu and `-sMIN_WEBGL_VERSION=2 -sMAX_WEBGL_VERSION=2`.
WebGPU shaders retain generated WGSL headers; private fallback shaders are GLSL ES
3 in `webgl_renderer.cpp`. The usual build publishes both backends in one Wasm
artifact, without an additional network-loaded graphics library.

`scripts/render-fallback-smoke.cjs` uses the README's Playwright/pngjs environment:

```powershell
$env:DEMO_URL = 'http://localhost:8117'
$env:BROWSER_EXECUTABLE = 'C:/Program Files/Google/Chrome/Application/chrome.exe'
$env:NODE_PATH = 'path/to/test/node_modules'
node scripts/render-fallback-smoke.cjs
```

The suite checks real textured canvas pixels, light/orbit controls, validated
resolution budgets, real context loss/restoration with identical paused pixels
and clocks, GUI text input/restoration, skeletal and morph output, particles and
compositing. Controlled probes exercise missing API, null adapter, rejected device,
timeout/late acquisition, required WebGPU and both backends unavailable. Existing
browser and quality suites now expect WebGL 2 when WebGPU is missing.

Verified on 2026-10-10 with Emscripten 6.0.11 Release and Chrome 154.0.8037.99:
the isolated build is `build/web-fallback-release`, with its staged site in
`build/fallback-browser-site`. [Fallback results and paired PNGs](../build/evidence/render-fallback/results.json)
record the executed cases. The existing [WebGPU quality/recovery suite](../build/evidence/fallback-webgpu-quality/results.json)
passed eight groups without page/console errors. The existing
[browser rendering/import/input suite](../build/evidence/fallback-browser-regression/browser-smoke.json)
passed fourteen groups without page/console errors. Ten focused native Debug suites
passed in `build/quality-native`: quality, views, frame preparation, cameras,
frustum culling, animation, morphs, particles, materials and GUI. Native checks
used the configured compiler's matching runtime directory on PATH.

The verified staged Wasm SHA256 is
`82880208F4E4EC9E667DACEDC562F7C2547637E4D12B3616DF7A721C8E310EAB`.

These checks establish this Chrome/GPU execution and controlled acquisition
conditions. They do not establish physical Android/iOS acceptance, a Safari/Firefox
matrix or sustained mobile performance. Capability and visual targets on those
devices still require device-specific evidence.
