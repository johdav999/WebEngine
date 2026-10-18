# Mesh particle validation - 2026-10-17

Delivered: pooled static mesh particles, material/texture inheritance and override,
seeded three-axis rotation/spin, local/world transforms, editor authoring and archive
v21 continuation. This increment adds no rigid bodies, collision, picking, shadow
casting, mesh soft-depth fade, terrain projection or decals.

## Build and native checks

Release built with repository-local Emscripten 6.0.11 through
`scripts/build.ps1 -Configuration Release`, then focused `cmake --build
build/web-release -j 4` for acceptance fixtures (`BINARYEN_CORES=2`). Full dist staged
to `build/mesh-site`, served at port 8217 using `scripts/serve.py`. Final host/Wasm,
asset worker and editor adapter hashes match dist; SHA-256 record:
`build/evidence/mesh-particles/build-hashes.json`.

Native targets mesh_particle_tests, plane_particle_tests, trail_tests, fx_tests,
editor_fx_tests, scene_archive_tests, persistence_tests and render_frame_tests built.
All eight CTest suites passed in 4.84 seconds. New mesh test reports 31 checks.
Evidence: `build/evidence/mesh-particles/native-tests.txt`.

Checks cover mesh parts without billboard duplicates or per-particle nodes,
shared geometry/material ranges, shadow-caster exclusion, opaque instancing and
stable object upload suppression, retained World poses, byte-exact live archive
round trip/continuation, seeded rotation replay, Local transforms, singular-pose
suppression without pool changes, age-alpha promotion into sorted transparency,
texture override, frustum/mask/disable independence, draining stop, invalid geometry/
spin/soft-fade rejection, embedded resources, exact authoring undo, dependency
collection and isolated preview source preservation. Existing legacy fixtures are
retained and pass the affected archive/runtime suites.

## Browser evidence

Desktop Chrome 155.0.8059.80; forced WebGPU and WebGL2:

- `scripts/mesh-particle-smoke.cjs`: pixel parity with an ordinary MeshComponent,
  opaque occlusion, creation-order-independent alpha sorting, additive accumulation,
  fog, three-axis rotation/spin, multiple material slots, ordered masked views,
  inherited texture vs explicit FX override, flat leaf-like geometry, 1x/4x MSAA,
  resize/recovery and v21 live scene reopening. Evidence `build/evidence/mesh-particles`.
  The 128-particle opaque fixture reports maxInstancesPerDraw=128 on WebGPU and 1
  on WebGL2, consistent with the existing renderer contracts.
- `scripts/mesh-editor-smoke.cjs`: Mesh debris preset, imported GLB assignment,
  geometry/spin undo/redo, invalid missing-geometry preservation, explicit unsupported
  soft-fade rejection, World alpha/Local additive preview, pause/step, exact source
  bytes, resize/device recovery and embedded v21 .wefx round trip. DPR1 and DPR2
  passed: `build/evidence/mesh-editor`, `build/evidence/mesh-editor-dpr2`.
- Existing `scripts/fx-milestone-smoke.cjs`: billboard/stretch and depth-fade paths,
  projections, blending, MSAA, ordered views, recovery, archives and fountain controls;
  evidence `build/evidence/mesh-fx-regression`.
- Existing `scripts/plane-particle-smoke.cjs`: ground/supplied-normal geometry,
  offset, flipbooks, fog, soft fading, ordered masks, MSAA, recovery and archive;
  evidence `build/evidence/mesh-plane-regression`.
- Existing `scripts/editor-fx-smoke.cjs`: curves/timeline gestures and cancellation,
  assets, file round trips, invalid input, preview source restoration, Play/Stop,
  scene reopening and recovery; evidence `build/evidence/mesh-authoring-regression`.
- Existing `scripts/trail-smoke.cjs`: ribbon controls, undo/redo, World/Local rendering,
  pause/step, source preservation, resize/recovery and current archive round trip;
  evidence `build/evidence/mesh-trail-regression`.

All runs reported zero console/page errors. Screenshots were inspected for material
slot boundaries and visible imported fragments in the preview. A native test initially
omitted the camera aspect argument; it was corrected before the passing run. A browser
metric assertion initially sampled the previous fixture's periodic diagnostics; the
suite now waits for the 128-particle counter sample. Some old result labels name v18,
v19 or v20 features; their current-version byte assertions check 21.

New scripts and editor-fx.js pass Node syntax checks; `git diff --check` passes.
Architecture and feature documentation updated. Installed engine-skill and CatGame
skill copy retain their prior guidance and each received seven affected files,
hash-verified against staged copies.

## Limits

This proves desktop Chromium behavior, not physical Android/iOS, other browsers
or sustained performance. Ordinary mesh/backend material and transparency limits
apply. Transparent mesh parts are individually sorted/submitted; only compatible
opaque parts use WebGPU instancing. CPU particle records now include mesh rotation/
birth matrices in all modes. Fragments do not collide with the floor or cast shadows.
Applications still own contacts, placement, clock policy and scene reconstruction.
See [contracts](mesh-particles.md).
