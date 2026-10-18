# Scene editor validation

Recorded 2026-10-15 against the working checkout. This is desktop Chrome evidence.

## Selected build

- Pinned Emscripten: `6.0.11`; Release build in `build/web-editor-release`.
- Browser site: `build/editor-browser-site`, served at `http://localhost:8163`.
- Wasm SHA256: `71f4599b1110d72c187c9799428ad279a3023877fa5befb18dcea02b90d5e810`.
- `build/evidence/editor/build-identity.json` records matching JS/Wasm/host and
  editor source hashes; `verified-main.cpp` retains the integration source snapshot.
  Separate staging avoids replacing another task's `dist/`.
- Browser: Chrome `155.0.8059.79`. Both forced `webgpu` and `webgl2` backends
  were confirmed from the actual initialized renderer.

## Results

- Native CTest: **5/5 passed** (`editor`, `scene_archive`, `persistence`, `selection`,
  `manipulation`). Output: `build/evidence/editor/native-tests.txt`.
- Browser editor acceptance: **14 checks per backend**, repeated at device pixel
  ratios **1 and 2**. Output: `build/evidence/editor/results.json` and
  `build/evidence/editor/dpr2/results.json`.
- Existing scene/archive browser acceptance: **8 checks passed**, including actual
  scene/asset downloads, fresh-page restore without source FBX, embedded WAV,
  skeletal animation, malformed data preservation, and checkpoint controls at a
  narrow viewport. Output: `build/evidence/editor/archive-regression/results.json`.
- Direct `?editor=1` launch auto-entered edit mode after default asset readiness,
  paused simulation, and published 66 hierarchy nodes. Output: `launch.json` and
  `launch.png` under `build/evidence/editor/`.
- Installed `engine-skill`: `quick_validate.py` passed. Editor JS syntax and focused
  Git whitespace checks passed.

The editor browser checks use real controls and pointer drags, not mocked scene
state. They verify inspector/rendered-pixel changes, move and rotation handles,
exact local-transform undo/redo, one command per drag, invalid zero-scale rejection,
Escape/blur/pointer cancellation, viewport picking without gameplay providers,
physical download/reopen with **zero differing canvas pixels**, failed-open
preservation, hierarchy filtering and keyboard commands, drag coordinates after
resize, camera contact ownership, and edit retention across editor exit/re-entry.
Desktop and resized screenshots were captured for both backends and pixel ratios;
the desktop screenshot was visually inspected.

## Corrections established by validation

- Shared archive staging disables whole fieldsets. The editor now restores that
  fieldset gate after save/open, as well as each input's own read-only state.
- A rotated physics actor can decompose to a scale infinitesimally different from
  one. Editor teleport rejects actual world scaling, then supplies exact unit scale
  to the physics API while preserving authored local Euler values for undo/redo.
- The older archive fixture authored IBL intensity while default day/night lighting
  owned that value. Its `1.35` versus `1` assertion also failed with `editor.js`
  omitted (recorded under `archive-baseline-without-editor/`). The fixture now
  explicitly disables the lighting cycle before manually authoring IBL; the full
  archive suite passes. No runtime sky ownership policy was changed for this fix.

## Scope

This increment edits the displayed scene with simulation paused. Undo/redo covers
transforms. Viewport picking uses CPU mesh bounds and the first render view;
hierarchy selection also supports disabled nodes. Play/Stop isolation, property
reflection, node/component creation, snapping, multi-selection, material history,
and scale gizmos remain later increments. Physical mobile devices, non-Chromium
browsers and production hosting were not tested.
