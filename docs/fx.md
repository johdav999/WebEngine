# Particle FX

`ParticleEmitterComponent` owns a bounded CPU pool and seeded playback; shared
`ParticleEmitterDefinition` and `EffectAsset` resources are immutable after publication.
`EffectComponent` composes up to 64 emitters using protected children. Existing
play/restart, pause/resume, draining stop, clear, local/world space, curves, timed
bursts, masks and flipbooks remain supported. See the README FX section for basics.

## Shaped emission and motion

```cpp
auto d = std::make_shared<engine::ParticleEmitterDefinition>();
d->shape = engine::ParticleShape::Cone;
d->radius = 0.15f;
d->coneAngle = 0.3f; // Half-angle in radians, around emitter-local +Y.
d->speedMin = 3; d->speedMax = 5;
d->velocityMin = d->velocityMax = {}; // Shape speed adds to the existing velocity range.
d->gravity = {0, -3, 0};
d->drag = 0.3f;
d->wind = {0.4f, 0, 0};
d->turbulence = {0.15f, 0.08f, 0.15f};
d->turbulenceFrequency = 1;
d->alignment = engine::ParticleAlignment::Velocity;
d->stretch = 0.08f;
d->softDistance = 0.25f;
auto& emitter = actor.addComponent<engine::ParticleEmitterComponent>("Spray", d);
```

All shape positions are emitter-local before `ParticleSpace` conversion. Existing
`positionMin/Max` supplies an additional box offset for every shape. Box is the
legacy default and uses +Y for optional shape speed. Sphere samples uniform volume
(`surfaceOnly=true` samples its shell) and emits speed radially. Disc samples area
uniformly in XZ and emits speed along +Y. Cone samples the same disc with independent
uniform solid-angle directions within `coneAngle` (0 to pi/2). Radius can be zero.

MeshSurface samples static `emissionMesh` triangles in proportion to local triangle
area, then samples barycentric position uniformly and emits along the geometric face
normal. It uses the existing immutable Mesh resource, with at most one million
triangles; indices, finite positions and nonzero total area are validated. Degenerate
triangles have zero probability. No skin/morph evaluation or presentation LOD enters
sampling. Nonuniform hierarchy scaling changes world-space sample density. Each
emitter derives its own triangle-area CDF when configured, also after archive load.

`drag` is a nonnegative rate per second, bounded to 1000. Wind is the target velocity
for linear drag: `dv/dt = gravity + force + turbulence - drag * (v - wind)`.
Wind has no effect when drag is zero. `forceOverLife` is an optional vector acceleration
curve; `velocityOverLife` is an optional additive displacement velocity curve, which
does not accumulate into base velocity. Both use ordered normalized-age `VectorKey`
values (1 to 256 keys when present), with linear interpolation and endpoint clamping.
Gravity, wind, turbulence and both curves use the selected simulation space. World
particles capture emitter-transformed birth position/velocity; subsequent motion
fields are already world-space values. Local particles follow the hierarchy.

Turbulence is a nonnegative per-axis acceleration amplitude for deterministic smooth
sinusoidal gusts with per-particle seeded phase; frequency is cycles/second (0 to 1000).
It is an artistic field, with no fluid/collision solver or spatial noise texture.
Constant acceleration/drag integrate analytically. Curves/gusts use midpoint intervals
near 1/60 second, capped at 128 intervals per particle per update; unusually large
updates are approximate, and varying-field motion is not an exact frame-partition
invariant. Pools, emission budgets, pause and disabled-hierarchy behavior are unchanged.
New settings default to legacy Box/camera-facing behavior with motion additions off.
Invalid definitions preserve the current definition and living particles.

## Velocity-stretched billboards

Velocity alignment projects current world velocity into each camera plane. The
billboard's long axis follows that projection, with authored rotation added. Width
remains the size curve value; length is `size + worldSpeed * stretch`. Stretch is in
seconds, defaults zero and has no effect with Camera alignment. Camera-parallel or
zero velocity falls back to a stable square with authored rotation. This is per-view
presentation; it never changes simulation position, lifetime or emission capacity.

`FxFrame` retains center-based alpha ordering shared with transparent meshes and
batches only compatible consecutive particles. The WebGPU instance payload is now
112 bytes (the earlier shaped/soft increment used 80); WebGL 2 builds equivalent CPU billboard vertices and retains
its existing individual particle draws. Alpha/additive effects depth-test without
writing depth. Fog applies before blending as before.

## Soft particles

`softDistance` defaults zero. A positive value fades alpha and additive radiance by
`clamp((opaqueViewDepth - particleViewDepth) / softDistance, 0, 1)` in world view-depth
units. Clear-depth pixels retain full opacity. Perspective and orthographic cameras
reconstruct world positions using their own inverse view projection and viewport.

Both backends reuse renderer-owned opaque color/depth scratch targets introduced
for water. Active masked soft emitters conservatively request targets, including
paused/stopped emitters; disabled subtrees and absent camera masks do not. A view
with soft particles snapshots opaque World depth before transparency, sharing a
snapshot with water transmission when both need it. WebGPU resolves minimum MSAA
depth; WebGL 2 uses matched depth blits. Single-sample scenes use accumulation plus
presentation; no scratch capture is added when all consumers are disabled. The
existing 1x1 fallback resources remain. Views consume snapshots serially, with resize
and device/context recovery rebuilding private bindings. Existing pass/texture/depth
counters include these costs; a soft-only view adds snapshot and continuation passes,
plus final presentation at 1x. This milestone captures color alongside depth rather
than introducing a separate optimized depth-only allocator.

Particles and transparent meshes do not contribute to soft depth. Water requesting
scene inputs is excluded from the shared opaque capture. Background, Overlay and
GUI are excluded, and remain outside fading. Soft depth does not provide collision,
transparent-surface intersections, sorting fixes or volumetric lighting.

## Delayed starts

Optional `startDelay` (0–86400 seconds, default zero) postpones the first emission
cycle. It applies once, then loops use duration normally. Rates and burst ages
exclude delay. Archive v18 adds delay; older formats default it to zero.

## Persistence and validation

Archive format 17 appends the new emitter settings/curves/static surface reference
and each live particle's turbulence phase. Older formats retain legacy defaults.
The derived surface CDF is rebuilt, while pools, RNG, playback and phases continue.
Scene saves, asset libraries, cloning and isolated editor Play use the same contracts.
Deploy matching browser host/worker codecs. The old common-component snapshot remains
separate from full scene archives. A standalone JSON loader is not provided. The scene editor now supports effect
authoring, isolated previews and self-contained .wefx files; see [FX authoring](fx-authoring.md).

The playground **Visual effects → Fountain and mist** preset exercises the new
features with existing Play/Pause/Resume/Stop/Clear and flipbook-import controls.
`fountainEffect()` is an example preset; application contact/gameplay emission policy
stays outside the particle system.

Build with `./scripts/build.ps1 -Configuration Release` using `.tools/emsdk` pinned
by `.emscripten-version`. Build native targets `fx_tests`, `scene_archive_tests`,
`persistence_tests`, `water_tests` and `terrain_tests`; run matching CTest names
`fx`, `scene_archive`, `persistence`, `water`, `terrain`. On Windows put the configured
native compiler runtime directory on PATH when required. Browser acceptance:

```powershell
node scripts/fx-smoke.cjs
node scripts/fx-milestone-smoke.cjs
node scripts/water-depth-smoke.cjs
```

Use `NODE_PATH` for Playwright/pngjs, `BROWSER_EXECUTABLE` for Chrome and `DEMO_URL`
for the exact staged Release host/Wasm. The milestone suite forces both backends and
checks rendered alpha/additive depth fades, far/clear depth, stretched coverage,
perspective/orthographic projection, 1x/4x, ordered masked views, resize/recovery,
scene save/load and public fountain controls. Evidence defaults to
`build/evidence/fx-milestone`. Desktop Chrome does not establish physical mobile
acceptance or sustained performance. See [validation record](fx-validation.md).

Ribbon mode is delivered separately: see [ribbon trails](trails.md) for path sampling,
space semantics, rendering, authoring and archive v19 contracts.

[Plane alignment](plane-particles.md) adds camera-independent ground/supplied-normal
quads with surface offset, existing rotation and archive v20 birth-normal persistence.

Static [mesh particles](mesh-particles.md) are now delivered with shared materials,
3D spin and archive v21 state. Soft fading remains a quad/ribbon capability.

Optional particle collision and independent projected decals are now delivered; see [contact/decal contracts](fx-contacts-decals.md) for responses, queued output, authoring, budgets and persistence. Rigid-body particles and PBR decal channels remain outside this increment.

Event-driven child effects and finite fading decals are now implemented; see [composition contracts](fx-composition.md). Archive v29 adds graph dependencies and live composition/decal continuation while retaining the earlier fields above.
