# Dynamic diffuse lighting and reflections

## Dynamic diffuse probes

`DynamicDiffuseGi` in `engine/dynamic_gi.hpp` computes one to four diffuse
bounces in one bounded world-space `DiffuseGiVolume`. `bounces` defaults to one.
Each probe stores cosine-weighted
incoming light along six axes and directional hit-distance moments. Runtime
shaders blend the nearby probes and reduce the influence of probes across
occluders. The volume fades into the existing baked diffuse environment near
its edge. Direct and specular lighting retain their own paths.
WebGL2 basic lighting applies the probe contribution alongside its flat ambient.

The caller owns the update clock, proxy geometry and light selection. Build
world-space `DiffuseGiTriangle` proxies with diffuse reflectance and emission
in linear space. Use `replaceStatic` for stable geometry and `replaceDynamic`
for moving geometry. Supply a priority point when moving an occluder to refresh
nearby probes early. The tracer uses separate CPU BVHs and an explicit sky
radiance color. It shades hits with bounded punctual-light visibility rays.
Geometry and material inputs are opaque proxies. Alpha cutouts, texture
evaluation and skinned geometry updates require additional integration.

Each later bounce samples the previous complete probe sweep. The direct light
and emission are evaluated once at the hit, then reflected incoming diffuse
light is added. The source sweep stays frozen while the next one updates, so
changing the number or order of updates per frame produces the same result.
Geometry, light, sky or configuration changes restart the diffuse solve.
`completedSnapshot()` returns the latest complete sweep of the current solve,
and the snapshot's `completedBounces` records its completed transport depth.

The limits are 128 probes, 4,096 static and 512 dynamic triangles, eight lights,
24–96 primary rays per updated probe and one to eight probe updates per call.
`DiffuseGiVolume` defaults to an 8×4×4 grid, 48 primary rays per probe and four
probe updates per call. Each primary hit may launch visibility rays. Choose
budgets from measured CPU work and response time on target hardware. The tracer
limits each batch to one million traversal and triangle tests. A batch that
exhausts the limit throws and leaves the last complete snapshot published.

Call `update` at an idle presentation boundary. A completed batch publishes an
immutable `DiffuseGiSnapshot`. Pass that shared snapshot to `Scene::setDiffuseGi`
when `update` returns true. The default scene has no snapshot. Publish `nullptr`
to restore baked diffuse lighting. Rejected settings or proxy updates preserve
the previous usable data. Probe snapshots are transient and renderer bindings
rebuild on device or context recovery. The application can regenerate a volume
from authored geometry after loading a scene. This state stays independent of
physics and the simulation clock.

`traceRadianceBatch` reuses the same proxy BVHs for up to 256 explicit rays.
`traceScene()` supplies an immutable shared geometry lease with its own
`geometryRevision()`. A lease retains its geometry and trace distance after the
original owner changes or is destroyed. It accepts the same lights, sky and an
optional diffuse snapshot. This keeps longer captures coherent without copying
the BVHs or retaining a borrowed scene owner.

## Off-screen reflection captures

`DynamicReflectionCapture` in `engine/dynamic_reflections.hpp` traces a cubemap
from live proxy geometry and lighting. Its completed immutable
`ReflectionProbeSnapshot` contains a prefiltered `Environment`, capture position,
world influence bounds, intensity and blend distance. Publish it through
`Scene::setReflectionProbe` to enable one reflection volume. Publish `nullptr`
to restore the existing environment reflections. Probe snapshots remain transient.

Configure `ReflectionCaptureSettings` with a validated environment as the BRDF
source. `size` defaults to 16 and accepts powers of two from 4 to 32.
`workPerUpdate` defaults to 128 and accepts 1–256 primary geometry rays or cubemap
filter taps per call. Primary hits can also issue the shared tracer's bounded
per-light visibility queries. `convolutionSamples` defaults to 16 and accepts 1–32.
`rayDistance` defaults to 30 world units. The capture reuses the supplied BRDF
LUT and publishes after the whole cubemap and roughness mip chain complete.
`progress()` reports completed and total work plus pending status.

Call `update` with the GI tracer, lights, sky and optionally
`gi.completedSnapshot()`. A capture pins one immutable geometry lease and its
lighting inputs for a cycle. Geometry, light and completed diffuse changes queue
the next cycle, allowing captures to finish during continuous motion. This
introduces bounded update latency determined by capture size and work budget.
`invalidate` requests a fresh cycle explicitly.

Full PBR and compact WebGL2 sample the cache with box projection and an edge fade.
The global environment and two authored lighting zones can coexist with the
reflection volume. The shader uses the existing environment texture bindings.
This is a spatial reflection approximation from one capture position. Nearby
parallax, visibility and angular detail depend on the influence box and cubemap
resolution. Screen-space hits can supply additional visible detail.

## Screen-space reflections

`RenderQuality::screenSpaceReflections` defaults off. Enable HDR scene color
with `post.enabled` for reflections. WebGL2 also needs its compact PBR tier.
The first implementation targets perspective views and opaque or masked World
PBR materials with metallic at least .25 and roughness at most .5. The visible
reflection fades with sampled roughness, screen edges and trace distance.
WebGL2 compiles its reflection shader variant on first use.

The renderer captures opaque color and depth before drawing participating glossy
receivers. It uses a bounded depth trace against the capture and falls back to
the reflection cache when available, then the existing specular environment,
for misses. The capture uses existing
scratch resources. Water and soft particles can take a later capture with the
glossy geometry included. Screen-space rays can sample visible geometry.
The geometry-traced cache provides off-screen content. Reflections between
participating glossy receivers use the same cache approximation. Read `screenReflectionSnapshots` in frame performance
data to see whether a capture ran.

## Workshop and verification

Open `?scene=dynamic-gi`. Close the door with the camera and ceramic sample fixed
to block the bounce from a red panel outside the camera view. Disable dynamic
bounce to compare the baked environment. Choose one to four diffuse bounces
to compare the ceramic's upper surface under the white canopy. Enable off-screen
reflections to see the red panel in the angled mirror, then close the door to
occlude that source. Screen-space reflections add the green source in the floor.

On Windows launch `webengine_lighting_windows.exe --backend dawn --dynamic-gi open`
or use `--backend d3d12`. Use O for the door, L for dynamic diffuse lighting,
B to cycle bounce counts, F for off-screen reflections and R for screen-space
reflections. The CLI also accepts `--dynamic-gi closed`, `--dynamic-gi baked`,
`--bounces 1|2|3|4`, `--offscreen-reflections`, and `--screen-space-reflections`.

Build `web_assets` with the pinned repository-local Emscripten SDK. Run
`node scripts/indirect-lighting-smoke.cjs` against the local site on forced
WebGPU and WebGL2. Run the Windows `dynamic_gi`, `dynamic_reflections` and
`environment_lighting` CTests and
`python scripts/indirect-lighting-windows-smoke.py <path-to-webengine_lighting_windows.exe>`.
The original `dynamic-gi-smoke.cjs` and `dynamic-gi-windows-smoke.py` retain the
one-bounce and screen-space fallback regression checks.
Compare the same ceramic region with the door open, closed and baked, and compare
the same frame with reflections enabled and disabled. Report CPU probe update
time and the time or number of frames for the field to settle separately from
total frame time. Desktop screenshots and capture timings do not establish
sustained performance on physical Android or iOS devices.

## Validation recorded 2026-11-07

The integrated Release `dynamic_gi`, `dynamic_reflections` and
`environment_lighting` CTests passed on Windows. The transport and reflection
tests also passed as standalone builds under repository-pinned Emscripten 6.0.11
and SDK Node. These cover frozen bounce inputs, deterministic scheduling,
geometry/light invalidation, bounded work, complete capture publication,
continuous-motion coalescing and immutable geometry lease lifetimes.

The desktop browser pixel suite passed on WebGPU and WebGL2. It compared one
and three bounces, verified that all source-panel corners were outside the
camera, tested door occlusion and reopening, combined SSR with the cache,
restored baked specular lighting after opt-out and recovered the GPU resources.
At the canopy comparison region, mean red increased from approximately 33 to
39 on both renderers. The mirror changed from approximately RGB (68, 71, 76)
with baked reflections to (206, 54, 40) with the off-screen source, then nearly
black when the door blocked it. These are display-space pixel comparisons,
not radiometric measurements.

Native Windows pixel comparisons passed on both Dawn and direct D3D12 using an
NVIDIA GeForce RTX 5060 Ti. Both produced approximately 33 versus 39 in the
canopy's red channel for one versus three bounces, and the off-screen mirror
mean was RGB (205, 53, 39). Door blocking and combined SSR/cache checks passed.
Observed peak combined CPU update costs were 0.88 ms for Dawn and 1.29 ms for
direct D3D12 across the captured cases. Native device-loss recovery with the new
cache was not exercised in this run; browser recovery was. Physical mobile and
sustained mobile performance were not tested.

The browser's observed peak combined diffuse/capture CPU update was 2.0 ms on
WebGPU and 1.4 ms on WebGL2 in this run. The workshop's 128 probes at eight
updates per call take 16 calls per diffuse bounce, or 48 for three bounces.
A size-16 reflection cycle with 16 filter samples and a 256-unit update budget
takes 38 calls; changed diffuse inputs can queue another complete cycle.
These counts describe settling latency separately from per-call CPU cost and
total frame time. Evidence is under `build/evidence/indirect-lighting-webgpu`,
`build/evidence/indirect-lighting-webgl2` and
`build/evidence/indirect-lighting-windows`.
