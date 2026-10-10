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
data. Missing WebGPU/secure context gets an explicit unavailable report.

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
path. There is no fallback rendering backend.

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

The renderer chooses the first matching supported variant when compression is
enabled; otherwise it generates/uploads the existing RGBA semantic mip chain.
RGBA fallback pixels are required and retained for unsupported devices, compression
disable, archives and GPU recovery. Upload extents use physical block-rounded mip
sizes as specified by [WebGPU texture copies](https://gpuweb.github.io/gpuweb/#texture-copies).
Encoded CPU payload and actual GPU mip bytes are counted in scoped performance data.
Changing the compression setting invalidates texture/material GPU bindings.

There is no KTX2/Basis decoder, runtime block encoder, compressed GLB extension or
compressed archive codec. Ordinary PNG/JPEG/FBX/GLB loading remains RGBA; applications
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
platform lifecycle events and an authored BC7 upload versus RGBA fallback.
Evidence goes to `build/evidence/render-quality/`.

These checks do not establish physical Android/iOS acceptance, Safari/Firefox
compatibility, real OS GPU crashes, OS page discard, guaranteed BFCache eligibility,
ETC2/ASTC execution on supporting hardware or sustained mobile performance.
