# FX authoring validation

Verified 2026-10-17 in the existing shared Windows checkout. Release browser build
uses repository-local Emscripten 6.0.11; browser is Chrome 155.0.8059.80. Unrelated
changes were retained. Architecture and both engine-skill copies were updated.

## Native

Eight focused CTest suites passed: editor_fx, editor, editor_content,
editor_structure, editor_terrain, fx, scene_archive and persistence. Record:
`build/evidence/editor-fx/native-tests.txt`.

The dedicated FX tests verify immutable preset preservation, validated replacement,
root/emitter saved identities, exact scene-byte undo, redo, conflicting external
writer rejection, nested custom-child preservation, effect-resource round trips with shared texture identity,
malformed/empty resource rejection, isolated preview/pause/step/isolation/stop,
stable borrowed camera address across replacement, bounded-pool dropped diagnostics,
source-byte preservation, delayed one-shot and looping emission, delayed burst age,
and save/load continuation while delay is pending. Existing real v16 FX fixture
loading retains legacy defaults. Current archives write v18.

## Browser

`scripts/editor-fx-smoke.cjs` passed on forced WebGPU and WebGL 2 at DPR1 and DPR2,
with no console or GPU validation errors. Evidence:

- `build/evidence/editor-fx/results.json` and screenshots/downloads.
- `build/evidence/editor-fx-dpr2/results.json` and screenshots/downloads.

Real controls and pointer gestures cover preset creation, immutable edits and
undo/redo; invalid field preservation; add/duplicate emitters; curve key tables and
SVG drag/one-command/cancellation; delayed emitter timeline drag; imported PNG atlas
selection and grid controls; exact standalone .wefx byte round trip (including
resource references); corrupt file preservation; independent preview pause/restart/
step/isolation and live edits preserving pause; closing after selection loss; exact
source-scene SHA256 equality before/after preview and normal Play; scene save/reopen;
resize and device/context recovery; responsive layout. Source bytes are hashed in
the browser to avoid transferring a large numeric array through the test protocol.

Existing `scripts/editor-structure-smoke.cjs` passed both backends using an isolated
native-produced v18 fixture. It checks component/hierarchy/group transform history,
physics rejection, camera interaction, and exact authoring bytes/pixels after Play.
Evidence: `build/evidence/editor-fx-structure-regression/results.json`.
`EDITOR_FIXTURE` now optionally selects the input fixture without overwriting older
evidence. Native fixture: `build/evidence/editor-fx-fixture/fixture.wescene`.

Existing `scripts/fx-milestone-smoke.cjs` passed both backends for soft depth,
alpha/additive behavior, stretch, projections, 1x/4x MSAA, ordered masked views,
resize/recovery, current archives and public fountain controls. Evidence:
`build/evidence/editor-fx-runtime-regression/results.json`.

## Acceptance boundary

This is desktop Chrome evidence, including high-DPI input. It does not establish
physical Android/iOS use, other browser acceptance or sustained mobile performance.
Preview has independent particles/camera/clocks and no game callbacks/audio. General
Play/Stop still has its documented built-in reconstruction limits. There is no
apply-back, standalone JSON loader, external texture-path resolver, GPU particle
simulation or effect graph. A consuming game's engine gitlink was not advanced.
