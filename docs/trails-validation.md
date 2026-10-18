# Ribbon trail validation - 2026-10-17

Delivered scope: single-source camera-facing ribbon trails, editor authoring/preview
and archive v19. This is the first increment of FX milestone 3; ground alignment,
mesh particles, collision and decals remain future work.

## Build and native checks

Built Release using the repository-local Emscripten 6.0.11 SDK, via
`scripts/build.ps1 -Configuration Release` and focused `cmake --build
build/web-release -j 4` rebuilds (`BINARYEN_CORES=2`). Full dist contents were copied
to `build/trail-site` and served with `scripts/serve.py --port 8197 --directory
C:/Users/Johan/source/repos/WebEngine/build/trail-site`. Final host and Wasm hashes
match dist; `build/evidence/trails/build-hashes.json` records SHA-256 values.

Native targets: trail_tests, fx_tests, editor_fx_tests, persistence_tests,
render_frame_tests, scene_archive_tests. Final CTest run passed all six suites in
4.33 seconds. `trail_tests` reports 280 checks (including finite vertex assertions).
Evidence: `build/evidence/trails/native-tests.txt`.

Trail checks cover bounded distance sampling/ring eviction, per-update overload
breaks, teleports/manual/resume breaks, pause, stop/drain including a sub-sample
endpoint, delayed one-shot emission, local-parent/world movement, masks/disable,
finite per-camera geometry including parallel tangents, alpha/additive batch order,
exact live archive continuation, reusable resource round trip, exact authoring undo,
isolated local-source preview and invalid-capacity rejection. Existing archive
fixtures, scene codec and application persistence suites passed unchanged fixtures.

An initial native test used the dense overflow definition for a later geometry
assertion; correcting the fixture to the normal definition resolved that failure.
Sandboxed native invocations stalled; final checks ran with the configured compiler
runtime directory on PATH outside that restriction. A leftover owned test process
initially locked linking; it was stopped, then all targets rebuilt and passed.

## Browser evidence

Desktop Chrome 155.0.8059.80; forced WebGPU and WebGL2. The screenshots were inspected
for visible joined strips and color/width gradients.

- `scripts/trail-smoke.cjs`: editor preset and trail controls, immutable undo/redo,
  world alpha/local additive paths, pause/step, soft fading enabled, exact source
  byte preservation, resize, device recovery and v19 `.wefx` round trip. Runs at
  DPR 1 and 2; results/screenshots in `build/evidence/trails` and
  `build/evidence/trails-dpr2`. The paused GPU path reports zero trail vertex uploads.
- `scripts/trail-depth-smoke.cjs`: deterministic actual-pixel fades for orthographic
  and perspective cameras, alpha/additive, far/clear depth, 1x/4x MSAA, two ordered
  masked viewports, resize/recovery and saved live-trail scene reopening. Results
  in `build/evidence/trail-depth`. Both backends produced center RGB [149,37,0]
  without fading and [15,171,0] near the opaque receiver. The clear second view
  produced [150,1,2], confirming it did not reuse the first view's receiver depth.
- `scripts/editor-fx-smoke.cjs`: existing full authoring gestures/cancellation,
  resource import/export, malformed input preservation, source restoration, Play,
  scene reopening, resize and recovery. Final results in
  `build/evidence/trails-editor-regression`. Its historical `v18` check label still
  names the previous feature milestone; the executable assertion checks byte 19.
- `scripts/fx-milestone-smoke.cjs`: existing billboard depth/stretched-coverage,
  projections, alpha/additive, MSAA, masked views, recovery, archive and fountain
  controls passed after shared ribbon shader integration, before the ribbon-only
  stop endpoint correction. Evidence in `build/evidence/trails-fx-regression`.

All browser runs above reported zero console/page errors. New scripts pass Node
syntax checks. `git diff --check` found no whitespace errors. Architecture, feature
contracts, installed engine-skill and CatGame's skill copy were updated; five affected
files in each skill copy were hash-verified against staged updates.

## Acceptance limits

This establishes desktop Chromium behavior. Physical Android/iOS, other browsers
and sustained mobile performance are unverified. Simple tangent joins and
segment-center transparency ordering retain the limitations in [contracts](trails.md).
A preview orbit is illustrative motion; applications still own actual gameplay
source movement and explicit stroke breaks. No game-specific source policy or
additional milestone-3 particle modes are implied.
