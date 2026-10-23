# Shared authoring frontend validation — 2026-10-22

The existing dirty checkout and concurrent Windows renderer/input work were
preserved. This change adds `engine_editor_gui` and native/browser adapters;
it does not replace either renderer or the portable editor commands.

## Native Windows

Built with Visual Studio 18 2026, x64 Release, the pinned SDL/Dawn dependencies
and the Windows SDK. The SDK/device tests used approved host access.

- `editor_authoring_tests` drives the actual retained widgets through terrain
  strokes/history; AI states/transitions/assignment; animation metadata, controller
  transitions and assignment; prefab definition edits, repeated publication,
  nesting/history; FX validation/placement; cinematic keys/history; malformed
  files, disabled queues and callback teardown. It also creates fixture documents.
- `desktop_editor_tests` runs the frontend in a real hidden SDL/Dawn window,
  captures all six pages, exports the five asset document types and checks the
  Play gate. Existing native text/mouse edits, scene archives, resize, source
  replacement, dirty/corrupt-file preservation and isolated Play still pass.
- Focused regressions cover `editor`, `editor_terrain`, `editor_fx`, `prefab_asset`
  and `editor_authoring`. The old FX test incorrectly pinned the shared archive
  envelope to v31; it now checks against the current asset envelope and retains
  its timing/resource/round-trip assertions. No archive version changed here.
- Real readbacks are in `build/evidence/editor-authoring-windows/`; the native
  workbench test also writes CTest's separate `build/windows-services/editor-test-evidence/`.
  The rendered pages were visually inspected. GPU: NVIDIA RTX 5060 Ti, Dawn D3D12.
  This run does not establish direct-D3D12 renderer acceptance for these pages.

## Browser

Built `webengine` and `editor_authoring_wasm_tests` with repository-local Emscripten
6.0.11 and a repository-local temporary directory. The portable widget contracts
also pass under Node/Wasm.

`scripts/editor-authoring-smoke.cjs` serves the current host and Wasm locally and
runs Chrome with forced WebGPU and WebGL2 separately. Both pass all six pages,
actual canvas pointer routing, the DOM text-service bridge, native-produced
document uploads, five downloads per backend, independent history, and changed-area
and stale-generation upload rejection. It reports no page errors. Screenshots and
`report.json` are in `build/evidence/editor-authoring-browser/`.

`editor_authoring_tests <fixture-dir> <browser-evidence-dir>` then loads those
browser-generated `.weanim`, `.weai`, `.weprefab`, `.wefx` and `.wecin` documents
using native archive readers and verifies edited clip labels, graph states,
emitter rate and cinematic key counts. This establishes file interchange in both
directions, in addition to using a shared implementation.

The existing `scripts/editor-smoke.cjs` also passed on both forced renderers
with the tools closed: selection, typed transforms, invalid values, all gizmos,
undo/redo, viewport resize and camera orbit. Evidence is in
`build/evidence/editor-authoring-scene-regression/results.json`. Final native
CTest passed 6/6 in about 43 seconds.

## Reproduce

```powershell
cmake --build build/windows-services --config Release --target editor_authoring_tests desktop_editor_tests editor_tests editor_terrain_tests editor_fx_tests prefab_asset_tests --parallel 6
./build/windows-services/Release/editor_authoring_tests.exe build/evidence/editor-authoring-fixtures
ctest --test-dir build/windows-services -C Release -R '^(editor_authoring|desktop_editor|editor|editor_terrain|editor_fx|prefab_asset)$' --output-on-failure
# Use .emscripten-version and repository-local SDK; set TEMP/TMP/EMCC_TEMP_DIR
# to a writable repository directory when Windows temp ACLs block emcc.
cmake --build build/web-assets-release --target webengine editor_authoring_wasm_tests --parallel 6
node build/web-assets-release/editor_authoring_wasm_tests.js
$env:NODE_PATH='C:/Users/Johan/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules'
$env:BROWSER_EXECUTABLE='C:/Program Files/Google/Chrome/Application/chrome.exe'
node scripts/editor-authoring-smoke.cjs
./build/windows-services/Release/editor_authoring_tests.exe build/evidence/editor-authoring-fixtures build/evidence/editor-authoring-browser
```

Browser smoke requires Playwright; set NODE_PATH to its installed modules. The
test creates and closes its own server and browser. No physical controller,
mobile device, long-session performance, clean-machine installer or new native
advanced graph/curve/cinematic playback acceptance is implied. See
[delivered scope](editor-authoring.md#scope).
