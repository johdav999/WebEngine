# Mesh particles

Set `ParticleEmitterDefinition::renderMode = ParticleRenderMode::Mesh` and supply
`particleMesh` to emit pooled static geometry for debris, leaves or fragments.
This uses the same seeded emission, motion, lifetime, local/world space, pause,
stop/clear and masks as existing particles. No actor/component is created per
particle. `emissionMesh` remains the separate geometry used for birth sampling.

```cpp
auto d = std::make_shared<ParticleEmitterDefinition>();
d->renderMode = ParticleRenderMode::Mesh;
d->particleMesh = Mesh::cube(); // Or a shared imported static mesh.
d->blend = BlendMode::Opaque; // Preserve the mesh's material alpha modes.
d->space = ParticleSpace::World;
d->meshRotationMax = {2*pi, 2*pi, 2*pi};
d->meshAngularVelocity = {2, 3, 1};
d->velocityMin = {-1, 2, -1};
d->velocityMax = {1, 3, 1};
d->gravity = {0, -3, 0};
d->size = {{0, .15f}, {1, .06f}};
d->color = {{0, {1, .6f, .2f, 1}}, {1, {.6f, .2f, .1f, 1}}};
auto& emitter = scene.addChild<ParticleEmitterComponent>("Debris", d);
```

## Geometry and simulation

`particleMesh` is immutable shared triangle geometry with ordinary mesh materials,
UVs and material parts. Active Mesh mode requires nonempty valid vertices/indices,
at most one million vertices/three million indices, valid triangle part ranges,
and `maxParticles * max(1, partCount) <= 1000000`. Positions/normals/UVs must be finite.
Skeletal skinning, morph targets and Water materials are rejected. Publish fully
configured resources before sharing them. Failed configuration preserves the
current definition and live pool; normal archive byte/allocation limits still apply.
High triangle counts multiplied by large pools remain an application budget choice.

`meshRotationMin` and `meshRotationMax` are ordered finite Euler XYZ ranges in radians,
seeded per particle at birth. `meshAngularVelocity` is finite XYZ radians/second.
Both default zero. The existing scalar rotation/angularVelocity also apply, added
to the mesh's local Z Euler angle. Rotation composition is the engine's Rz*Ry*Rx;
these are artistic Euler rates, not a rigid-body angular-velocity solver. New random
samples are consumed only in Mesh mode, preserving billboard/trail RNG sequences.
Particle records now include mesh rotation/birth-matrix storage, increasing CPU pool
footprint for all modes; archive allocation budgets use the current record size.

Size keys multiply the source mesh uniformly. Local particles compose the current
emitter/parent world matrix with particle translation, Euler rotation and size.
World particles capture the emitter's complete linear transform at birth, including
scale/mirroring/shear, while position and velocity follow existing world simulation.
Later source movement/rotation/scale does not move old world particles. World mode
therefore uses the captured matrix rather than multiplying the billboard sizeScale
again. Nonuniform scale is preserved in the composed transform. Normal matrices
use inverse transpose. Zero size, singular matrices or non-finite resulting poses
suppress drawing while leaving simulation intact.

Culling/LOD and camera order are presentation-only. Pool records, 3D rotation and
motion advance once in the scene variable tick, including while offscreen. The
existing capacity/overload rules apply. `particles()` / EffectComponent particle
counts include mesh pool records; `SceneFrame::particles` contains quads only.

## Materials, blending and rendering

Mesh mode retains material slots and all normal material maps/scalars; particle
RGB multiplies material base color, and alpha multiplies material opacity. By default
`meshTextureOverride=false` preserves mesh textures. Set it true to use the existing
FX `texture` as the base-color map on every slot, retaining the other maps. UVs remain
mesh-authored. Flipbooks, billboard alignment/stretch and plane offsets do not drive
mesh geometry. Nonzero `softDistance` is explicitly rejected in Mesh mode in this
increment; ordinary mesh depth testing applies.

`blend=Alpha` or Additive forces that blend for each part. Mesh-only Opaque mode
preserves each material's Opaque/Mask/Blend/Additive mode. When the age color alpha
falls below one, non-additive parts become Alpha, allowing ordinary fade curves;
masked parts then use blended texture alpha rather than a cutout threshold. Explicit
material opacity with an Opaque material keeps ordinary mesh-renderer semantics.
Use Alpha when a continuously translucent effect is wanted.

Portable `mesh_particle.cpp` collects `MeshDraw` records and layer indices into the
existing SceneFrame, with shared resources, normal matrices, transformed bounds,
mirroring and visibility. The renderer's usual static mesh detail policy applies.
RenderFrame batches compatible opaque parts, explicitly permitting opaque instance
reordering; transparent parts join FxFrame's existing center-sorted queue with
ordinary meshes, quads and trails. WebGPU supports the existing opaque instancing;
WebGL2 retains its existing per-instance submission/basic material lighting limits.
No new shader, GPU geometry per particle or render pass is required. Existing mesh
and object upload counters cover mesh FX; `meshParticles` reports collected live
mesh particles per view (including size-zero or offscreen records).

Mesh particles receive normal World material lighting and fog. They do not cast
shadows in this increment; existing material shadow reception is retained where the
backend supports it. They add no collision, rigid bodies, picking targets, navigation,
terrain projection, decals or automatic contact events. They can pass through ground.
Applications own emission and contact policy. Approximate transparency sorting and
ordinary material/backend limits remain unchanged.

## Editor, assets and persistence

Scene editor -> Details -> Particle FX has a **Mesh debris** preset. Rendering mode
includes Mesh particles; Mesh particles controls select imported/static particle
geometry, XYZ rotation ranges, spin and optional FX base-texture override. Import
GLB/FBX via Assets first. Geometry is distinct from Shape -> Emission mesh. Select
geometry and set soft fade to zero before changing an existing emitter to Mesh mode.
The Mesh materials / opaque blend choice is valid only for Mesh mode; choose Alpha
or Additive before returning to quads/ribbons. Missing/unsupported geometry and
invalid ranges preserve the last valid effect. Preview uses the existing isolated
scene, clocks, pause/step and source-byte preservation. Edit/undo/redo use immutable
EffectAsset replacements and saved identities with refreshed runtime handles.

Archive v21 appends particle mesh references, 3D rotation settings, texture override
and each live particle's rotation/birth basis. Versions 1-20 keep their existing
modes and default the additions to no mesh, zero angles/spin, override false and
identity birth basis. Scene/resource discovery includes particle geometry/materials
and texture dependencies. `.wescene`, `.weassets`, `.wefx`, cloning, Play and
application envelopes share the codec. Host and worker versions must match. Playback
basis matrices are finite affine linear transforms; malformed records are rejected.

Build Release with repository-local `.tools/emsdk`, `.emscripten-version` and
`scripts/build.ps1 -Configuration Release`. Native targets: mesh_particle_tests,
plane_particle_tests, trail_tests, fx_tests, editor_fx_tests, scene_archive_tests,
persistence_tests, render_frame_tests. Run corresponding CTest names mesh_particle,
plane_particle, trail, fx, editor_fx, scene_archive, persistence, render_frame; Windows
may need the configured compiler bin on PATH. Browser suites:
`scripts/mesh-particle-smoke.cjs` and `scripts/mesh-editor-smoke.cjs` (DPR1/2 using
DEVICE_SCALE_FACTOR). Use Playwright/pngjs, NODE_PATH, BROWSER_EXECUTABLE, DEMO_URL
and EVIDENCE_DIR against the exact staged host/Wasm. See [validation](mesh-particles-validation.md).

Optional particle collision and independent projected decals are now delivered; see [contact/decal contracts](fx-contacts-decals.md) for responses, queued output, authoring, budgets and persistence. Rigid-body particles and PBR decal channels remain outside this increment.


Native Dawn and direct D3D12 support this presentation contract; see
[native FX scope and validation](windows-fx-rendering.md).
