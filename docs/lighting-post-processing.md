# Shadowed lighting and HDR post-processing

Both browser backends support directional and spot shadows, camera-fitted sun
cascades, and optional HDR bloom/color grading. Portable settings contain no GPU
or browser types. These are presentation mechanisms; light switching, camera
ownership and visual defaults remain application policy.

Native Dawn and direct D3D12 (milestone 3) support the same post controls.
Direct D3D12 queries FP16 render/blend/sample/load and HDR MSAA resolve support;
`hdrPostProcessing`, `effectivePost` and `effectiveMsaaSamples` report availability
and actual presentation. Post stays off by default and GUI draws after composition.
See [native backend scope](windows-d3d12.md) for static-content limitations.

```cpp
auto& lamp = actor.addComponent<engine::LightComponent>("Headlight", engine::LightType::Spot);
lamp.direction = {0, -.2f, -1};
lamp.range = 25;
lamp.shadow.enabled = true;

auto quality = renderer.quality();
quality.directionalCascades = 3;
quality.shadowDistance = 80;
quality.post.enabled = true;
quality.post.exposure = .5f;       // EV/stops; multiplier is 2^exposure
quality.post.bloomStrength = .15f;
quality.post.bloomThreshold = 1;  // Scene-linear, before exposure
quality.post.saturation = 1;
quality.post.contrast = 1;
renderer.setQuality(quality);
```

## Shadows

The first eight enabled, masked lights retain their shared lighting budget.
`maxShadowMaps` remains 0..4 per view; eligible positive-intensity lights consume
maps in scene order. A directional light takes up to `directionalCascades` of the
remaining maps; each spotlight takes one. For example, three sun cascades leave
one map for a headlight. There is no automatic priority/ranking or point cubemap.

`directionalCascades=1` preserves the authored fixed, texel-snapped orthographic
volume. Values 2..4 fit camera-frustum slices, supporting perspective and
orthographic cameras. Splits blend linear/log distances with `cascadeLambda`
(default .6, range 0..1); coverage ends at min(camera far, `shadowDistance`).
The last 10% of each slice blends to the next. Radius quantization and texel
snapping stabilize XY; `ShadowSettings::halfDepth` adds caster depth padding.
Objects outside that finite volume cannot cast into it. If the distance ends
before the camera near plane, that directional light publishes no cascades.

Spot maps use the collected world position/direction, twice the outer half angle
as square perspective FOV, near=max(.00001, range*.001), and far=range. Geometry
closer than the near plane is excluded. Existing depth/normal biases, resolution,
1/9-tap PCF, cast/receive flags, masks, independent shadow LOD, off-camera casters,
alpha cutouts, mirrored/double-sided meshes and skeletal/morph poses apply.
Transparent surfaces receive but do not cast. Ambient/emission stay unshadowed.

`planShadows` returns at most four value records, without retained references or
scene mutation. WebGPU owns a Depth32Float array; WebGL2 owns a DEPTH_COMPONENT24
array and bounded depth-only passes, using texture unit 13 internally. Device
loss rebuilds them. Their bytes and submissions appear in existing counters;
only WebGPU provides asynchronous GPU timing. Both report `spotLightShadows=true`.

## HDR pipeline

Post-processing defaults off. Enabling it switches scene color, reflection and
water color captures to RGBA16F. Meshes, sky, particles, trails and underwater
composition retain scene-linear values through transparency and MSAA. After all
ordered views/layers, the resolved scene feeds quarter-resolution bright-pass
extraction (16 source taps) and a 25-tap tent bloom/composite. Zero bloom strength
skips extraction and bloom sampling. Exposure precedes Reinhard tone mapping,
then saturation and contrast around linear .18, clipping and sRGB output.
The browser canvas remains SDR; HDR refers to internal radiance, not HDR display
output. This is numeric grading, not LUTs, auto-exposure or a filmic curve.

The effect applies to the complete composited canvas, including Overlay meshes;
bloom may cross adjacent view boundaries. GUI and DOM controls render afterwards
and are not graded. Resize, quality switches and recovery rebuild dependent
targets without altering simulation. GPU resources are backend-owned; no new
clock, actor owner, callback or subscription is introduced.

WebGPU supports the float targets directly. WebGL2 requires
`EXT_color_buffer_float`; `RenderCapabilities::hdrPostProcessing` reports it.
Unavailable devices keep the requested setting and render the standard path.
Float-target MSAA is checked separately; requested 4x falls back to 1x if needed.
The existing WebGL basic-lighting/no-IBL/no-instancing limits remain.

All fields validate together before replacing quality: exposure -8..8, bloom
strength 0..2, threshold 0..32, saturation/contrast 0..2, cascades 1..4, finite
positive shadow distance and lambda 0..1. Defaults are exposure0, bloom .15,
threshold1, saturation1, contrast1, cascades1, distance80. Settings are renderer
policy, not scene/archive preferences. Existing spotlight shadow booleans persist
through normal scene saves, duplication and Play without a new archive version.

## Authoring and validation

Open `?scene=lighting` for shadow-casting headlights, a warm torch and three-cascade
sun presets. Pillars make occlusion visible and an emissive source demonstrates HDR
bloom. Cast shadows and HDR bloom/color grading toggles compare the same paused scene;
drag to orbit. The existing headlight editor link uses these controls too. No model
download is required. The examples use ordinary components in `src/spotlight_example.inc`
and browser controls in `web/editor-spotlight.js`; they add no reusable light owner.
The quality panel exposes individual exposure, bloom, saturation and contrast values.
Presets request three cascades but retain the current shared shadow-map budget.

Use Rendering quality -> Lighting & post-processing -> Apply lighting. Spot
shadows use the existing Light inspector's shadow toggle with Undo/Redo/Play.
Profiles reset to default post/cascade policy; custom edits retain current policy.

Use the pinned local Emscripten 6.0.11 and README commands. When building explicit
targets, `web_assets` stages the matched generated site and current `web/` host
into `dist`; building only `webengine` does not publish that site.

Native targets: `shadow_tests`, `spotlight_tests`, `render_quality_tests`,
`render_frame_tests`, `scene_archive_tests`. Browser:
`scripts/lighting-post-smoke.cjs` checks both backends with actual pixel differences,
invalid retention, disable restoration, MSAA, resize and recovery.
`scripts/lighting-showcase-smoke.cjs` checks preset buttons, each visible shadow,
post comparison/restoration, compact layout and GPU recovery on both backends.
Existing `shadow-smoke.cjs`, `spotlight-smoke.cjs`, and water suites cover reused paths.
See [recorded evidence](lighting-post-processing-validation.md). Desktop Chrome
and DPR emulation do not establish physical Android/iOS, Safari, or sustained
mobile performance. Extra shadow passes and HDR/bloom have explicit GPU costs.
