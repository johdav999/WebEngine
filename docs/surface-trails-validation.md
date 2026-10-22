# Surface-following trails validation — 2026-10-19

Stage 6 implements supplied-contact strips in the shared checkout. It introduces
explicit SurfaceTrail alignment, world sampling/normals, bounded geometry, FX
authoring/isolated sloped preview and archive v28 live-normal continuation.
Separate concurrent FX composition work advances the same checkout to v29;
Stage 6 preserves that work and tests a real earlier v28 `.wefx` fixture. A minimal
build repair uses Node's public transform API in the concurrent effect publisher
instead of its inaccessible private decomposition helper.

## Portable and ABI checks

Native MinGW Debug: **66/66 CTest suites passed**, 14.35 seconds, including
the separate concurrent FX composition suite (`build/stage6-ctest.log`).
The focused `surface_trail` suite verifies supplied bank and changing hill normals,
camera-independent expanded vertices, tangent-plane offset, distance samples and
birth times across frame rates, repeating UV phase, invalid input retention,
airborne gaps, teleport/opposed-normal/reset/resume breaks, capacity/work-budget
eviction, expiry and world-history independence from emitter movement. It also
checks exact scene continuation after contact resubmission, actual v28 effect
compatibility, resource export, immutable editor history, sloped preview,
truncation and allocation-limit rejection. Existing trail/FX/archive/editor suites
run in the complete native set.

Explicit `surface_trail_wasm_tests` runs in Node on Emscripten's 32-bit ABI;
**260 focused Wasm checks passed** (`build/stage6-wasm.log`). The filesystem fixture check is
native-only; all portable logic/continuation assertions run in Wasm too.
Repository-local SDK **6.0.11**, Release `build/fx-contact-web`, selected
`web_assets` publication to `dist`. Native build is `build/fx-contact-native`.
Final browser runs use an immutable copy in `build/stage6-validation-site`, served
on localhost:8106, to isolate acceptance from concurrent source/site changes.
Its exact hashes are recorded; this is not a claim that later edits were tested.

## Browser checks

Chrome **155.0.8059.80**, forced WebGPU and WebGL2. The new editor suite runs at
1500x1050 CSS pixels at DPR 1 and 2, with resize/recovery at 1180x850:

- `build/evidence/surface-trail/results.json`
- `build/evidence/surface-trail-dpr2/results.json`

It verifies preset/configuration and undo/redo; rejected Local-space edits retain
the scene hash; parameter/offset changes, bounded moving contacts on a slope with
airborne intervals, rendered pixels versus a stationary source, pause stability,
point/segment/drop/upload diagnostics, resize/GPU recovery, effect export/import,
scene save/open, malformed-file retention and exact source hashes across isolated
preview and Play/Stop. Paused WebGPU vertices reuse uploads. Backend-specific
banked, stationary, paused, resized, recovered and editor screenshots are retained.

`scripts/trail-depth-smoke.cjs` with `SURFACE_TRAIL=1` supplies surface normals
through the actual portable API. Its two-backend report is
`build/evidence/surface-trail-depth/results.json`. Pixel checks cover opaque
occlusion, fog, alpha/additive soft fading, orthographic/perspective reconstruction,
clear/far depth, 1x/4x MSAA, masked ordered views, resize/recovery and live scene
archive reload. For example, the WebGPU centre changed from hard red/green
`[149,37,0]` to soft `[15,171,0]`; opaque occlusion removed red (`[0,186,0]`).
These are fixture pixel observations, not a colour/performance guarantee.

Affected browser regression reports and final build hashes are recorded in
`build/evidence/surface-trail/build-metadata.json`. Architecture/contracts and both
installed/CatGame engine-skill copies are updated; no game dependency pin changed.

## Limits

Straight endpoint/normal interpolation cannot reconstruct unobserved contact
motion or conform the full strip width to terrain. Marks are baked world traces:
moving/deforming/removed receivers and terrain edits/page eviction need application
clear/break policy. No automatic tyre slip policy, physical vehicle trail example,
receiver tracking, collision mutation, miter/round caps or arbitrary contact queue
is claimed. Bounds are per emitter/view; maximum aggregate history/render workload
and sustained performance were not stress-tested. Desktop checks and DPR emulation
do not establish physical Android/iOS acceptance.
