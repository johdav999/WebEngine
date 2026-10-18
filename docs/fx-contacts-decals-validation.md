# Particle contacts and projected decals validation

Verified 2026-10-17 against the shared development checkout using isolated build
directories. This worktree also contains unrelated engine changes; it was not
reset or cleaned. The current shared scene/effect codec writes v23 and retains
the contact/decal fields introduced in v22.

## Builds and native checks

Used repository-local Emscripten 6.0.11 (`.emscripten-version`) with
`BINARYEN_CORES=2`:

```powershell
./.tools/emsdk/upstream/emscripten/emcmake.exe cmake -S . -B build/fx-contact-web -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/fx-contact-web -j 4
```

Native CMake/Ninja build `build/fx-contact-native` uses the installed C++ compiler
and the repository's existing Jolt 5.3.0 source through
`FETCHCONTENT_SOURCE_DIR_JOLT`. Build the following targets, then run:

```powershell
cmake --build build/fx-contact-native --target fx_contact_tests fx_tests editor_fx_tests scene_archive_tests persistence_tests render_frame_tests mesh_particle_tests plane_particle_tests trail_tests -j 4
ctest --test-dir build/fx-contact-native -R '^(fx_contact|fx|editor_fx|scene_archive|persistence|render_frame|mesh_particle|plane_particle|trail)$' --output-on-failure
```

All nine suites passed in the final native run (4.96 seconds). The new fx_contact
suite has 55 checks covering swept response/queued output, masks, budgets,
settlement/continuation, projection/clipping/UVs, cache invalidation, opt-out,
atomic budget suppression, invalid candidate preservation and exact editor undo.
The authoring preview has an isolated static floor for collision-enabled effects.

## Browser checks

The completed isolated build's `site` outputs and current `web/` host were staged
in `build/fx-contact-site`, served on port 8227. Tests used Chrome 155.0.8059.80,
Playwright/pngjs from `build/detail-browser-tools/node_modules` and forced both
WebGPU and WebGL2. Run with:

```powershell
$env:NODE_PATH='build/detail-browser-tools/node_modules'
$env:BROWSER_EXECUTABLE='C:/Program Files/Google/Chrome/Application/chrome.exe'
$env:DEMO_URL='http://localhost:8227'
$env:EVIDENCE_DIR='build/evidence/fx-contact'
node scripts/fx-contact-smoke.cjs
# Repeat with DEVICE_SCALE_FACTOR=2 and a separate EVIDENCE_DIR.
```

The contact/decal script checks actual rendered pixels and editor interactions:

- Clipped floor/slope projection, volume scaling, normal rejection, receiver masks,
  receiver opt-out and transparent receiver exclusion.
- Atomic source-budget suppression, alpha texture coverage, fog, per-view masks,
  MSAA 1/4, resize, GPU/context recovery and scene reopening.
- Bounce, Slide, Kill, Settle, query exhaustion and application-owned creation of
  one projected mark from a queued impact.
- Collision inspector editing and undo/redo, collision preview/pause with exact
  source preservation, .wefx export/import, DecalComponent creation, opacity and
  imported texture undo/redo, Play/Stop restoration and scene save/reopen.

Evidence is stored under `build/evidence/fx-contact` and
`build/evidence/fx-contact-dpr2`; each contains screenshots, downloaded effect and
scene archives, and `results.json`. Existing FX regression results are under
`build/evidence/fx-contact-regression`.

Both contact/decal runs passed all listed checks at DPR 1 and DPR 2 with zero
page/console errors on both backends. The floor projector covers 94809 red pixels
at DPR 1 and 378378 at DPR 2 on each backend. Existing editor FX, ribbon, plane,
mesh and shaped/soft particle smoke scripts also passed on both backends with
zero errors. `artifact-hashes.json` records matching isolated/served application
JS, Wasm and decode-worker JS hashes. JavaScript syntax checks and
`git diff --check` passed (the shared checkout reports line-ending warnings).

An initial whole-viewport save comparison included the changing demo title; the
script now hides host title/footer/touch controls for rendering comparisons.
Sandbox browser runs reported resource-access errors; final acceptance uses
ordinary local Chrome access and retains strict zero-error assertions.

These checks establish desktop Chromium behavior. They do not establish physical
Android/iOS acceptance, sustained mobile performance, arbitrary skinned/deformed
receiver support or a multi-impact rigid-body particle solver. See the
[feature contract](fx-contacts-decals.md) for delivered boundaries and budgets.
