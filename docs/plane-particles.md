# Ground and supplied-normal particle alignment

`ParticleAlignment::Plane` orients existing pooled particles on a fixed plane
independently of the camera. The default normal `{0,1,0}` creates horizontal ground
quads. Supply a different `planeNormal` for walls, slopes and contact flashes.
This is an orientation mode for the existing Billboard render mode; Ribbon ignores
particle alignment. Emission, motion, lifetime, rotation, curves and flipbooks retain
their existing behavior. Applications own contact queries and effect placement.

```cpp
auto d = std::make_shared<ParticleEmitterDefinition>();
d->alignment = ParticleAlignment::Plane;
d->planeNormal = {0,1,0};
d->surfaceOffset = .02f;
d->space = ParticleSpace::World;
d->velocityMin = d->velocityMax = {};
d->size = {{0,.2f}, {1,3}};
auto& emitter = scene.addChild<ParticleEmitterComponent>("Impact ring", d);
// Move/rotate the emitter to the contact, then advance the scene once.
```

## Normal, space and rotation contracts

`planeNormal` is emitter-local at birth, need not be unit length and defaults up.
It must be finite, with finite squared length and length at least 1e-6. The runtime
normalizes it. `surfaceOffset` defaults zero and must be finite; signed values move
the rendered quad along its unit world normal by that many world units. Offset is
presentation-only: simulation positions, velocity, emission and collision do not
change. Sorting, fog and soft fade use the offset rendered position.

Local particles store the local normal and transform it with the emitter's current
inverse-transpose world matrix, including its parents. World particles capture the
transformed normal at birth and keep it when the emitter later moves or rotates.
Nonuniform scale therefore preserves plane orientation correctly; size retains the
existing largest-axis scalar scale policy, rather than a sheared quad. A singular
local transform suppresses plane drawing. A singular world birth drops that emission
and increments the existing dropped count. Camera/Velocity particles retain their
previous behavior under those transforms.

The stable basis uses the world normal and a reference axis chosen away from
parallel. It remains finite for horizontal, vertical and downward normals. Authored
rotation and angular velocity rotate within this basis, around the normal. Basis
phase is deterministic but changes at its reference-axis threshold; continuously
changing local normals may show that phase change. World orientation is fixed per
birth. Velocity stretch applies only to Velocity alignment. Plane mode does not
project positions onto a plane or restrict gravity/velocity: configure those fields
for a stationary ring or moving surface flash as appropriate.

For a world-space contact normal on an unrotated identity emitter, assign that
normal directly. With an existing transformed emitter, convert the normal to its
local normal space (transpose of its linear world matrix), or orient/place a
separate contact effect. Publish definitions as immutable resources; never mutate
a shared live definition to retarget an existing effect.

## Rendering and persistence

`ParticleDraw` carries a normalized world normal. Portable `FxFrame` constructs the
plane basis once for each draw, independent of camera; it retains normal camera
and velocity billboard paths. The instance payload is now 112 bytes, adding two
vec4 plane axes to the prior 80-byte payload. WebGPU expands the quad with these
axes in WGSL; WebGL2 expands equivalent CPU vertices. No new passes, textures or
backend types are added to portable APIs. Existing particle counters include planes.

Both backends retain center-sorted transparency with meshes/trails, alpha/additive
blending, no depth writes, depth test, masks, full color/size curves, flipbooks,
fog and optional opaque-depth soft fading. Positive offset can avoid coplanar depth
artifacts; softDistance must be chosen with the offset, because a very small gap
also fades a surface particle. Ordinary transparent sorting/soft-depth limitations
still apply. These quads do not conform to curved surfaces, receive PBR lighting,
project decals or perform terrain/physics queries. Mesh particles are documented in [mesh particles](mesh-particles.md).

Archive v20 appends plane normal/offset definitions and normalized per-particle
birth normals. Versions 1-19 retain their prior alignment and default the new fields
to up/zero. Scenes, assets, `.wefx`, cloning, Play and application envelopes share
the codec; matching host and worker versions must be deployed together. Invalid
normal/offset data is rejected before publication, preserving the last valid state.

## Editor and validation

Scene editor -> Details -> Particle FX includes **Ground ring**, a procedural
expanding-ring example. Appearance -> Alignment offers Plane / supplied normal;
Plane alignment controls contain the normal components and signed offset. Existing
rotation/spin, texture/atlas, size/color curves, blend, space and soft fading controls
apply. Changes use immutable replacement and bounded undo/redo. Open preview uses
the existing isolated scene and floor, without ticking or editing the source scene.
Normal editing is authored configuration, not a terrain picking gesture.

Build Release with `scripts/build.ps1 -Configuration Release`, using the repository
`.tools/emsdk` version in `.emscripten-version`. Native targets `plane_particle_tests`,
`fx_tests`, `trail_tests`, `editor_fx_tests`, `scene_archive_tests`,
`persistence_tests`, `render_frame_tests` cover the affected contracts. Put the
configured native compiler bin on PATH when required. Run corresponding CTest names
`plane_particle`, `fx`, `trail`, `editor_fx`, `scene_archive`, `persistence`,
`render_frame`. Browser suites: `scripts/plane-particle-smoke.cjs` and
`scripts/plane-editor-smoke.cjs`; the editor suite accepts DEVICE_SCALE_FACTOR=2.
Use Playwright/pngjs via NODE_PATH, BROWSER_EXECUTABLE, DEMO_URL and EVIDENCE_DIR.
Run existing FX/editor/trail regression scripts against the exact selected build.
See [validation](plane-particles-validation.md).

Static [mesh particles](mesh-particles.md) are now delivered with shared materials,
3D spin and archive v21 state. Soft fading remains a quad/ribbon capability.

Optional particle collision and independent projected decals are now delivered; see [contact/decal contracts](fx-contacts-decals.md) for responses, queued output, authoring, budgets and persistence. Rigid-body particles and PBR decal channels remain outside this increment.


Native Dawn and direct D3D12 support this presentation contract; see
[native FX scope and validation](windows-fx-rendering.md).
