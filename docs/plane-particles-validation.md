# Plane particle validation - 2026-10-17

Delivered: ground/supplied-normal alignment for existing pooled particles, offset,
rotation, editor controls/preset, isolated preview and archive v20. Automatic terrain
projection, collision queries, decals and mesh particles remain future work.

## Build and native checks

Release built with `scripts/build.ps1 -Configuration Release` using repository-local
Emscripten 6.0.11, then `cmake --build build/web-release -j 4` for acceptance fixtures
(`BINARYEN_CORES=2`). Full dist staged to `build/plane-site`, served at port 8207
with `scripts/serve.py`. Host/Wasm, asset worker and editor adapter SHA-256 hashes
match dist; recorded in `build/evidence/plane-particles/build-hashes.json`.

Native build targets: plane_particle_tests, fx_tests, trail_tests, editor_fx_tests,
scene_archive_tests, persistence_tests, render_frame_tests. All seven corresponding
CTest suites passed (4.36 seconds), including the retained real legacy archive
fixture. The new test reports 28 plane checks. Evidence:
`build/evidence/plane-particles/native-tests.txt`.

Checks include normalized orientation and offset without simulation displacement,
camera-independent axes/winding, no velocity stretch, live archive byte round trip
and playback continuation, in-plane spin, World orientation retention, Local
inverse-transpose normals under nonuniform scale/rotation, singular-transform
handling, wall/arbitrary/downward normals, invalid normal/offset/alignment rejection,
exact live-state authoring undo, .wefx settings and isolated source preservation.

## Browser checks

Desktop Chrome 155.0.8059.80, forced WebGPU and WebGL2:

- `scripts/plane-particle-smoke.cjs`: ground vs camera-facing geometry, supplied
  slope normal, ground rendering in perspective, signed offset behind a receiver,
  two flipbook cells and scene fog; depth fading in orthographic/perspective,
  alpha/additive, far/clear depth, 1x/4x MSAA, ordered masked views, resize/recovery
  and live scene archive reopen. Evidence `build/evidence/plane-particles`.
- `scripts/plane-editor-smoke.cjs`: Ground ring preset, normal/offset controls,
  invalid zero-normal preservation, immutable undo/redo, world alpha/local additive,
  pause/step, zero repeated WebGPU uploads while paused, exact authoring-source
  bytes, resize/device recovery and v20 .wefx round trip. Both DPR1 and DPR2 passed;
  evidence `build/evidence/plane-editor` and `build/evidence/plane-editor-dpr2`.
- `scripts/fx-milestone-smoke.cjs`: existing billboard/stretch, depth/fog-related
  render path, projections, alpha/additive, MSAA, ordered views, archives, recovery
  and fountain controls; evidence `build/evidence/plane-fx-regression`.
- `scripts/trail-smoke.cjs`: existing ribbon authoring, world/local rendering,
  pause/step, source preservation, recovery and current-format .wefx round trip;
  evidence `build/evidence/plane-trail-regression`.
- `scripts/editor-fx-smoke.cjs`: existing full curves/timeline gestures/cancellation,
  imported assets, file round trips, invalid input preservation, isolated preview,
  exact source restoration, Play/Stop, saved scene reopen and device recovery;
  evidence `build/evidence/plane-authoring-regression`.

All browser runs reported zero console/page errors. Controlled plane depth pixels
matched across backends: hard [149,37,0], near-soft [15,171,0], clear second viewport
[150,1,2]. Ground and slope screenshots were visually inspected: horizontal rings
are foreshortened, and tilted quads correctly intersect/clip against the flat floor.
This does not imply automatic surface conforming. An initial editor test used exact
equality for float .08; it was corrected to float tolerance and both DPR suites pass.
Some older regression result labels retain v18/v19 feature names, while their
current-format assertions check byte 20.

New JavaScript scripts and web/editor-fx.js pass Node syntax checks. Git diff
whitespace checks pass. Feature/architecture docs and both installed/CatGame
engine-skill copies are updated, preserving each copy's guidance; all six affected
files in each copy were hash verified against staged changes.

## Limits

Desktop Chromium and DPR2 do not establish physical Android/iOS, other browser or
sustained performance acceptance. Plane particles remain unlit quads with existing
center-based transparency sorting and opaque-only soft-depth limitations. The
stable basis can change phase at its reference-axis threshold for changing normals.
Applications own contacts, source placement and simulation clocks. See
[contracts](plane-particles.md).
