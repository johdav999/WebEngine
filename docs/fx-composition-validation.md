# FX composition validation

Validated 2026-10-19 in the shared WebEngine development checkout. Unrelated work
was preserved. Native and browser builds use separate isolated directories;
historical archive fixtures are retained unchanged.

## Build commands

Native CMake/Ninja Release build: `build/fx-composition-native`, using the compiler
and Ninja installed under WinGet and local Jolt 5.3.0 source through
`FETCHCONTENT_SOURCE_DIR_JOLT`. Put the configured compiler's directory on PATH
before running the binaries. Focused checks:

```powershell
cmake --build build/fx-composition-native --target fx_composition_tests fx_contact_tests fx_tests editor_fx_tests scene_archive_tests persistence_tests component_tests application_tests surface_trail_tests mesh_particle_tests plane_particle_tests trail_tests render_frame_tests -j 4
ctest --test-dir build/fx-composition-native -R '^(fx_composition|fx_contact|fx|editor_fx|scene_archive|persistence|component|application|surface_trail|mesh_particle|plane_particle|trail|render_frame)$' --output-on-failure
```

All thirteen suites passed. The new composition suite has 42 checks covering
Birth/Impact/Death, next-update child initialization, event/world placement,
world anchoring after source movement, inherited velocity, bounded events/spawns/
active counts/nesting, invalid graph/cycle rejection, pause/resume/stop/restart,
completion, managed decal fade/expiry and exact archive/editor continuation.

Browser Release uses repository-local Emscripten 6.0.11, matching
`.emscripten-version`, with `BINARYEN_CORES=2`:

```powershell
./.tools/emsdk/upstream/emscripten/emcmake.exe cmake -S . -B build/fx-composition-web -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/fx-composition-web -j 4
```

Stage `build/fx-composition-web/site/*`, current `web/*`, and demo model/font assets
into `build/fx-composition-site`. Serve that directory with `scripts/serve.py` on
port 8237. Served JS/Wasm must match the completed isolated build; do not stage
while the linker is still writing artifacts. Main and decode worker must use the
same archive codec (v29).

## Browser acceptance

Used desktop Chrome 155.0.8059.80 with forced WebGPU and WebGL2, Playwright/pngjs
from `build/detail-browser-tools/node_modules`:

```powershell
$env:NODE_PATH='build/detail-browser-tools/node_modules'
$env:BROWSER_EXECUTABLE='C:/Program Files/Google/Chrome/Application/chrome.exe'
$env:DEMO_URL='http://localhost:8237'
$env:EVIDENCE_DIR='build/evidence/fx-composition'
node scripts/fx-composition-smoke.cjs
# Repeat with DEVICE_SCALE_FACTOR=2 and a separate EVIDENCE_DIR.
```

The script checks real rendered frames plus source bytes/history:

- Impact chain preset and typed trigger/limit changes, exact undo/redo.
- Child preset replacement, nested .wefx import and malformed-child preservation.
- Sparks/dust/marks after impacts, whole-chain paused pixel equality, stepped
  completion, projected mark fade and expiry.
- Exact source restoration after preview and normal Play/Stop; scene save/reopen
  and embedded graph dependencies in exported .wefx.

Evidence locations are `build/evidence/fx-composition`,
`build/evidence/fx-composition-dpr2` and `build/evidence/fx-composition-regression`.
Each browser run retains JSON results, screenshots and downloaded archives.
The new script requires zero console/page errors. Physical mobile and sustained
performance remain separate acceptance; these tests do not establish a GPU FX
simulation system or arbitrary skinned/PBR decal support. See
[delivered contracts and limits](fx-composition.md).

Both DPR 1 and DPR 2 composition runs passed on WebGPU and WebGL2 with zero
console/page errors. Collision/decal, existing editor FX and surface-trail browser
regressions also passed on both backends. An old v23 current-writer assertion in
the editor FX script was migrated to v29 before its passing rerun; historical
fixtures were not rewritten. Matching served/isolated main and worker hashes are
recorded in `build/evidence/fx-composition/artifact-hashes.json`. Final native
acceptance passed all thirteen suites in 3.66 seconds, including shared-DAG depth
and live decal settings rejection. JavaScript syntax checks and `git diff --check`
passed.
