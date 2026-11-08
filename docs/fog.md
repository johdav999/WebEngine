# Exponential height fog

`Scene::setFog(std::optional<FogSettings>)` owns optional presentation data.
New scenes have no fog. `setFog(std::nullopt)` disables it. Invalid settings throw
before replacing the previous value. `Scene::fog()` returns a const optional.

Native Dawn and direct D3D12 share these settings, per-view opt-outs
and analytic fog payload. Direct coverage includes World materials,
skinning and morphs, sky, water, and particle and ribbon rendering.
See [native backend scope](windows-d3d12.md).

```cpp
engine::FogSettings fog;
fog.color = {0.65f, 0.75f, 0.85f}; // Linear display light, not sRGB.
fog.density = 0.04f;
fog.startDistance = 2;
fog.baseHeight = 0;
fog.heightFalloff = 0.15f;         // Zero gives uniform exponential fog.
scene.setFog(fog);
engine::RenderView inset{&insetCamera};
inset.fogEnabled = false;
```

Defaults are color `(0.65,0.75,0.85)`, density `0.02`, start distance `0`, base
height `0`, and height falloff `0.15`. Distances/heights are world units; density
and falloff are inverse world units. Finite color components must be in `[0,1]`,
density/falloff in `[0,100]`, start distance in `[0,1e6]`, base height in
`[-1e6,1e6]`. A zero density is a valid no-op. The playground's **Height fog**
controls affect the displayed scene, work with terrain/water demos, and convert
the color picker from sRGB. Controls refresh after scene load/return.

## Ray integration

Density at height `y` is `density * exp(-heightFalloff * (y-baseHeight))`.
The shared shader helpers analytically integrate this along each surface's
viewing segment after removing `startDistance` from its beginning. Transmission
is `exp(-opticalDepth)`. A small-difference series avoids cancellation for nearly
horizontal rays. Endpoint-exponent evaluation is bounded to `[-40,40]` and optical
depth to 80 for stable extreme heights. This intentionally saturates extreme
out-of-scale scenes. `fogTransmission` exposes the matching portable calculation.

Perspective segments start at the eye; orthographic segments start at the
per-pixel near plane. The sky uses the same integration to its far-plane endpoint,
including orthographic pixel offsets, and converges toward the same fog color as
world surfaces. Fog does not change the sky's lighting, IBL or day/night state.
Authors still choose fog color to fit the scene's time/weather.

## Rendering and composition

Both WebGPU and WebGL 2 apply shared fog functions inside material shaders after
tone mapping and before display encoding, in linear display light. PBR, unlit,
terrain layers, skinned/morphed meshes, alpha meshes and particle billboards use
their own interpolated world positions. Alpha stays unchanged; additive meshes
and particles attenuate toward zero rather than adding fog-colored emission.
Background meshes, Overlay and GUI remain unaffected. `RenderView::fogEnabled`
defaults true and disables material, particle and sky fog for that view only.
Ordered cameras retain separate payloads; existing MSAA, resize, GPU recovery and
uniform caching paths remain authoritative. Fog adds no render passes or textures.

Water's intrinsic shading receives camera-to-surface fog before composition.
Refraction captures already fogged opaque scene color and does not fog that color
again. Water absorption mixes it with fogged body color; foam is fogged at the
surface. Planar reflections integrate a two-leg path from the real eye/orthographic
near plane to the water plane and then to the reflected opaque receiver, sharing
one start-distance budget. The fully fogged reflection is mixed directly with the
fogged surface, without another atmospheric attenuation. Existing one-plane and
opaque-only reflection limits remain. Fallback environment reflection has no
receiver distance and receives surface fog only.

Atmospheric fog remains a scene-wide artistic medium, including below water.
The existing underwater absorption/caustics composite runs afterward and keeps its
documented opaque-depth approximation for transparency. This is not a coupled
air/water volumetric transport model; refraction uses captured camera rays, not a
physical bent-ray atmospheric integral. See [water](water.md).

## Persistence and validation

Scene archive format 16 adds optional scene fog and each view's opt-out. Formats
1–15 load with scene fog absent and view fog enabled. Scene cloning/editor Play
preserve the same settings; GPU resources and runtime state are not archived.
Deploy matching host/worker codecs after the format change.

Native CTest `fog` compares analytic integration with numerical integration and
checks height, distance, start distance, extreme heights, invalid-state rollback,
view payloads, archives and cloning. Run it with `sky`, `water`, `scene_archive`,
`persistence`, `render_frame`, `render_view` and `fx`. Browser
`scripts/fog-smoke.cjs` checks both forced backends, height/distance pixels,
alpha/particles, unaffected layers, start distance, invalid inputs, 1x/4x MSAA,
ordered opt-out cameras, perspective/orthographic, resize/recovery, save/load,
controls and water/underwater composition. Evidence is written to
`build/evidence/fog`. A test run is recorded separately in
[fog validation](fog-validation.md).

## Opt-in volumetric fog

Set `FogSettings::volume` to a `FogVolume` to add a world-aligned box of
participating media. Use `additionalVolumes` to add up to three more boxes,
with or without a primary box. The default box spans `(-5,0,-5)` to `(5,4,5)`. Its
scattering color is `(.8,.8,.8)`, extinction is `.12` per world unit, its
edge fade is `.25` world units, and its ray distance limit is 60 world units.
Clear all volumes or set `RenderQuality::volumetricFogSteps` to zero to select
analytic fog. The default budget is 16 and the maximum is 32. Set the scene's
analytic density to zero to author local fog alone. Both settings validate before
publication. Each camera uses the existing fog opt-out.

Browser WebGPU, WebGL2, Windows Dawn and direct D3D12 share a per-camera fog
solver. Each ray starts at the perspective eye or its orthographic near-plane
position. `RenderQuality::volumetricFogMaxLights` accepts 1–4 and defaults to 1.
The solver selects positive-intensity directional and spot lights. Shadow-mapped
lights come first in map order, counting a cascaded light once. Remaining slots
follow view light order. The solver accumulates single scattering and
transmission in linear color using the selected lights' existing shadow maps. Analytic
height fog is integrated before, inside and after the box.

The grid stores cumulative results at 16 depths along each ray on a 64×36
screen grid. Depth slices follow the ray's entry and exit from the box, which
concentrates samples in the medium. A direct box test protects rays that miss
the medium or end before it. Opaque and transparent meshes, sky, billboards and
ribbons interpolate the grid at their own endpoint. Additive effects apply
transmission only. Water refraction reuses the fogged opaque capture and water
surface shading samples its own endpoint. Planar reflections use the direct
solver for their two-leg path. Background meshes, Overlay and GUI are excluded.

The grid is on by default for a single active volume. WebGPU and Dawn compute
each ray's cumulative samples once into a packed FP16 storage buffer. WebGL2
and direct D3D12 rasterize the same prefixes into an FP16 atlas using their
existing float target support checks. Each view finishes sampling before the
next view overwrites the shared 288 KiB resource. WebGPU and WebGL2 release the
full grid when no view needs it. D3D12 keeps the single allocation until backend
teardown. The grid rounds a positive ray-step budget up to 16 or 32 samples per
column. FP16 target unavailability or `RenderQuality::volumetricFogGrid=false`
selects the direct per-fragment solver with the exact positive step budget.
Multiple active boxes also select the direct solver. It clips steps to each
box and adds extinction where boxes overlap. Each box contributes its own
scattering color, edge fade and camera distance limit. Midpoint lighting
and the extinction-weighted source approximate the ordering of media
within each step.
A step budget of zero selects analytic height fog. `fogGridPasses` reports the
number of grids built in the frame. WebGPU includes the compute pass in World
GPU timing. D3D12 exposes a Volumetric fog GPU timing span.

Multiple scattering, temporal reprojection and device-tuned mobile presets
remain future work.

### Spotlight self-shadowing

Set `RenderQuality::volumetricFogSelfShadows=true` to attenuate each selected
spotlight along the segment from each fog sample to the light. The default
is off. Geometric shadow visibility and light-path transmission multiply
the same direct scattering term. A hard-edged box uses its exact intersected
length. For edge fade, four midpoint samples along the clipped light ray
approximate the integral of the density ramp. The light path ends
at the spotlight, independently of the camera/reflection distance budget.

WebGPU, WebGL2, Dawn and D3D12 share the rule in the grid and direct march,
including both legs of a planar water reflection. Light transmission accounts
for every active box on that segment, including boxes outside the camera
distance limit. The switch is renderer quality policy.

In the doorway use **Fog self-shadowing** and **Planar water**, or keys **T**
and **W** in the Windows gallery. The dedicated capture fixture isolates
scattering at 1.5 and 4.5 world units into the box and compares its
linear-light on/off ratio. Run `node scripts/fog-self-shadow-smoke.cjs browser`
with the same browser setup as the water test. For Windows run
`node scripts/fog-self-shadow-smoke.cjs windows build/windows-desktop/Release/webengine_lighting_windows.exe`.
Captures and ratios are written to `build/evidence/fog-self-shadow/`.

The 2026-11-06 Release captures used a density of 0.5, so the expected
transmission at the two probes was 0.472 and 0.105. Grid, direct, faded-edge,
planar reflection and reflection-off checks passed on all four backends.

| Backend | Grid near / far | Direct near / far | Reflected near / far |
| --- | --- | --- | --- |
| Browser WebGPU | 0.471 / 0.106 | 0.471 / 0.106 | 0.472 / 0.106 |
| Browser WebGL2 | 0.472 / 0.105 | 0.471 / 0.105 | 0.474 / 0.104 |
| Windows Dawn | 0.468 / 0.105 | 0.468 / 0.105 | 0.474 / 0.105 |
| Windows direct D3D12 | 0.468 / 0.105 | 0.468 / 0.105 | 0.474 / 0.105 |

### Surface shadows and geometric shadow filtering

Enable `RenderQuality::volumetricFogSurfaceShadows` for fog attenuation of
direct directional, point and spot illumination on World PBR surfaces and
water. Ambient, image-based lighting, emission and unlit shading retain their
existing terms. A receiver can lie outside the fog if the light ray crosses it.

Enable `RenderQuality::volumetricFogSoftShadows` to filter geometric shadows
used for volumetric scattering. The filter takes five weighted comparison
samples and blends to the next valid cascade over the final 10% of the current
cascade's range. Both switches default off. The fog quality settings apply
per enabled view and follow the zero-step analytic fallback.

### Light-space optical-depth cache

Set `RenderQuality::volumetricFogLightCache=true` alongside self-shadowing or
surface shadows to cache the local media's light-path extinction for up to
four spotlights. The default is off. Each light uses a 64³ lattice aligned to
its direction and bounded by all active fog boxes. The GPU stores optical
depth in FP16 and interpolates it before evaluating transmission. Cache bounds
include media beyond the camera ray-distance limit. A receiver beyond the
bounds samples the exit of its light ray, so a clear-air receiver can still
receive attenuation from intervening fog.

The cache key includes local bounds, extinction, edge fade and spotlight
pose and compact slot. Camera movement, scattering color and light intensity
changes preserve optical-depth data. An optical edit or light pose change
builds a complete replacement before rendering. Directional and point lights,
and spots outside the four cached slots, use direct integration.
Light-relative domains too narrow for reliable floating-point lattice
coordinates also use direct integration.
View opt-out, zero steps and disabling the cache select the existing paths.
Cache data is transient renderer state and uses the existing scene archives.

WebGPU and Dawn store 2 MiB per cached view in a filterable RGBA16F 3D texture.
Hardware filtering interpolates optical depth. Their optional path requires
`maxTextureDimension3D >= 64` and `maxSampledTexturesPerShaderStage >= 17`.
Lower adapter limits or optional resource creation failures select direct
integration. Cache policy specializes shaders so the disabled path removes
cache sampling code. D3D12 stores
2 MiB in a raster atlas reused by ordered views. WebGL2 combines its cache
and camera grid in a larger atlas and stages camera-grid production before
blitting, which avoids sampling an active render attachment. Resource byte
counts are exported as `fogLightCacheBytes`. `fogLightCacheBuilds` counts
builds in the current frame. Recovery invalidates GPU data and rebuilds it
from the current scene.

The lattice approximates direct light integration. Thin boundaries and
density ramps can require direct integration for the desired image accuracy.
Moving fog or lights can rebuild the lattice every frame. Use the
[profiling cases](fog-profiling.md) to compare total GPU cost and image
differences for the intended scene.

Run `node scripts/fog-milestones-smoke.cjs browser` and
`node scripts/fog-milestones-smoke.cjs windows <webengine_lighting_windows.exe>`
for surface transmission, overlapping boxes, light selection and moving
spotlight-shadow and cascade fixtures. Evidence is in
`build/evidence/fog-milestones/`.

Archive format 39 stores the additional boxes. Formats 37 and 38 preserve
their single-box data and load with an empty list of additional boxes. Earlier
scenes load with no volume. Cloning and editor Play use the archive contract.
Ray steps and lighting limits remain renderer policy.

The 2026-11-06 milestone suite passed all 18 image cases on WebGPU, WebGL2
with compact PBR, Dawn and direct D3D12. The clear-air receiver matched the
Beer–Lambert reference with each center channel within one 8-bit code value.
The center of the overlapping pair matched a doubled-density box, and the
two-light fixture reproduced both single-light color channels. The fixed and
moving shadow controls exercised both the spotlight filter and directional
cascade fixture. The spotlight self-shadow and volumetric water regressions
also passed on all four paths. Fourteen focused Windows CTests passed. The browser cadence
samples cover 20 frames per selected case. Physical Android and iOS device
validation and sustained performance remain open.

Open `?scene=fog-doorway` and switch between Volumetric grid, Direct volumetric,
Analytic height fog and Off. Toggle Cast shadows to see how the opening shapes
the beam. The scene includes a gradient sky, translucent panel and dust.
On Windows use `webengine_lighting_windows --backend dawn --fog-doorway volumetric`
or select `d3d12`. Use O, A, V and D to switch modes and S to toggle shadows.
Capture options include `--capture image.ppm`, `--no-doorway-shadows`,
`--doorway-ortho`, `--doorway-hdr` and `--doorway-msaa`.

Run CTest `fog` for validation, fallback payload, view opt-out and archive
coverage. Run `scripts/fog-doorway-smoke.cjs` and `scripts/fog-smoke.cjs` on a
fresh pinned browser build. For both Windows backends run
`python scripts/fog-doorway-windows-smoke.py build/windows-desktop/Release/webengine_lighting_windows.exe`.

### Shared grid evidence, 2026-11-06

The Chrome 156 checks used the repository-local Emscripten 6.0.11 Release
build. Forced WebGPU and WebGL2 each passed the doorway pixel comparisons,
grid pass checks, particle draws, camera opt-out, resize after requesting HDR/MSAA and
GPU recovery. Both recovered with zero changed pixels at the RGB-difference
threshold of 12. Disabling the WebGL2 float color extension produced exactly
the direct mode's image and skipped the grid pass. The existing analytic fog
suite passed on both browser paths, including its water and view tests.

On the desktop test adapter, the Chrome WebGPU render GPU sample mean was
5.32 ms with the grid versus 17.73 ms with direct fog, from eight samples per
mode after warming the scene. WebGL2 had no GPU timestamp data. Its mean frame
interval was 29.45 ms with the grid versus 31.11 ms with direct fog on this run.
These figures characterize this scene and machine. They leave physical mobile
performance and sustained multi-scene testing open. See
`build/evidence/fog-grid/` and `build/evidence/fog-grid-analytic/`.

On Windows, Dawn and direct D3D12 rendered the perspective doorway and the
orthographic HDR/4× MSAA variant on the RTX 5060 Ti. The grid and direct
reference differed at 17,687 and 17,639 of 921,600 pixels respectively at
the same RGB threshold. The D3D12 debug-layer capture also passed.
All nine focused Windows fog and archive tests passed. These comparisons
exercise sky, transparent meshes and particles in the doorway. The dedicated
[volumetric water fixture](water.md#volumetric-fog-acceptance) covers refraction
and reflected paths separately.

### Per-fragment baseline evidence, 2026-11-06

Each comparison counts pixels whose summed absolute RGB difference exceeds 12.
The shadow toggle affects both surface and volume lighting.

| Path | Volumetric versus analytic | Shadows on versus off | Browser recovery |
| --- | ---: | ---: | ---: |
| Browser WebGPU | 355,287 | 312,254 | 0 changed pixels |
| Browser WebGL2 | 347,594 | 310,828 | 0 changed pixels |
| Windows Dawn | 376,836 | 326,351 | — |
| Windows D3D12 | 376,836 | 326,350 | — |

The browser used Chrome 156 and the repository-local Emscripten 6.0.11 Release
build. The existing analytic fog smoke suite also passed on both browser paths.
The Windows captures used the Visual Studio Release build and the explicit
backend selection. All nine focused native fog and archive tests passed.
Captures and JSON counts are in
`build/evidence/fog-doorway/`. The scene exercises the perspective spotlight
path at 1×. Volumetric HDR, MSAA, water, other views and physical mobile costs
were outside this capture comparison.
