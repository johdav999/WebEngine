# Stage 2 driving support validation

Verified 2026-10-18 in the shared, uncommitted WebEngine checkout. This record covers
physical surfaces, compounds, impact observations, editor authoring and persistence.
See [contracts](physical-surfaces.md) and [implementation plan](driving-support-plan.md).

## Build and environment

- Native: `build/fx-contact-native`, Debug, configured MinGW GCC 16.1.0 runtime on PATH.
- Browser: `build/fx-contact-web`, Release, repository-local Emscripten **6.0.11**,
  `web_assets` published to `dist`, served at `http://localhost:8098`.
- Chrome **155.0.8059.80**, headless desktop Windows; actual backend asserted as
  WebGPU and WebGL 2 independently. Initial viewport 1450×1000, DPR1 and DPR2;
  resize/recovery and editor checks use 1250×850.
- Exact file hashes are in `build/evidence/physical-surfaces/build-metadata.json`.
  Main Wasm SHA256: `34699e16c3e7e4476651802bc8026ed54579b724730014ce991785f5bee06c79`.
  Worker Wasm SHA256: `32da28d49ba433b762217cf44f16cde408e5c45ab902411ff0a9ecdc0aec4fc4`.

## Results

All **61 native CTest suites passed** (10.88 seconds recorded in
`build/stage2-ctest.log`), including physical_surface, vehicle, physics/joints,
terrain/paging/page-edit, editor/history, archives/persistence, navigation and AI.
The focused suite and vehicle suite also passed as 32-bit Wasm executables in Node.

Focused tests cover offset/rotated compounds, computed COM/inertia through impulses,
convex compound overlap/sweep targets, bounded debug geometry, source child/triangle
metadata, nesting/unit-scale/mesh restrictions, pre-solver impact measurements,
neutral visual edits, rolling resistance, terrain seams, shared visual geometry,
editor history, resource sharing, invalid imported surfaces, explicit v24 neutral
defaults and v25 saves. Physical WETP3 palettes, worker build/hydration, paged sculpt,
writeback and source-budget rejection are exercised in native and Wasm tests.

Native flat-floor braking from 8 m/s measured **4.47439 m asphalt, 8.02094 m gravel,
27.3617 m ice** after 240 fixed steps. Colour-only edits reproduced the asphalt
result. Identical steering commands produced different lateral responses; rolling
resistance reduced coast distance. These are regression fixtures, not tyre calibration.

`scripts/physical-surface-smoke.cjs` passed on **both renderers at DPR1 and DPR2**, with
empty captured page/console error lists. Evidence:

- `build/evidence/physical-surfaces/results.json` and course/editor/recovery PNGs.
- `build/evidence/physical-surfaces-dpr2/results.json` and PNGs.
- `build/stage2-browser.log` and `build/stage2-browser-dpr2.log`.

The compound-chassis browser fixture measured **4.470855 m asphalt, 8.547422 m gravel,
27.435460 m ice**. Both renderers matched within .001 m. Recolouring retained the
asphalt distance; steering and wheel coefficients/IDs differed by surface. Impact
callbacks observed a **7.375537 m/s** peak closing speed and validated the documented
relative-velocity projection. Query checks cover compound child paths and a physical
terrain seam. The fixture uses a different chassis from the native flat-floor test;
no cross-build bit identity is asserted.

Real browser controls verified surface asset creation/assignment, rejected negative
friction preserving valid data, compound child pose/add/undo/redo and duplication,
terrain cell range changes/undo/redo, disabled authoring during Play, source-byte
preservation on Stop, authored file reopen and scene/IndexedDB progress restores.
Resize and forced renderer recovery retained the course. Screenshots were inspected
for vehicle collision outlines, lane rendering and readable editor controls; the
new panel uses the existing editor form styling.

The existing `vehicle-smoke.cjs` also passed on both backends after the change,
covering acceleration/braking/reverse, view/mask/MSAA invariance, editor tuning,
Play driving, blur/suspension, component/preset history and restore/recovery.
Evidence: `build/evidence/stage2-vehicle-regression/results.json`.
The existing `terrain-paging-smoke.cjs` and `terrain-page-edit-smoke.cjs` also
passed on both renderers. They cover real workers, halo seams, source budgets,
cancellation/retry, collision/LOD, dirty pinning, generation-checked atomic writes,
reload, malformed archive preservation and GPU recovery. Evidence is in
`build/evidence/stage2-terrain-paging-regression/results.json` and
`build/evidence/stage2-terrain-page-edit-regression/results.json`. Both reports
have empty captured error lists. These browser regressions use neutral source
pages; physical page palettes/writeback are additionally covered in the native
and Wasm contract suite described above.

Initial checks found duplicate DOM IDs with the old physics playground and a form
layout missing the shared editor class; both were fixed and the dedicated suite was
re-run at both DPRs. Legacy synthetic archive fixtures and older terrain browser
version assertions were updated for the v25 empty surface catalog. A simultaneous
browser regression attempt hit host `ERR_NO_BUFFER_SPACE`; final browser regression
runs use sequential browser processes. These initial failures are not acceptance.

## Reproduce

```powershell
cmake --build build/fx-contact-native -j 2
# Add the configured C++ compiler's runtime DLL directory to PATH.
ctest --test-dir build/fx-contact-native --output-on-failure -j 2
cmake --build build/fx-contact-web --target web_assets physical_surface_wasm_tests vehicle_wasm_tests -j 2
node build/fx-contact-web/physical_surface_wasm_tests.js
node build/fx-contact-web/vehicle_wasm_tests.js
python scripts/serve.py --port 8098 --directory dist
# In another terminal; Playwright/pngjs and browser executable must be available.
$env:NODE_PATH = "$PWD/build/detail-browser-tools/node_modules"
$env:BROWSER_EXECUTABLE = 'C:/Program Files/Google/Chrome/Application/chrome.exe'
$env:DEMO_URL = 'http://localhost:8098'
node scripts/physical-surface-smoke.cjs
$env:DPR = '2'
$env:EVIDENCE_DIR = 'build/evidence/physical-surfaces-dpr2'
node scripts/physical-surface-smoke.cjs
# Clear DPR/EVIDENCE_DIR or set separate regression evidence directories.
node scripts/vehicle-smoke.cjs
node scripts/terrain-paging-smoke.cjs
node scripts/terrain-page-edit-smoke.cjs
```

Installed and CatGame engine-skill copies were updated and checked, including the
matching new physical-surface reference and preserved CatGame dependency-pin guidance.
Documentation does not update a consumer engine pin.

## Limits

Physical surfaces are opt-in and independent of visual layers. Full-source terrain
physical mapping is editable; physical page painting remains unimplemented while
existing page brushes preserve loaded maps. Rolling resistance is the documented
one-step-delayed approximation. Impact data reports pre-solver velocity, not solved
impulse, force or damage. Saves retain supported state, not deterministic replay.
These desktop checks do not establish physical Android/iOS acceptance, sustained
fleet performance, controller hardware acceptance or human handling approval.
