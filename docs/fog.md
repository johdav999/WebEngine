# Exponential height fog

`Scene::setFog(std::optional<FogSettings>)` owns optional presentation data.
New scenes have no fog. `setFog(std::nullopt)` disables it. Invalid settings throw
before replacing the previous value. `Scene::fog()` returns a const optional.

Native Dawn and direct D3D12 (milestone 3) share these settings, per-view opt-outs
and analytic fog payload. Direct coverage includes conventional World materials
(including skinning/morphs from milestone 4) and
sky; water and particle/ribbon rendering remain outside that backend's scope.
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

Local volumes, ray-marched volumetric fog, light shafts and multiple scattering
are future work. Desktop Chrome evidence does not establish physical mobile
acceptance or sustained performance.
