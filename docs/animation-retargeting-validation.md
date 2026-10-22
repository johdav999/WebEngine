# Animation retargeting validation — 2026-10-20

Implemented and verified in the shared WebEngine development checkout. Unrelated
checkout changes were preserved. No CatGame dependency pin or deployed site changed.

## Builds and portable checks

Used the existing `build/native` configuration with its matching WinLibs MinGW
runtime on PATH. Rebuilt relevant targets and passed all 10 CTest suites:
`animation_retarget`, `animation_library`, `animation_layers`, `animation_control`,
`ik`, `animation`, `root_motion`, `scene_archive`, `persistence`, `editor_content`.
The focused retarget test was rerun after tightening its floating-point assertion.

Used repository-local Emscripten **6.0.11**, Release `build/web-release`,
`web_assets` and `animation_retarget_wasm_tests`. The retarget Wasm test passed in
Node **24.13.1**. A later test-only rebuild hit sandbox temp-file permissions;
setting `EMCC_TEMP_DIR` to `build/retarget-emscripten-temp` allowed the final build
and test to complete. This did not change the served application artifacts.

Core tests cover:

- Source rest mapping to destination rest with different bind orientations;
  unequal limb proportions and destination-local bone offsets.
- Model-axis quaternion alignment, median scale estimation, root translation,
  pelvis translation and unit conversion without double scaling.
- Explicit different-name maps, unmapped destination accessories and animated
  source helpers, incompatible ancestors, duplicate/missing names and invalid data.
- Nonuniform/animated scale rejection, profile roundtrip and malformed input,
  destination pose expansion budget rejection before allocation.
- Strict append rejection for mismatched bind poses; retarget append preserving
  input library, stable IDs and inverse binds; one library history edit.
- Baked markers/root motion through normal playback; library/scene archives,
  scene apply/undo/redo and construction of the side-by-side demo.

## Browser acceptance

Served the selected `dist` on `http://localhost:8137`. Ran browser suites serially
with Chrome **155.0.8059.85**, actual **WebGPU** and forced **WebGL2**.
The initial sandbox browser launch could not access localhost; permitted local
browser execution completed the checks. No final page/console errors occurred.

`scripts/animation-retarget-smoke.cjs` passed at **DPR1 and DPR2**:

- Blue source library export and gold destination selection, automatic exact-name
  suggestions, manual unmapping/restoration, duplicate-source rejection.
- Translation-scale edit/rejection/estimation and pelvis translation mode.
- `.weretarget` save/open, repairable missing-joint diagnostics and disabled Bake,
  corrupt profile/source preserving the previous staged source/profile.
- FBX and GLB decoder source loading. The column FBX shares only a Root name;
  removing that last mapping disables Bake. Full-body semantic matching is not inferred.
- Atomic bake appending clips without replacing prior IDs, library undo/redo,
  retained markers, isolated clip preview and paused scrubbing.
- Destination-only gold-pixel change between rest and wave; blue donor excluded
  from the comparison crop. DPR1: WebGPU **828**, WebGL2 **4379** changed pixels;
  DPR2: WebGPU **3304**, WebGL2 **17049** in the recorded run. Counts can vary with
  render timing; the test requires visible destination change, not a fixed count.
- Source-scene hash unchanged during bake/preview, baked `.weanim` reopen,
  scene apply/undo/redo/reopen, staged-source cleanup on Close/rebind and compact
  900×760 controls with Close reachable.

Inspected screenshots of the mapping workbench and both renderer previews.
Evidence: `build/evidence/animation-retarget/` and
`build/evidence/animation-retarget-dpr2/` (results, screenshots, source/baked
libraries and profiles).

Existing `editor-animation-smoke.cjs`, `animation-layers-smoke.cjs` and
`ik-smoke.cjs` also passed serially on both backends at DPR1 against this build.
Their evidence is in `build/evidence/retarget-regression-authoring/`,
`retarget-regression-layers/` and `retarget-regression-ik/`.
JavaScript syntax and `git diff --check` passed.

## Served artifact identity

SHA256 at acceptance:

| File | SHA256 |
| --- | --- |
| `dist/webengine.wasm` | `34273f77a9fee43a0ed7dd3bcc8897a9fc9cf405a59245abc4aa1c3eed877909` |
| `dist/asset_decode.wasm` | `d87b73bfaaeff73e17f71e9fc33167ff2666810f0af9185f19165b21a2b16d8e` |
| `dist/editor-animation.js` | `9b48b5b84adf295281624e6096e58cd530e849b24a1bf0e579eea04a8307c2f1` |
| `dist/app.js` | `313ff9329db04a37d93236e3911f360bc95fc3d9502e3a747b1b0570d2a43b56` |

Architecture, README and animation contracts were updated. Installed engine-skill
and CatGame's repository copy include the new API, workflow, profile/pose limits,
ownership, build and validation guidance; CatGame-specific dependency guidance
was retained.

## Scope

This verifies deterministic sampled retarget baking and desktop browser integration
using authored rigs/import fixtures. It does not establish arbitrary commercial
character-rig compatibility, human animation-quality approval, automatic foot
locking/contact preservation, physical Android/iOS acceptance or sustained mobile
performance. See [contracts and limitations](animation-retargeting.md).
