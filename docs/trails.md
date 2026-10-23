# Ribbon trails

Ribbon trails are the first delivered increment of FX milestone 3. Set
`ParticleEmitterDefinition::renderMode = ParticleRenderMode::Ribbon` on an existing
`ParticleEmitterComponent`, or create `ribbonEffect()` in the editor. The component
samples its moving origin once per scene variable update, independently of cameras,
visibility, culling and rendering. Billboard mode remains the default.

```cpp
auto definition = std::make_shared<ParticleEmitterDefinition>();
definition->renderMode = ParticleRenderMode::Ribbon;
definition->space = ParticleSpace::World;
definition->trailMaxPoints = 256;
definition->trailLifetime = 2;
definition->trailMinDistance = .04f;
definition->trailBreakDistance = 3;
definition->trailTextureLength = 1;
definition->size = {{0, .3f}, {1, .02f}}; // Full strip width over point age.
auto& trail = scene.addChild<ParticleEmitterComponent>("Trail", definition);
// Move trail.transform.position before scene.update(chosenDelta).
// Call trail.breakTrail() when gameplay starts a separate stroke.
```

## Sampling and ownership

Each emitter owns one fixed-capacity ring, accessed oldest-first through
`trailPointCount()` and checked `trailPoint(index)`. Defaults are 256 points,
1 second lifetime, .05 sample distance, 5 teleport break distance and 1 texture
repeat length. Capacity is 2-4096; lifetime is positive and at most 86400 seconds;
sample distance and repeat length are at least .0001, and break distance must be
at least sample distance. All values must be finite. Invalid definitions preserve
the previous definition/runtime state.

Distance sampling interpolates the straight path and birth times between scene
updates, retaining residual distance. It cannot infer motion between game updates.
The first active tick seeds the current source without inventing past movement.
There are at most 256 new distance samples per update. Longer sampling gaps start a
new strip at the current endpoint. Capacity evicts oldest points, and lifetime
expires them. `droppedParticles()` includes evicted/dropped trail samples in Ribbon
mode; `particles()` contains billboard records only. An additional moving endpoint
is derived for drawing between distance samples and is not a simulation particle.

`ParticleSpace::World` stores world positions and birth-time width scale. Later
hierarchy movement does not move old points. `Local` stores positions in the emitter
parent's local coordinates: moving the emitter relative to its parent draws a
path, and moving the parent carries that path. This differs from local billboard
particles, which follow the emitter itself. Local width follows the largest parent
axis scale; world width captures the largest source axis scale. Distances use the
selected space's units. A singular local source matrix suppresses drawing.

The existing scene update clock owns point ages. Pause freezes them; resume starts
a separate strip. Stop without clear drains saved points; clear/restart empties the
ring. Disabling a subtree freezes simulation and hides drawing while preserving
history, including undo detachment. Explicitly call `breakTrail()` across disabled
movement when a connecting segment is unwanted. Movement beyond break distance
also starts a strip. Looping keeps sampling until stopped; duration stops one-shot
sampling, and startDelay postpones its first sample. Rate, bursts, birth shapes,
velocity/force, rotation, alignment and flipbook settings do not drive Ribbon
geometry. Shared definition validation still applies to those fields.

## Rendering

`SceneFrame::ribbons` carries generic strip segments with endpoint colors, widths,
tangents and texture coordinates. `FxFrame` expands each segment to six 48-byte
vertices for the selected camera, with shared endpoint tangent directions and a
finite camera-parallel fallback. It does not mutate simulation. Size and RGBA curves
use point age divided by trail lifetime. The whole texture repeats by accumulated
path length along U and spans the width along V; repeat phase is rebased per segment
to bound UV precision. Ribbon mode does not use flipbook cells.

Both WebGPU and WebGL2 use the World transparent queue, depth test without depth
writes, per-emitter masks, alpha/additive blending, scene fog and optional
`softDistance` opaque-depth fading. Segment centers sort with transparent meshes
and billboards; intersecting transparent geometry has the existing approximate
sorting limitations. Soft capture has the same excluded transparent/water receiver
limitations and cost as billboard FX. WebGPU retains geometrically grown per-view
buffers and skips unchanged vertex uploads; WebGL2 streams compatible batches.
Counters expose `trailSegments`, `trailDrawCalls`, `trailUploadBytes`.

This implementation supplies simple camera-facing joins, without miter expansion,
round end caps, per-particle subtrails, motion-vector rendering, collision, decals,
ground-aligned particles or mesh geometry within a ribbon. Separate
[plane](plane-particles.md) and [mesh](mesh-particles.md) particle modes are now
delivered; advanced trail joins, collision and decals remain future increments.

## Authoring and persistence

Scene editor -> Details -> Particle FX includes the Ribbon trail preset and
rendering mode, point capacity, lifetime, sample distance, teleport distance and
texture repeat controls. Width/color use the existing curve editor; textures,
blend and soft fade use existing appearance controls. Edits use immutable assets
and normal undo/redo. Isolated `EffectPreview` moves ribbon emitter sources along
an optional orbit in its own scene. `setTrailMotion(bool)` and the Move trail source
checkbox control only that preview; source assets and scene playback stay unchanged.

Archive version 19 appends ribbon settings and full live ring/sampler state to the
existing codec. Versions 1-18 load as Billboard. Scenes, assets, `.wefx`, cloning,
editor Play and application envelopes share the codec. Live rings validate bounds
and chronology before publication; saved IDs persist while runtime handles rebuild.
Host and worker codecs must be deployed together. Preview orbit controls are transient.

Build with `./scripts/build.ps1 -Configuration Release`, using repository-local
`.tools/emsdk` and `.emscripten-version`. Build native `trail_tests`, `fx_tests`,
`editor_fx_tests`, `persistence_tests`, `render_frame_tests`; run CTest `trail`, `fx`,
`editor_fx`, `persistence`, `render_frame`. Windows may need the configured compiler
bin directory on PATH. Run `scripts/trail-smoke.cjs` and
`scripts/trail-depth-smoke.cjs` on the exact staged host/Wasm using Playwright/pngjs,
`BROWSER_EXECUTABLE`, `NODE_PATH`, `DEMO_URL`, `EVIDENCE_DIR`; the editor suite accepts
`DEVICE_SCALE_FACTOR=2`. See [validation](trails-validation.md).

Static [mesh particles](mesh-particles.md) are now delivered with shared materials,
3D spin and archive v21 state. Soft fading remains a quad/ribbon capability.

Optional particle collision and independent projected decals are now delivered; see [contact/decal contracts](fx-contacts-decals.md) for responses, queued output, authoring, budgets and persistence. Rigid-body particles and PBR decal channels remain outside this increment.

Explicit SurfaceTrail mode is now delivered for world contact samples and supplied
normals. It reuses bounded trail storage/rendering with camera-independent surface
orientation, an isolated sloped preview and v28 live-normal persistence. The Ribbon
alignment and sampling contract above remains unchanged. See
[surface-following trails](surface-trails.md).


Native Dawn and direct D3D12 support this presentation contract; see
[native FX scope and validation](windows-fx-rendering.md).
