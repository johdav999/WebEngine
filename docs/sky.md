# Sky backgrounds, spheres and weather

`Scene::setSky(std::optional<SkyBackground>)` atomically publishes validated
presentation settings. Reusable scenes default to sky off; `std::nullopt` removes
it. Invalid modes, nonfinite or out-of-range data, zero sun direction and malformed
panorama pixels throw before replacing valid settings. Published textures and mesh
assets remain immutable. Browser/GPU types stay private to the renderer backends.

These settings also render on native Dawn and direct D3D12 (milestone 3), using
the same prepared sky payload, panorama color semantics and explicit clocks.
See [native backend scope](windows-d3d12.md) and its environment acceptance runner.

```cpp
engine::SkyBackground sky;
sky.geometry=engine::SkyGeometry::Sphere; // Or Fullscreen.
sky.mode=engine::SkyMode::Atmosphere;      // Or Gradient/Panorama.
sky.clouds.enabled=true;
sky.clouds.coverage=0.58f;
sky.dayNight.enabled=true;
sky.dayNight.hour=9;
sky.dayNight.daySeconds=180;
scene.setSky(sky);

// Call once per selected clock step, after simulation and before rendering.
// Scene and directional light are caller-owned; the handle is scene-scoped.
engine::advanceSky(scene,simulationDelta,sun.id());

engine::RenderView mainView{&mainCamera};
engine::RenderView overlayView{&overlayCamera,std::nullopt,false};
// Third argument suppresses sky independently of color clearing.
```

## Geometry and per-view behavior

Both WebGPU and WebGL 2 draw sky after a view's color clear and before Background
objects. World and Overlay follow; GUI still draws once after all views. Sky
never writes depth. It respects viewport/scissor and `RenderView::skyEnabled`,
which defaults true independently of `clearColor`. Set it false on load-color
views that must preserve an earlier background. Disabled cameras skip drawing.

`Fullscreen` submits one triangle. `Sphere` submits a shared 64-segment,
32-ring unit sphere, with inward winding/normals and an unlit `Material` through
`skySphereMesh()`. Its 3,968 triangles use the existing mesh/resource upload
representations. A camera-relative matrix centers each draw on its own viewing
camera without changing an actor transform or subtracting large world positions
in shaders. Shader clip depth is forced to the far plane; scene near/far distances
do not clip the sphere. Orthographic extent includes the lens aspect and affine
camera axes, while shading uses parallel forward rays. Sphere and fullscreen
modes share the same sky fragment model.

The sphere is a renderer presentation mesh, excluded from `Scene::collect`,
shadows, picking, physics, culling, LOD and gameplay node counts. It is not a
movable gameplay actor. Backends own pipelines/bindings and reuse existing upload
budgets/caches; a deferred sky texture/mesh retries on a later frame. Context or
device recovery recreates GPU resources from retained CPU settings and assets.

## Gradient and panorama modes

Gradient `horizon`, `zenith` and `nadir` are nonnegative linear radiance. Shading
interpolates towards zenith/nadir by the viewing direction's positive/negative Y
coordinate. The gradient is symmetric about world Y, so yaw rotation has no
visible effect. Brightness multiplies radiance in every mode; output uses Reinhard
mapping and 1/2.2 display encoding. Brightness zero produces opaque black.

Panoramas are top-down sRGB RGBA8, up to 4096 pixels per dimension; a 2:1 image is
recommended. Set `sky.panorama` to a shared immutable `Texture` and use
`SkyMode::Panorama`. Alpha is ignored. Y-axis `rotation` is radians, matching IBL
rotation. Sampling uses `u=atan2(direction.z,direction.x)/(2*pi)+0.5` and
`v=acos(direction.y)/pi` after rotation, with horizontal repeat and vertical clamp.
Both backends decode before bilinear interpolation and sample mip level zero to
avoid longitude-seam derivative artifacts. HDR/EXR file decoding is not provided.

## Atmosphere and clouds

`Atmosphere` uses an Earth-size spherical atmosphere (ground radius 6360 km,
outer radius 6460 km), Rayleigh and aerosol density scale heights of 8 and 1.2 km,
and bounded numerical single-scattering integration: eight view intervals and
four sunlight intervals per view sample. Optical transmittance attenuates the
sunlight; planet intersection blocks sunlight in shadowed samples. Rayleigh and
Cornette-Shanks aerosol phase functions follow the physical formulation described
in [Bruneton's atmosphere reference](https://ebruneton.github.io/precomputed_atmospheric_scattering/atmosphere/functions.glsl.html).
This is an independently implemented approximation, not the reference's
precomputed multiple-scattering pipeline.

Settings expose Rayleigh/Mie strength, aerosol anisotropy, observer altitude in
km, world-space direction towards the sun and solar intensity. Translation of the
game camera does not change altitude: the caller sets the atmospheric observer
height explicitly. The sun direction is normalized when preparing a view. The
model includes a sun disc, a simple opposite moon disc and a dark night floor.
Below the horizon, scattering fades into the authored nadir color. Panorama yaw
rotation does not rotate this physical model; set sun direction or cycle azimuth.

Clouds are an optional two-dimensional procedural layer on any sky mode. Five
value-noise octaves form density; coverage controls the threshold, density
controls opacity, and scale controls frequency. Wind advances an explicit,
bounded noise-domain offset through `advanceSky`. Sun elevation tints clouds at
dawn/dusk/night. The cloud layer fades near the horizon to bound projection
frequency. It is not volumetric, does not cast world shadows, and does not add
physics or precipitation simulation. Multiple scattering and object aerial
perspective are also outside this implementation.

## Day/night ownership, clocks and lighting

`advanceSky(scene,deltaSeconds,directionalLightId)` copies/validates a candidate,
resolves the optional light through the scene's generation-checked handle, and
validates its inverse transform before publishing anything. Negative/nonfinite
delta, stale/foreign handles, non-directional lights and singular transforms are
rejected without partial changes. No callbacks/subscriptions or retained scene
pointers are introduced. An omitted light handle updates sky/ambient/IBL settings
without changing a light component. With no configured sky, the call is a no-op.

`SkyDayNight::enabled` opts into ownership of `Scene::ambient`,
`environmentIntensity` and the supplied light's direction/color/intensity. Its
24-hour orbit has sunrise at 06:00, noon at 12:00, sunset at 18:00 and midnight at
00:00. Azimuth rotates that orbit. Direct light changes from warm dawn/dusk sun to
cool, weaker moonlight at night. This is an artistic daily orbit, not astronomical
latitude/date/time-zone modeling. Disabling the policy leaves its last lighting
values intact; callers restore alternative lighting policy explicitly.

`running=false` holds the hour while cloud wind can still advance. A zero clock
step holds both hour and cloud phase, but can apply manually edited lighting.
Large positive steps wrap without hidden-time catch-up loops. Applications choose
simulation or presentation time. The playground supplies simulation delta after
simulation: global Pause and suspension freeze both wind and time of day. Editing
time while paused applies the selected lighting on the next frame.

Visible sky and `Scene::environment` remain separate resources. Automatic lighting
scales the existing IBL intensity; it does not rebake panoramas or synchronize
reflection direction with the moving sun. WebGL 2 still lacks IBL, but supports the
sky, ambient/directional light changes and cloud animation.

## Archives, playground and validation

Format-6 archives retain sky geometry, atmospheric parameters, cloud settings/wind
phase, cycle configuration/hour, panorama resource sharing and view sky flags.
The sphere's generated mesh/GPU objects and caller's sun handle are recreated,
not serialized. Formats 1-4 load with sky off. Format 5 loads its original gradient
or panorama with fullscreen geometry, clouds and automatic lighting disabled.

The playground starts with a mesh sky sphere, atmosphere, clouds and a 180-second
day. The material inspector's **Sky background** section controls geometry, mode,
brightness, panorama rotation/upload, cloud coverage/density, scattering/haze,
time, speed and whether the cycle runs. The clock label follows actual scene state.
Local panorama reads cancel if controls change or the target scene is replaced.

`sky_tests` checks inward geometry, affine/translation invariance, clock wrapping,
light handle rejection, atomic state and archive compatibility. Run
`scripts/sky-smoke.cjs` for original gradient/panorama behavior and
`scripts/sky-weather-smoke.cjs` for real WebGPU/WebGL 2 geometry, clipping, views,
atmosphere, clouds, lighting, pause and recovery. See
[recorded validation](sky-validation.md). Desktop Chrome checks do not establish
physical Android/iOS acceptance or sustained mobile performance.
