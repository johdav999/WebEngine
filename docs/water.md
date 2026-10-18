# Reflective animated water

`MaterialShader::Water` renders an optional presentation surface on ordinary mesh
components on WebGPU and WebGL 2. It combines three bounded Gerstner waves,
analytic geometric normals, two moving normal-map samples and procedural fine
ripples, dielectric Fresnel reflections and directional/point highlights.
Existing alpha modes, winding, double-sided state, layers, masks and material
instances apply. Opt-in refraction and shoreline foam capture opaque World color
and depth before drawing water on either backend. Optional planar reflections
capture nearby opaque objects with a mirrored camera. With all scene effects
disabled, water retains the original rendering path.

## Usage

```cpp
#include "engine/material.hpp"

auto water = std::make_shared<engine::Material>();
water->shader = engine::MaterialShader::Water;
water->baseColor = {0.018f, 0.11f, 0.16f};
water->roughness = 0.16f;
water->water.amplitude = 0.12f;
water->water.wavelength = 3;
water->water.direction = {1, 0.3f};
water->water.underwaterStrength = 1; // Optional effects when the camera enters the water.
water->water.causticsStrength = 1.6f; // Optional animated light on submerged receivers.
water->doubleSided = true; // Render the surface from below.
water->water.planarReflectionStrength = 1; // Optional nearby-object capture.
water->water.refractionStrength = 0.85f;
water->water.foamStrength = 0.9f;
water->water.foamWidth = 0.4f; // World units.
water->normalTexture = rippleNormals; // Optional shared linear +Y normal map.
mesh.setMaterial(0, engine::MaterialInstance(water));

// Publish/freeze the material, then provide a caller-owned clock before rendering.
renderer.setWaterTime(simulationSeconds);
renderer.render();
```

Use a subdivided, upward-facing mesh whose water plane is local XZ. Two triangles
can show animated normals but cannot resolve geometric waves. Wave dimensions
are in local mesh units; component transforms scale and rotate the whole surface.
The shader displaces positions and derives the tangent basis after any existing
vertex pose deformation. Water does not generate geometry or choose tessellation;
authored LODs must retain sufficient wave detail. The demo uses an 18-unit square
with 128 divisions per axis.

`WaterSettings` defaults are amplitude 0.12, wavelength 3, speed 1, steepness 0.35,
direction (1, 0.3), ripple scale 6, ripple strength 0.18, reflection strength 1,
horizon (0.65, 0.8, 1), zenith (0.06, 0.2, 0.45). Reflection colors are linear
radiance. Directions are normalized when preparing shader parameters. Validation
rejects nonfinite settings, a zero direction, negative values and unsupported
ranges before replacing valid overrides. Ranges are amplitude [0,100], wavelength
[0.01,10000], speed [0,10], steepness/ripple strength/reflection strength [0,1],
ripple scale [0.01,1000], direction components [-10000,10000] and radiance [0,100].
`normalScale` scales authored ripples; `rippleStrength` controls all fine ripples.
Amplitude zero disables geometric waves; amplitude and ripple strength both zero
produce stationary geometry/normals, while enabled foam can still animate.
Speed zero freezes waves, ripples and foam. Horizontal displacement is clamped to prevent wave folds.

`MaterialInstance::water` overrides the full settings value, inheriting when
disengaged. Shared binding identity includes normalized water parameters so
different settings cannot be instanced together. Animation time is a per-renderer,
per-view uniform, not a material-cache key: advancing it does not create new
material bindings. The portable `MeshComponent::localBounds()` expands geometry
bounds by the largest effective water amplitude on all slots, including override
changes. This bounds every axis of the supported displacement and preserves
camera/shadow culling without changing mesh vertices or transforms.

## Depth-aware refraction and shoreline foam

Set `refractionStrength` or `foamStrength` above zero to opt into scene inputs.
Both default to zero, preserving existing materials and format-7 water. Other
defaults are refraction distortion 0.015 (fraction of the view extent), foam width
0.35 world units, absorption (0.7, 0.24, 0.12) per world unit, foam scale 8 and
foam color (0.85, 0.93, 1) in linear display color. Strengths are [0,1], distortion
[0,0.1], foam width [0.001,100], absorption components [0,100], foam scale
[0.01,1000] and foam color components [0,1]. All fields must be finite. Settings
and optional full-value overrides use the same atomic material validation.

Screen-space effects apply only to **World** water. Background/Overlay water keeps
surface shading. `waterSceneInputsRequired(scene, visibilityMask)` reports whether
enabled World water overlapping the mask requests inputs; disabled water and
zero-strength settings do not. Backends combine active camera masks before
allocating canvas-sized targets, then split only views collecting relevant water.
The preflight is conservative with respect to frustum culling. Tiny valid fallback
textures keep common mesh bindings usable when effects are off.

Each affected view draws non-water opaque/masked World geometry first, captures
its color/depth, then draws water and the existing transparent/particle queue.
Snapshots are reused serially across views. Sampling clamps to the current
viewport's half-texel bounds; depth never comes from a previous camera or another
layer. Water is excluded from the source snapshot, avoiding self-feedback.
Background color is retained, but Background objects have no World depth and
therefore do not become refractive geometry. Other transparent geometry and other
water surfaces are not included in refraction.

The shader reconstructs world positions from opaque depth with the view's inverse
projection. It estimates ray thickness for both perspective and orthographic
cameras, distorts the lookup using the surface normal, and applies exponential
color absorption. Refraction samples in front of the water in camera depth,
above its plane, at clear depth, or outside the view are rejected/clamped, falling
back to the original pixel. Color capture contains already tone-mapped scene
pixels; it is decoded and combined in linear display-light space, avoiding a
second tone map. This is an artistic screen-space approximation, not a physical
Snell-law/volume renderer.

Foam fades by the reconstructed gap below the water plane, using `foamWidth` in
world units. A procedural pattern follows the same caller-owned water clock.
Clear depth creates no foam, and dry foreground geometry continues to occlude
the surface. Foam color is independent of environment reflections. There is no
shoreline mesh, terrain query, persistent foam simulation or physics coupling.

WebGPU owns a sampled accumulation color target for single-sample water scenes,
a color snapshot and R32Float depth snapshot. At 4x MSAA, an intermediate capture
averages opaque color samples and conservatively selects minimum depth. World
continues loading its original multisample color/depth; all views still resolve
once to the canvas before GUI. At 1x, a final color presentation pass copies the
accumulation target to the canvas. WebGL 2 uses complete RGB8/DEPTH_COMPONENT24
texture framebuffers and a same-size nearest color/depth blit; MSAA depth resolve
uses the driver's sample selection. No WebGL float-color extension is required.

Targets are private to the backends and recreated after resize/recovery.
Disabling both effects releases canvas-sized scratch storage; inactive scenes
keep the original pass count. A single affected view adds an opaque snapshot and
World continuation, plus a final presentation/blit at 1x. Memory estimates include
scratch color/depth. WebGPU World timing includes capture and continuation; final
presentation/resolve and GUI remain outside layer GPU timing. Asset upload budgets
do not govern these mandatory resize-dependent render targets. Mobile bandwidth
and memory costs must be measured on target devices.

## Underwater fog and absorption

Set `underwaterStrength` to [0,1] (default 0) for depth-based underwater rendering
on both backends. Other defaults are `underwaterFogDensity=0.12` per world unit,
`underwaterMaxDistance=100` world units and `underwaterColor=(0.015,0.12,0.16)` in
linear display light. Density is [0,100], maximum distance [0.01,10000], and color
components [0,1]; all fields must be finite. The existing `absorption` coefficients
also attenuate underwater colors per world unit. Strength scales optical path length. Fog density zero leaves absorption
only; absorption zero leaves scattering only. Strength zero, or density, absorption and caustics all zero, skips the underwater pass. Material instances
inherit or override the full validated settings value as usual.

`underwaterView(scene, eye, visibilityMask)` selects the first active World water
whose authored local XZ bounding rectangle contains the eye below local Y=0.
The signed distance must be more than 0.01 world units below the transformed
plane. Camera masks, disabled ancestors and component layers apply. Selection is
independent of the surface's camera frustum visibility, so looking down or away
from the surface does not turn fog off. The returned value owns a copy of settings,
footprint and inverse transform; it borrows no node or GPU resources. The mean
plane determines entry, without wave-driven flicker or a simulation tick.

The rectangle and lower half-space define a presentation region with no bottom.
This is suitable for a lake or pool with a rectangular footprint; arbitrary mesh
outlines, holes, caves and authored volume shapes are not inferred. Translated,
rotated, mirrored and nonuniformly scaled surfaces retain local orientation.
Overlapping water uses traversal order, with one selected region per camera.
Set the surface material `doubleSided=true` when its underside should be visible.
Water's existing surface shading remains an approximation without total internal
reflection or a physical underwater lighting model.

After World meshes, water and the transparent/particle queue, each submerged view
captures its completed color and World depth. A fullscreen pass reconstructs the
opaque endpoint or far-plane endpoint at clear depth. Perspective rays start at
the eye; orthographic rays start on their per-pixel near plane. Each segment is
clipped to local Y<=0 and the local X/Z bounds, then converted to world distance
and capped by `underwaterMaxDistance`. `underwaterDistance` exposes the matching
portable segment calculation. Upward rays stop at the surface, and sideways rays
stop at the footprint edge, rather than fogging the entire visible scene.

The shader decodes already tone-mapped scene color and applies exponential RGB
absorption plus exponential scattering toward `underwaterColor`, in linear display
light, without a second tone map. Larger path distance removes red more quickly
than blue with the default absorption. Transparent objects and particles are
composited before this step, but their fog distance comes from opaque/surface depth
behind them because they do not write a separate transparent depth buffer. There
is no volumetric light scattering, submerged camera collision or buoyancy.

Underwater rendering reuses the backend-owned water color/depth scratch targets;
underwater-only materials allocate them even when refraction/foam are off. Each
submerged view adds a snapshot and a composite before Overlay. Dry cameras skip
both, and Overlay/GUI colors remain untouched. Snapshots are shared serially across
ordered views with independent camera data and scissors. The same 1x/4x color/depth
resolve rules apply as for refraction; the fog pass writes the original scene color
attachment at its current sample count. Final canvas resolve/presentation and GUI
still happen once after all views. Pure underwater at 1x also requires the existing
single-sample presentation pass. No float-color extension is needed on WebGL 2.
Targets rebuild on resize/recovery and shrink when no effect needs them. Counters
include actual captures/draws/storage; WebGPU World timestamps include both added
passes. WebGPU caches unchanged underwater uniforms, including while paused.
Underwater settings survive archives and recovery; camera and clock policy
remain application-owned.

## Interaction ripples and moving-object wakes

Both backends accept up to 16 copied, transient `WaterInteraction` values through
`Renderer::setWaterInteractions(span)`. Empty input clears them. This is an optional
presentation mechanism; applications detect contacts, track objects and choose
emission cadence independently of physics or renderer visibility. For example:

```cpp
engine::WaterInteraction ripple;
ripple.position = contactWorldPosition;
ripple.startTime = simulationSeconds;
ripple.strength = .8f;
engine::WaterInteraction wake = ripple;
wake.position = movingObjectWaterContact;
wake.direction = objectWorldVelocity; // Normalized during staging, projected in the water plane.
wake.wakeLength = 4; // Zero selects an expanding circular ripple.
std::array inputs{ripple, wake};
renderer.setWaterTime(simulationSeconds);
renderer.setWaterInteractions(inputs);
```

Origins and directions are world-space. Defaults are direction (1,0,0), start time
0, strength .6, radius .7, wavelength .6, propagation speed 2, lifetime 4 seconds,
wake length 0 and visibility mask all bits. Finite ranges: origin/direction
components +/-1000000; start time uses the existing water-clock range; strength
[0,1], radius/wavelength [.05,100] world units, speed [0,100] world units/second,
lifetime [.01,60] seconds and wake length [0,100] world units. Wakes require a
nonzero direction. Radius controls ring packet width or the width of each V arm,
and also the maximum contact distance from the surface. The whole snapshot
validates before publication; an invalid value or count preserves previous inputs.

Staging computes age using the caller water clock, then drops future, expired,
zero-strength and camera-mask-excluded inputs. Sources fade quadratically over
lifetime. Rings expand at propagation speed; a wake trails its projected heading
with a finite, widening V envelope. Applications refresh a continuous wake with
current position, heading and start time, or retain an impulse until it expires.
No automatic object/velocity tracking or event history is retained. Rewinding the
clock re-evaluates the retained snapshot, including reactivating earlier inputs.
Ordered cameras see the same snapshot/time with independent mask filtering.

World water samples the procedural field in world units, perturbs surface normals
and adds bounded bright crests after refraction. This works with ordinary water
meshes, transforms, alpha modes and the existing reflection/refraction paths.
Origins farther than the source radius from a surface are rejected. Background
and Overlay water ignore interactions. The effect changes no vertices, bounds,
shadow geometry, collision, buoyancy or CPU picking. It does not simulate fluid
obstacles, wave transport, spray or interaction-driven underwater caustics.

No render passes or textures are added. WebGPU appends bounded numeric data to
its private per-view frame payload (2400 bytes total), preserving unchanged-frame
caching. WebGL 2 stages the same active values into fragment uniforms. CPU input
copies survive resize and device/context recovery; there are no borrowed node,
callback or resource lifetimes. A successful `Renderer::setScene` clears transient
inputs; `setViews` retains them. They are excluded from scene/material archives
and material cache identity, so archive format stays unchanged. Save/rebuild them
through application state if desired. Clear them explicitly when ending gameplay
without replacing the renderer scene.

Choose **Calm interaction pool** to isolate the effects from background waves.
The demo's **Object interactions** selector offers both effects, contact ripples,
a moving wake or off. A moving yellow object emits a directed wake and sampled
trailing impulses; a bouncing orange object emits contact rings. These paths and
emission decisions live in `src/water_demo.*`. Pause freezes object motion and
interaction ages; the Still water preset freezes their presentation too. Return
and scene replacement clear inputs; restart resets the demo clock and sources.
`scripts/water-interactions-smoke.cjs` checks real pixels for independent effects,
ring propagation, wake direction, expiry/future rejection, masks/elevation,
pause/resume, ordered perspective/orthographic views, MSAA, resize, recovery,
scene replacement, optional costs and moving-source demo controls.

## Animated underwater caustics

Both backends support optional procedural cellular light ridges on depth-reconstructed
World receivers while the camera is inside the selected underwater footprint.
Set `causticsStrength` to [0,4] (default 0), `causticsScale` to [0.01,100]
(default 2 cells per approximate world metre in the water plane), `causticsSpeed`
to [0,10] (default 1), and `causticsDepth` to [0.01,10000] world units (default 8).
All values must be finite. `underwaterStrength` scales the contribution. Zero fog
and absorption still allow a caustics-only material; zero caustics strength adds
no effect to existing water materials. Full instance overrides and material/batch
identity include these settings. Archive v13 stores them; v12 and older load with
caustics disabled. Renderer time remains application state.

The existing post-World composite reconstructs receiver position and a geometric
normal from depth derivatives. It rejects clear depth, positions outside the
footprint, dry receivers, and a local surface exclusion band of twice wave
amplitude plus 0.02 to avoid lighting the animated water itself. Projection uses
the water plane axes, compensating for their inverse-transform lengths; general
shear is approximate. Ridges move with `setWaterTime(seconds)*causticsSpeed`,
fade smoothly to zero at `causticsDepth` below the mean plane, and favor surfaces
parallel to that plane. They multiply decoded receiver color before the existing
RGB absorption and fog, so distant patterns attenuate with the scene.

This is an artistic screen-space approximation: it does not trace refracted
sunlight, consult lights/shadows, detect occluders above the receiver, or light
surfaces seen through water from above. Transparent/particle color uses opaque
depth behind it, as with fog; there is no separate transparent receiver buffer.
Overlay/GUI remain outside composition. No additional textures, captures, draw
passes or timestamp queries are needed beyond underwater composition. The private
view payload is 256 bytes; WebGPU caches unchanged values and updates caustic time
only when enabled. Speed zero freezes the pattern independently of surface waves;
application pause freezes the shared clock and recovery retains it.

The demo enables caustics on its sloping lakebed. Choose **Underwater** or **Deep
underwater**, then compare **Caustics off** and **Still caustics**. Existing fog and
absorption isolation presets disable caustics. `scripts/water-caustics-smoke.cjs`
checks receiver pixels, shared clock/static speed, depth fade, dry/ceiling rejection,
ordered views, perspective/orthographic cameras, 1x/4x, optional costs, resize,
paused recovery and archive restoration on both backends.

## Planar reflections

Set `planarReflectionStrength` to [0,1] (default 0) to capture nearby objects on
both backends. `reflectionStrength` also scales this contribution; zero disables
all reflection/highlight paths. `waterPlanarReflectionRequired(scene, mask)` gates
optional target allocation using active World water and camera masks. The first
camera-visible World water requesting reflections selects **one plane per view**,
from its transformed local XZ plane at local Y=0. Coplanar water shares the capture;
other planes retain their environment/analytic fallback. Planes must match within
0.01 world units and their normals within dot 0.999. This bounded policy avoids
unbounded per-surface passes. A wave crest does not move the capture plane.

`waterReflection(draws, eye)` returns the normalized camera-facing plane and mirror
matrix. Backends multiply the original view-projection by that matrix, preserving
perspective or orthographic lens/viewport settings without changing scene cameras.
Winding reverses once in addition to each object's mirrored transform. A fragment
clip uses a 0.002-world-unit bias above the camera-facing plane,
so submerged objects do not appear reflected. The capture uses opaque/masked World
batches, including retained geometry outside the main camera frustum, the same
visibility mask, material resources, skin/morph poses and selected mesh LODs.
There is no second simulation tick or independent reflection LOD selection.

The single-sample color/depth targets are canvas-sized and reused serially across
ordered views. Each view clears its capture to transparent, renders objects, and
consumes it before the next view. Empty capture pixels retain the existing sky/IBL
reflection. Water, transparent objects, particles, Background/Overlay layers and
GUI are excluded, preventing recursive feedback. No sky/cloud/probe rebake runs.
A separate capture binding samples a harmless fallback texture rather than the
active reflection attachment. Viewport half-texel clamps prevent cross-view bleed.

Projected samples receive a small normal/roughness-scaled distortion and Fresnel
weight. Already tone-mapped capture colors are decoded and composed in linear
display light before refraction and foam, with no second tone map. This is a sharp
planar approximation; there are no roughness mip chains, SSR, multiple bounces or
arbitrary per-plane reflection captures. Main rendering keeps its requested 1x/4x
MSAA; the reflection itself is always single-sampled. One affected view adds one
pass; color/depth memory estimates and WebGPU World timestamps include it. Targets
shrink to valid 1x1 fallbacks when no active camera mask requests them, rebuild on
resize/recovery, and stay private to each backend. Frustum-conservative preflight
can retain full-size storage when requesting water is offscreen.

## Reflections and clock ownership

WebGPU uses the existing roughness-filtered `Scene::environment` reflection when
its intensity is positive. With no active environment lighting, it uses the
material's horizon/zenith approximation. WebGL 2 always uses that approximation,
and supports the water-specific normal mapping and specular highlights even
though its general PBR/IBL capabilities remain unavailable. Both backends add
nearby objects when planar reflections are enabled. Changing reflection strength to zero disables reflected
sky/environment and water specular highlights.

Visible sky and environment lighting remain independent. No reflection probe
capture or automatic sky/cloud rebake is provided. Applications can match the
approximation colors to their sky or supply a matching environment on WebGPU.
The demo defaults to matching gradient colors on both backends; its Environment
preset demonstrates WebGPU IBL and retains the approximation on WebGL 2.

`Renderer::setWaterTime(double)` accepts finite seconds within +/-1000000 and
validates before publication. `waterTime()` returns the supplied value. Time
starts at zero, survives device/context recovery and is shared across ordered
views. The renderer never advances it. Choose simulation or presentation time
explicitly; do not advance it once per view. The playground advances it once in
`afterSimulation`, so pause and page suspension freeze water. Changing scenes
does not implicitly reset the renderer clock.

Water settings and optional instance overrides are preserved in scene/asset/
application archives. Version 9 adds planar strength; version 8 retains depth-aware
water with planar capture off, and version 7 retains animated reflective water
with refraction/foam off. Formats 1-6 retain original PBR/unlit behavior. The current
writer uses version 14, which also includes terrain/material-layer records. Version
11 adds underwater settings; formats 1-10 keep underwater rendering disabled. Renderer clock policy/time
is not part of a scene archive; saved games should store their chosen time in
application state if required. Backend handles never enter archives.

## Demo and validation

Open `?scene=water&renderer=webgpu` or `?scene=water&renderer=webgl2`, or select
**Open water demo** in the inspector. Drag to orbit, scroll to zoom, and use
**Pause motion**, **Reset camera**, the surface presets and **Return to demo**.
The demo includes a sloping patterned lakebed, submerged markers, rock contacts
a pier and tall colored reflection landmarks. Planar reflections off compares
the object capture with the sky/environment fallback; Still planar mirror isolates
object reflection on flat water. The Camera views selector offers an oblique
orthographic inset to inspect the same surface from a second angle. Presets independently disable refraction, foam or all scene-input
effects; Still water freezes the full water clock-driven appearance.
The View selector moves between above water, underwater and deep underwater.
White submerged markers expose depth attenuation, and presets independently
disable underwater fog, absorption or the whole underwater effect.
Returning restores the previous camera transform/lens and orbit, scene-node enable states, sky, ambient,
environment intensity and orbit framing. Restart resets wave time and appearance.

Build with the repository-local Emscripten version from `.emscripten-version`.
Native CTest `water` covers validation, atomic overrides, displacement bounds,
binding/batching identity and scene/asset archive round trips. Run `material`,
`scene_archive`, `persistence`, `sky` and `render_frame` for affected contracts.
`scripts/water-smoke.cjs` uses Playwright/pngjs and `DEMO_URL`,
`BROWSER_EXECUTABLE`, `NODE_PATH`, optionally `EVIDENCE_DIR`. It verifies real
pixels on both forced backends, isolated waves/ripples/reflections, pause/resume,
invalid edits, alpha and mirrored pipelines, MSAA, perspective/orthographic ordered
views, resize, recovery, scene archive save/load/return, demo return/restart
and orbit. Query `waterTest=1` enables the private deterministic clock fixture.

`scripts/water-depth-smoke.cjs` additionally checks actual refracted/absorbed
pixels, isolated shoreline foam, strong-distortion foreground rejection, 1x/4x
capture, perspective/orthographic views, resize/recovery and optional target/pass
costs. Query `waterTest=1` enables deterministic private depth fixtures.

`scripts/water-planar-smoke.cjs` checks reflected object pixels, below-plane
rejection, optional pass/storage costs, main MSAA, ordered perspective/orthographic
views, resize/recovery and scene archive restoration on both backends.

`scripts/water-underwater-smoke.cjs` checks distance-dependent fog, spectral
absorption, capped paths, plane/footprint exits, optional captures, above/below-water
ordered cameras, Overlay, 1x/4x, orthographic projection, resize/recovery and actual
scene archive restoration on both backends.

Limits: physically traced caustics, FFT oceans and fluid simulation are not implemented. Screen-space
refraction cannot reconstruct hidden/offscreen or transparent geometry, and
multiple water surfaces do not refract each other. Transparent water retains the existing mesh-origin sorting
limitations. GPU displacement does not alter collision, buoyancy or CPU triangle
picking; applications own those independent simulation policies. Desktop Chrome
verification does not establish physical Android/iOS acceptance or sustained
mobile performance.

See [the recorded desktop validation](water-validation.md) for the tested build
identity, checks actually run and acceptance scope. The existing baked material
graph does not author animated water parameters; use the C++ material API.

## Scene atmospheric fog

Optional [height fog](fog.md) applies to intrinsic water shading before mixing captured inputs. Refraction samples already fogged opaque color. Planar captures integrate the real two-leg eye/near-plane-to-water-to-receiver path; captured fog is not applied again at the surface. Foam/body scattering use surface fog. Underwater remains a later independent depth composite, with its existing transparent-depth limitation. Scene atmospheric fog is not clipped out of underwater regions.
