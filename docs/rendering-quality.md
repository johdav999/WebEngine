# Rendering quality and browser resilience

`RenderQuality`, `ResolutionBudget`, `RenderCapabilities` and `resolveResolution`
in [render_quality.hpp](../include/engine/render_quality.hpp) are portable value
contracts. They contain no browser or WebGPU types. Applications choose a
profile or supply a validated custom budget; these are explicit settings, not
an FPS-based adaptive policy or device-performance classification.

| Profile | Scale / maximum DPR | Maximum dimension / pixels | Shadow resolution / samples / maps |
| --- | --- | --- | --- |
| Low | 0.75 / 1 | 2048 / 2,073,600 | 512 / 1 / 1 |
| Balanced (default) | 1 / 2 | 4096 / 16,777,216 | 1024 / 9 / 4 |
| High | 1 / 2 | 8192 / 33,554,432 | 2048 / 9 / 4 |

`Renderer::setQuality` validates the complete candidate before changing settings.
Resolution scale is 0.25–2, maximum pixel ratio 0.5–4, maximum dimension 1–16384,
and maximum pixels 1–268435456. Both axes shrink by the same factor to meet the
pixel and dimension budgets and the actual device texture limit. Integer rounding
can introduce a subpixel aspect difference. CSS layout, pointer coordinates and
simulation state retain their own contracts. DPR changes, resize and restoration
are evaluated against the same policy. Shadow resolution remains 256/512/1024/2048,
filtering 1/9 comparison samples, and map count 0–4 per view. Maps use the first
eligible lights in scene order. Zero maps skips shadow rendering/receiving; one
small depth-array layer remains bound because material pipelines require a valid
shadow binding. Other counts allocate only the configured number of layers.

```cpp
auto quality = engine::RenderQuality::profile(engine::QualityProfile::Low);
quality.resolution.maxPixels = 1280 * 720;
quality.maxShadowMaps = 0;
renderer.setQuality(quality);
```

The playground's **Rendering quality** panel selects profiles and custom budgets;
`?quality=low|balanced|high` selects the initial profile. Existing mesh-detail and
shadow controls remain available. LOD uses the existing immutable asset chains,
per-camera/per-light selection and hysteresis; enabling it does not generate
geometry or change collision/simulation. See [mesh detail](mesh-detail-validation.md).
Settings are runtime presentation policy, not scene archive data or saved preferences.

## Optional MSAA

`RenderQuality::msaaSamples` accepts 1 (off) or 4. All profiles default to 1;
applications opt into 4x independently of resolution or shadow filtering:

```cpp
auto quality = renderer.quality();
quality.msaaSamples = 4;
renderer.setQuality(quality);
auto samples = renderer.effectiveMsaaSamples();
```

Invalid counts reject the entire quality candidate. `RenderCapabilities::msaa4x`
reports attachment-format support after acquisition; `resolveMsaaSamples` selects
4 only when requested and supported. Unsupported devices retain the requested
policy and render at 1. Before attachments exist, `effectiveMsaaSamples()` returns
1; WebGPU applies sample changes at the next render boundary.

WebGPU owns multisampled color and matching `Depth24Plus` at the resolved canvas
extent. Scene material, skinning, particle, sky and viewport-clear pipelines match
the sample count. Background/World/Overlay and all camera views store/load the same
color, preserving ordered composition and independent per-layer depth. A final
resolve pass loads that color and resolves into the single-sample canvas before
the GUI. Empty view lists also initialize and resolve color. Shadow maps remain
single-sampled and material textures retain ordinary sampling. Binding layouts
remain compatible when scene pipelines change.

WebGL 2 queries RGB8 and DEPTH_COMPONENT24 renderbuffer sample counts. RGB8 matches
the opaque default canvas framebuffer required by its resolve. If both
support 4, it renders views/layers into a staged, completeness-checked multisampled
framebuffer, disables the view scissor, and resolves color with `glBlitFramebuffer`
before GUI. Unsupported 4x uses the default framebuffer. Allocation failure does
not publish an incomplete replacement framebuffer or invalid quality candidate.

Resize, sample changes and device/context restoration recreate attachments;
recovery retains requested quality and reevaluates support. `EngineQuality.snapshot()`
reports `msaaSamples`, `effectiveMsaaSamples` and `msaa4x`. The playground's
**Anti-aliasing** control selects Off/4x and marks custom quality. Selecting a profile
restores its one-sample default.

Performance data counts multisampled color in `gpuTextureBytes` and sample-scaled
depth in `gpuDepthBytes`; WebGL also estimates its default framebuffer depth.
MSAA adds one counted resolve pass. WebGPU timestamp totals cover scene/shadow
passes and exclude resolve and GUI. Storage estimates omit driver padding and
browser-owned color/swapchain allocations. Four samples increase memory/bandwidth
without implying a measured fourfold shading cost. Backing-resolution loss,
texture aliasing and specular shimmer remain separate quality concerns.

`scripts/msaa-smoke.cjs` checks real diagonal partial-coverage pixels at 1:1 backing
size, 1x/4x switching, invalid settings, public controls, split/loaded/empty views,
resize, storage estimates, paused recovery, GUI after resolve, skeletal/morph
deformation, rejected framebuffer publication and controlled unsupported WebGL
format support. `MSAA_SAMPLES=4` enables MSAA during existing
`compositing-smoke.cjs`, `sky-smoke.cjs` and `shadow-smoke.cjs` fixture suites.
Use the README's browser environment setup. Native `render_quality` checks validated counts and capability
selection. Physical mobile performance and browser/device coverage require
separate evidence.

### Verified MSAA evidence — 2026-10-14

- Pinned Emscripten 6.0.11 Release build: `build/web-msaa-release`; isolated
  site `build/msaa-browser-site` served on localhost port 8134 during validation.
- Native Debug CTest `render_quality` passed in `build/native`.
- Chrome 155.0.8059.79: `msaa-smoke.cjs` passed both real backends, including
  skeletal/morph deformation, profile reset, framebuffer rejection and recovery.
  At 640x480 backing pixels the diagonal fixture had 0 partial-coverage pixels at
  1x, 564 at WebGPU 4x and 440 at WebGL 2 4x. Backend sample patterns differ.
- Existing compositing and sky suites passed with `MSAA_SAMPLES=4`; shadow suite
  passed at both 1x and 4x. The shadow receiver crop now excludes the caster on
  both sides, and its known map-budget warning accepts the private renderer name.
- Results/screenshots: `build/evidence/msaa`, `msaa-compositing`, `msaa-sky`,
  `msaa-shadows-1x` and `msaa-shadows-4x` under `build/evidence/`.
- Tested Wasm SHA256:
  `7B2FDF2FC1F74EA6C3036C9AD64CD88D8F2163D356A925CE0A65E3098B4408AA`.

This establishes desktop Chrome correctness, not physical Android/iOS performance
or acceptance on other browsers. Concurrent checkout work was retained.

## Capabilities and recovery ownership

`Renderer::capabilities()` reports the active device's maximum 2D texture dimension,
array layers and buffer size, plus adapter timestamp support and enabled BC, ETC2
and ASTC compression features. Optional features are requested only when supported.
The host exposes `window.EngineQuality.snapshot()` and includes that snapshot in
`EnginePerformance.snapshot().rendering`: budgets, effective shadow settings,
canvas extent, compressed cache count, GPU generation, recovery attempts, secure
context and API availability. `capabilitiesAvailable` is false before acquisition;
initial placeholder limits are not a capability probe. Timestamp availability is
distinct from the timing sampler's enabled/disabled/readback status in performance
data. Backend identity and shadow/PBR/instancing support are also reported. Missing WebGPU or a secure context selects the WebGL 2 fallback in automatic mode; requiring WebGPU explicitly reports acquisition failure.

The renderer borrows the scene, views and GUI for its entire lifetime. Device loss
stops submission and queues recovery; `serviceRecovery()` must run at host frame
boundaries even while `ready()` is false. Recovery invalidates the callback owner
before releasing the old device, surface, pipelines, GUI renderer, view buffers,
timing readbacks and upload caches. It requests a fresh adapter/device, reconfigures
the canvas, and lazily uploads retained CPU assets again. Scene handles, camera,
pause state, quality policy and authoritative simulation data survive. Pending
callbacks from old GPU generations cannot write into the new caches or measurements.
Destroying the renderer cancels all callbacks through the same owner invalidation.

Automatic recovery allows three acquisition attempts, with bounded delays between
attempts. After 120 submitted frames the attempt budget resets. Exhaustion exposes
**Retry GPU connection**; `retryRecovery()` explicitly starts a new bounded cycle.
Initial acquisition/validation failures also expose retry. Shutdown is terminal
and destroys the renderer before its borrowed scenes. Recovery does not reload the
page, re-import the default model or reconstruct gameplay.

Transient surface timeouts skip a frame; outdated surfaces reconfigure next frame,
lost surfaces rebuild the backend, and surface errors enter the reported failure
path. Initial WebGPU API/adapter/device unavailability can select WebGL 2 before the canvas acquires a context. Once WebGPU has acquired the canvas, loss/exhausted recovery retains explicit WebGPU retry. WebGL 2 handles its own context loss/restoration. See [fallback selection and capabilities](rendering-fallback.md).

## Browser lifecycle

[quality.js](../web/quality.js) composes hidden-page, frozen-page and page-away
reasons. Visibility, freeze/resume and pagehide/pageshow (including persisted
back/forward restoration) clear browser input and call the existing host suspension
export. A pageshow cannot override an outstanding freeze or hidden-page reason.
GPU unavailability is combined with host suspension in the C++ frame callback.
Application suspension resets timestamp baselines and held input; all clocks
freeze and resume without hidden-time catch-up, while explicit pause is retained.
GPU service still runs independently of application advancement. Quality lifecycle
and DPR subscriptions are removed at terminal shutdown. Browser recovery from an
OS-discarded page requires normal page startup; in-memory scenes cannot survive a
process termination. See [application lifecycle](application-lifecycle.md).

## Optional compressed textures

`Texture::compressed` accepts pre-encoded BC7 RGBA, ETC2 RGBA8 and ASTC 4x4 variants
over the existing shared `Texture` representation. Every variant declares its
`TextureSemantic`: color uses an sRGB GPU format, data/normal use linear formats.
Author semantic mip chains offline, including normal renormalization where needed.
`CompressedTexture::validate(width,height)` requires a 4-aligned base size up to
4096, a complete mip chain through 1x1, exact halved logical dimensions and exactly
16 bytes per 4x4 block at each level. Unsupported enums, incomplete chains, extra
levels and wrong payload sizes are rejected before GPU publication. Validation
does not inspect the encoder's block contents or establish artistic quality.

The scene/particle renderer chooses the first matching supported variant when compression is
enabled; otherwise it generates/uploads the existing RGBA semantic mip chain.
RGBA fallback pixels are required and retained for unsupported devices, compression
disable, archives and GPU recovery. Upload extents use physical block-rounded mip
sizes as specified by [WebGPU texture copies](https://gpuweb.github.io/gpuweb/#texture-copies).
Encoded CPU payload and actual GPU mip bytes are counted in scoped performance data.
Changing the compression setting invalidates texture/material GPU bindings. GUI
textures retain their existing RGBA upload path.

The [mobile asset pipeline](mobile-assets.md) adds a restricted pre-encoded KTX2
reader, semantic mip export and fallback delivery bundles. There is no Basis
transcoder, runtime block encoder, compressed GLB extension or compressed archive
codec. Ordinary PNG/JPEG/FBX/GLB loading remains RGBA; applications
attach authored variants before publishing immutable textures. Current archives
save the RGBA fallback and omit these optional derived payloads, as they do LOD
chains. BC7/ETC2/ASTC require actual device support; none is assumed universal.

## Validation

The native `render_quality` test covers profiles, DPR, aspect-preserving pixel/device
limits, invalid/nonfinite candidates, complete block mip layout and unchanged RGBA
fallback. Existing lifecycle, LOD, shadows, views, frame caching, GUI, materials,
FBX/GLB and archive tests cover the reused contracts.

`scripts/render-quality-smoke.cjs` uses Playwright/pngjs with `DEMO_URL`, `NODE_PATH`
and `BROWSER_EXECUTABLE` configured as in the README. It tests actual WebGPU canvas
budgets, unchanged paused pixels after `GPUDevice.destroy()`, retained scene/clock
state, bounded adapter-failure retries, explicit reconnection, synthetic composed
platform lifecycle events, an authored BC7 upload versus RGBA fallback, GUI
resource/text-service recovery and shutdown during pending adapter acquisition.
Evidence goes to `build/evidence/render-quality/`.

These checks do not establish physical Android/iOS acceptance, Safari/Firefox
compatibility, real OS GPU crashes, OS page discard, guaranteed BFCache eligibility,
ETC2/ASTC execution on supporting hardware or sustained mobile performance.

## Verified evidence — 2026-10-10

- Emscripten 6.0.11 Release renderer: `build/web-quality-release`; staged site:
  `build/quality-browser-site`, served locally at port 8110 during validation.
- Native Debug: 12/12 targeted suites passed in `build/quality-native`: quality,
  application, mesh detail, scene archives, shadows, GUI, materials, views, camera,
  frame caching and FBX/GLB importers. This is scoped evidence, not a full-suite claim.
- Chrome 154.0.8037.99: rendering-quality/recovery suite passed all eight flows with
  no page/console errors. BC7 was supported and exercised on this adapter;
  unsupported compression/API fallback was also exercised through controlled probes.
- Final recovery-suite Wasm SHA256:
  `B54888AA8CDE249A7EACDDD861CDC69F43E560EFE63AFCB065AECBEDB90C1982`.
- Existing browser application, shadow, input and GUI suites passed. Input includes
  Chromium touch/DPR emulation. The GUI test's pass count now includes its shadow
  passes, fixing an obsolete four-pass expectation.
- Evidence: `build/evidence/render-quality/results.json` with paired scene/BC7
  PNGs; regression results in `build/evidence/quality-application/results.json`,
  `build/evidence/quality-shadows/results.json`,
  `build/evidence/quality-input/result.json`, and
  `build/evidence/quality-gui/results.json`.

The shared checkout contained concurrent application/assets/audio/GUI/persistence
work. It was retained; browser output was staged separately from `dist`. Build
configuration initially encountered not-yet-created sources, and a broad native
build encountered an unfinished prefab test. Validation used the explicit targets
above once their sources were ready.

## Planar water reflection captures

Opt-in `planarReflectionStrength` adds one single-sample World capture per view,
independent of main 1x/4x MSAA. The first visible requesting plane wins; coplanar
surfaces share it. Reflection color/depth storage is reported and shrinks to 1x1
fallbacks when camera-mask preflight finds no requesting water. WebGPU World
GPU timing includes the reflection pass. Reflection targets rebuild on resize and
recovery; each ordered view captures and consumes its pixels before the next.
See [water contracts](water.md) and `scripts/water-planar-smoke.cjs`.

## Water scene-input captures

Opt-in water refraction/foam adds current-view opaque color/depth captures without
changing requested/effective MSAA counts. WebGPU averages color and selects minimum
depth for its intermediate single-sample snapshots; WebGL 2 uses matching
RGB8/DEPTH_COMPONENT24 blits and driver-selected resolved depth. These intermediate
captures are separate from the one final canvas resolve after ordered views and
before GUI. At 1x, water uses a private accumulation target and final presentation
pass. Disabling both effects releases canvas-sized scratch targets and preserves
the original non-water pass count. Scratch memory is reported; capture/continuation
are included in WebGPU World timing, while final presentation/resolve and GUI remain
excluded. See [water contracts](water.md) and `scripts/water-depth-smoke.cjs`.

## Underwater scene composition

`underwaterStrength` opts into per-camera underwater fog/absorption. A containing
World water footprint is selected before target allocation, independently of
surface frustum visibility. Submerged views add a completed-World color/depth
snapshot and a fog composite before Overlay/GUI. Dry views skip both. The existing
1x/4x capture resolves are reused; fog writes the scene attachment at its current
sample count and final canvas resolve remains after all views. Pure underwater
materials also request optional scratch targets, even with refraction/foam off;
at 1x they need the single final presentation pass. Targets rebuild on resize and
recovery and shrink when unused. Memory counters include retained scratch storage;
WebGPU World timing includes both underwater passes. Transparent fog reuses opaque
depth rather than a separate transparent-depth buffer. See [water contracts](water.md)
and `scripts/water-underwater-smoke.cjs`.

Animated underwater caustics reuse the same composite and snapshots with no extra
passes or textures. The private underwater view uniform is 256 bytes; enabled
animation updates its time, while disabled or paused caustics retain cached values.
See `scripts/water-caustics-smoke.cjs` for both-backend pixel and cost acceptance.


## WebGPU material pipeline specialization

The WebGPU backend specializes its cached vertex/color pipelines for the effective
material family, alpha mode, layered-material flag, optional map presence and
reflection pass. Pipeline constants match the same effective material used to
upload uniforms. Opaque ordinary materials compile without alpha discard or
reflection clipping; unused water, terrain-layer and absent-map work is removed.
The cache identity includes every specialization flag, so changing a material
map or material family chooses the matching variant. Shared textures/materials,
light/shadow settings, animation and draw order retain their existing owners.
Shadow vertices use the same material-family constant; masked shadow fragments
specialize alpha/layer handling. WebGL 2 retains its existing shader path.

This is backend implementation policy and adds no application setting, resource
format or archive field. Validate PBR, unlit, water, masks, layers, map edits,
skinned/mirrored draws and device recovery when changing these constants.

Interaction ripples/wakes add no passes or textures. The private WebGPU frame
payload is 2400 bytes including up to 16 interaction records and per-view active
count; unchanged paused staging retains the existing upload cache. WebGL 2 uses
bounded fragment uniforms, without a float-target requirement. See water contracts
and `scripts/water-interactions-smoke.cjs`; fragment work scales with active sources.
