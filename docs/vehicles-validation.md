# Vehicle foundation validation — 2026-10-18

## Build and environment

- Shared Windows checkout, existing unrelated changes preserved; HEAD at validation:
  `3fe7d1dc1dc610c703944803cbb717cd65024ec9` plus working-tree implementation.
- Native Debug: `build/fx-contact-native`; MinGW C++20 compiler already configured
  by this checkout. Browser Release: `build/fx-contact-web`, repository-local
  Emscripten 6.0.11 and its Dawn port. No native renderer was added.
- Chrome 155.0.8059.80, headless Windows desktop, actual `webgpu` / `webgl2`
  capability snapshots asserted. Viewport 1450×1000, resize 1200×850, DPR1 and DPR2.
- Selected `web_assets` output served from `dist` on localhost:8098. Exact build
  metadata and hashes: `build/evidence/vehicle/build.json`.
- Final Wasm SHA256: `f3aabae9ee7046528fa0738e91e80af008266af64e5b5f9a329498e32e568055`.
- Final host JS SHA256: `c78ae9fbe4976e42edb7a6d826e16e45745c8d92da793cfb2fae3b2a9c9aff74`.

## Checks actually run

| Check | Result | Evidence |
| --- | --- | --- |
| Complete native CTest suite | 59/59 passed, 20.03 s | `build/vehicle-native-ctest.log` |
| Vehicle native contract suite | Passed, included above | `tests/vehicle_tests.cpp` |
| Same vehicle contracts on 32-bit Node/Wasm | Passed | `build/vehicle-wasm-tests.log` |
| Vehicle browser suite, DPR1 | Both renderers passed, no captured errors | `build/evidence/vehicle/results.json` |
| Vehicle browser suite, DPR2 | Both renderers passed, no captured errors | `build/evidence/vehicle-dpr2/results.json` |
| Existing physics browser regression | Passed | `build/evidence/vehicle-physics-regression/results.json`, `build/vehicle-physics-regression.log` |
| Vehicle/editor screenshots | Inspected from both backends, including DPR2 Details dock and Play | PNGs in both vehicle evidence directories |
| Installed and CatGame engine-skill copies | Vehicle references identical; new links/content verified, existing guidance retained | `references/vehicles.md` in each copy |

Native/Wasm vehicle coverage includes four-contact rest and bounded suspension,
forward acceleration, braking, reverse, steering, airborne contact loss, hill support,
bank normals, CCD wall collision, handbrake deceleration, coherent wheel presentation
and previous-pose interpolation, torque/point-force/impulse/velocity, invalid settings
and visual bindings, component/collider/body disable/removal/rebuild, scene transfer,
shared definitions, remapped duplicate IDs, checkpoint restore, invalid gear/checksum,
editor preset/property history/removal, and isolated Play preserving source bytes.

The native regression run required updating five exact current-version expectations
from 23 to 24. Its legacy-water migration fixture also needed to strip the existing
v23 relief fields when synthesizing old material formats; all legacy-load assertions
remain and now pass. No water runtime functionality changed for this task.

Each browser run executes the same command sequence for both renderers and asserts
backend-independent results within .001 for speed and position. It verifies actual
canvas changes, contacts/suspension, braking/reverse/turning, masked/split views and
1x/4x MSAA without simulation changes, binary scene saves, IndexedDB progress restore,
resize/device-context recovery, vehicle property tuning with undo/redo, invalid radius
and duplicate-component rejection, isolated Play driving, suspension/blur cancellation,
Stop byte equality and fresh paused-source telemetry, authored reopening, component
removal/undo, and complete preset creation/undo/redo. The test-only manual step/view
commands require `vehicleTest=1` and refuse manual simulation while the editor owns
its scene or the application is running.

The browser fixture measured 11.0201 m/s after its 120-step acceleration sequence,
9.7861 m travel, service-braked to about -.0214 m/s and reached -13.6404 m/s in its
reverse sequence; both renderers and DPRs produced the same recorded values. These
are fixture observations, not vehicle handling targets or real-world calibration.
Native/Node contracts use a separately initialized fixture and bounded behavioural
assertions, rather than a cross-platform bitwise replay promise.

A test-harness attempt expanded a large binary archive into JSON and exhausted the
Node heap. The final suite keeps binary saves inside the browser and completed.
One parallel-link Wasm optimizer process crashed; retrying at `-j 2` completed and
the resulting final artifact passed the tests above.

## Reproduction

Use current README setup and the repository-local SDK. Existing configured builds:

```powershell
cmake --build build/fx-contact-native
ctest --test-dir build/fx-contact-native --output-on-failure
cmake --build build/fx-contact-web --target web_assets vehicle_wasm_tests -j 2
node build/fx-contact-web/vehicle_wasm_tests.js
python scripts/serve.py --port 8098 --directory dist
# Another terminal; set NODE_PATH to Playwright/pngjs and BROWSER_EXECUTABLE.
$env:DEMO_URL = 'http://localhost:8098'
node scripts/vehicle-smoke.cjs
$env:DPR = '2'
$env:EVIDENCE_DIR = 'build/evidence/vehicle-dpr2'
node scripts/vehicle-smoke.cjs
```

Native Windows test processes also need the configured compiler's runtime DLL
folder on PATH. Evidence is scoped to this staged build/browser. Diagnostic block
wheels/course are not production assets. Physical devices/controllers, sustained
fleet performance, simulation-grade tyre calibration and human handling approval
remain unverified. Supported saved state is deliberately partial; no deterministic
solver checkpoint or replay is claimed. See [vehicle contracts](vehicles.md).
