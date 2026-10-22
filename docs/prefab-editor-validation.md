# Prefab asset editor validation — 2026-10-20

Verified in the shared WebEngine development checkout, preserving unrelated work.
No consumer dependency pin, deployment or remote asset publication changed.

## Portable and archive checks

Built with the existing native Debug configuration and matching WinLibs MinGW
runtime. The 23 relevant CTest suites passed: `ai_tasks`, `ai_controller`,
`state_machine`, `editor_content`, `editor_structure`, `editor`, `persistence`,
`assets`, `prefab_asset`, `prefab`, `application`, `scene_archive`, `physics`,
`animation`, `animation_retarget`, `ik`, `animation_layers`, `animation_control`,
`root_motion`, `performance_timeline`, `performance`, `glb_importer`, `fbx_importer`.

Prefab tests cover independent instance identities and shared resources, asset
checksum failures, embedded variant bases, scene save/load and Play cloning,
duplication identity remapping, local tint/root placement preservation, grouped
propagation undo/redo, override revert/unpack/undo and updates after scene reopening.
They also exercise variant rebasing, local/upstream additions, deletion conflicts
and rejection without partial scene/history publication. A final regression covers
duplication of a locally deleted node and preservation of that deletion on update.
After that final repair, `prefab_asset`, `performance_timeline`, `editor_content`,
`editor_structure`, `persistence` and `scene_archive` were rebuilt and rerun: all
six passed.

Repository-local Emscripten **6.0.11** built Release `webengine`, `web_assets`,
`prefab_asset_wasm_tests` and `performance_timeline_wasm_tests`. Both focused Wasm
tests passed in Node **24.13.1**. Interrupted earlier builds left invalid generated
objects and one incomplete Emscripten JavaScript cache entry; removing only those
identified generated files allowed normal builds to complete. The final build
uses `EMCC_TEMP_DIR` inside `build/prefab-emscripten-temp`.

## Browser acceptance

Served the selected `dist` at `http://localhost:8137`. Browser suites ran serially
with Chrome **155.0.8059.85**, actual **WebGPU** and forced **WebGL2**.
`scripts/prefab-editor-smoke.cjs` passed at **DPR1 and DPR2**:

- Private definition edits and history leave live scene bytes unchanged.
- Invalid property edits preserve the staged definition.
- Definition changes visibly update linked instances, retaining local tint and
  root placement; one scene undo/redo restores the complete propagation.
- Variants retain their local values while inheriting base changes.
- Asset file save/open, corrupt-file rejection, revert, unpack and scene reopening.
- Compact layout keeps final actions reachable.

Evidence is in `build/evidence/prefab-editor{,-dpr2}/results.json`, screenshots and
saved asset files. Final DPR1 scene comparisons changed 39,860 pixels on WebGPU and
95,862 pixels on WebGL2. These establish visible propagation in the demo, not a
render-quality benchmark. The existing `editor-animation-smoke.cjs` also passed
on both renderers after integration.

The older `performance-smoke.cjs` did **not** pass: its first fixed-count assertion
expects 11 draws while the current default atmosphere sphere produces 12. Its
later checks were not completed; this run is not aggregate-performance acceptance.
The dedicated timeline suite verifies the additive metrics export separately.

See [feature contracts](prefab-editor.md) for merge conflicts, camera update
restrictions and unsupported nested/custom definitions. Desktop Chromium/DPR
checks do not establish physical Android/iOS acceptance or sustained mobile
performance. The installed engine-skill and CatGame copy were updated together;
CatGame-specific guidance and its selected engine pin remain intact.

## Final staged artifact identity

SHA-256 for the browser build used by the final prefab/timeline acceptance runs:

| File in `dist` | SHA-256 |
| --- | --- |
| `webengine.wasm` | `69d8925d5890d5bd02cf7fabd41d401a9c1e061b764977f7fa49fba16b1bdb08` |
| `asset_decode.wasm` | `c29f77dac3ce3a21ecdea8eccb0a516ca84f7a6013465bdb3cf403418f554543` |
| `editor-prefab.js` | `d72876329d6fab031c0f80dc14a482d4583577fe07255487933a0a7d9143d1ca` |
| `performance-timeline.js` | `7787929f846d4af2a17b0356f7a5027a6e50596f75eea628d3e28e2a917ed2fa` |
