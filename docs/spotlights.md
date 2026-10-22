# Spotlights

Driving Stage 7 adds optional `LightType::Spot` to the existing scene lighting
contract. Both WebGPU and WebGL2 support cones with optional shadow maps. Headlight/torch mounting,
on/off rules and vehicle controls remain application choices.

```cpp
auto& lamp = vehicle.addComponent<LightComponent>("Headlight", LightType::Spot);
lamp.transform.position = {-.8f, 1.1f, -2};
lamp.direction = {0, -.22f, -1}; // Local emitted direction, not a target point.
lamp.innerAngle = 15 * pi / 180;
lamp.outerAngle = 30 * pi / 180;
lamp.range = 18;
lamp.intensity = 15;
lamp.color = {1, .9f, .7f};
lamp.validate();
```

## Data and ownership

`LightComponent` retains ordinary hierarchy ownership, visibilityMask, color,
intensity, range and local direction. Position inherits the hierarchy; direction
is transformed as a vector and normalized during collection. Nonuniform hierarchy
scale can change that direction, but range and cone angles remain world units.
Camera choice/interpolation/LOD never writes a light or physics. No controller,
callback or automatic vehicle attachment is introduced.

Defaults: range 10 m, intensity 1, white, direction `(0,-1,0)`, inner half angle
20 degrees, outer half angle 30 degrees, mask 1, shadows off. Angles in portable
APIs are radians. Require `0 <= inner < outer < pi/2` and a cosine separation of
at least 1e-6 for stable smooth interpolation. Spot range is at least .001 m;
intensity/color must be finite and nonnegative. Directional/Spot directions must
be finite and nonzero (squared length >=1e-12); unused zero Point direction remains
accepted for older scenes. Invalid/singular/nonfinite collected world directions
are rejected. Point shadows are rejected; Spot and Directional accept shadow.enabled. See
[lighting and post-processing](lighting-post-processing.md) for shared map budgets.

`validate()` checks common component state and light settings. Allocation-free
`validateSettings()` is used during warm collection, preserving RenderFrame's
zero-allocation contract. Public field edits remain supported; application code
validates complete candidates before publishing. Native typed editor commands
stage a property write, validate and roll back invalid candidates/history.

## Attenuation and budgets

At world receiver P, let distance be the distance to the source, L the unit vector
from receiver to source, and D the collected emitted direction:

```
radial = clamp(1 - distance / range, 0, 1)^2
cone = smoothstep(cos(outerAngle), cos(innerAngle), dot(D, -L))
attenuation = radial * cone
```

The inner region has full cone contribution; outside/on the outer boundary and
at/beyond range the contribution is zero. This retains the existing finite-range
Point law; it is not photometric inverse-square intensity. Near-source distances
retain the renderer's small division guard. Portable `spotlightAttenuation`
provides the reference law for finite distance >=0 and cosine in [-1,1].

Each view accepts the first **eight active enabled masked lights**, shared between
Directional, Point and Spot, in scene traversal order. Zero intensity still occupies
a slot. There is no distance ranking, clustered lighting or per-object light list.
Excess lights are truncated; WebGPU reports its existing overflow diagnostic.
`RenderCapabilities` and browser EngineQuality report `maxLights=8`,
`spotLights=true`, `spotLightShadows=true` on both renderers. There is no separate
spotlight quality switch. Shadow map budgets are unchanged.

WebGPU extends its private light payload to four vec4s (64 bytes), and its retained
frame buffer/cache and WGSL layout together (2336 bytes). WebGL2 supplies eight
cone vec4 uniforms. No GPU resource or renderer types enter portable APIs. Shadow opt-in adds bounded depth passes.
Existing material, alpha, layer, depth, fog, MSAA, reflection and recovery paths
apply. The cone scales direct diffuse/specular and water highlights only; ambient,
environment, emissive and unlit appearance are unchanged. WebGL2 retains its basic
lighting/PBR limitations. Shadowed cones occlude direct light through geometry. Volumetric beams are not implemented.

## Editor and example

Select a LightComponent (or use Scene structure -> Add LightComponent), then edit
Components & properties -> Light. Type includes Spot; inner/outer **half angles in
degrees**, range, local XYZ direction, color/intensity and visibility mask use
ordinary Undo/Redo, duplication and isolated Play. Conversion to Spot requires a
valid direction. Invalid inner/outer or shadow combinations retain
the last valid scene and do not add history.

The Spotlights panel controls a pointer-transparent selected cone overlay. It draws
inner/outer rings, four rays per ring and an axis, clipped to the viewport. Ring
ends lie on the spherical range boundary, matching shader distance cutoff.
Portable `spotlightOutline(light,segments=32)` supplies world line pairs (8..64
segments per ring, `2*segments+9` pairs), without retained scene references. It
does not affect rendering/simulation state. Outlines are authoring aids, visible
even when geometry would occlude them, and hidden in Play/FX preview.

Open headlight example opens a separate page at `?editor=1&scene=spotlight`, keeping
the current document intact. `src/spotlight_example.inc` creates a static example
chassis with two ordinarily mounted shadow-casting lights, pillars and an illuminated
ground plane. `?scene=lighting` exposes Headlights, Torch and Cascaded sun presets,
shadow and HDR comparison toggles. All presets are paused; orbit navigation remains
available. This is application content, with no new vehicle class or animation clock.

## Persistence and validation

Scene/application archive **v30** introduced the two angles in built-in
LightComponent records and accepts Spot. The current writer is **v34**, reading
**1..34**; this lighting showcase adds no format change. Versions 1..29 retain their
old record layouts and load 20/30-degree defaults. Saved directions, transforms,
masks, range/color/intensity, type and cone settings retain their values on duplicate,
save/open, application checkpoints and isolated Play. Invalid/nonfinite cones are
rejected in staged loading before document publication. GPU buffers and SVG outlines
are transient. Publish matched host, decode worker and Wasm artifacts.
`PrefabLightSettings` additionally exposes innerAngle/outerAngle and Spot type,
with factory validation before scene adoption.

Use the local pinned Emscripten 6.0.11 and README build commands. Focused CTest
`spotlight` / `spotlight_tests`, explicit `spotlight_wasm_tests`, and
`scripts/spotlight-smoke.cjs` cover contracts on native/Wasm and both renderers;
run the browser suite at DEVICE_SCALE_FACTOR=1 and 2. Also check warm collection,
shadow and water regressions when changing the private light payload.
See [recorded validation](spotlights-validation.md).
