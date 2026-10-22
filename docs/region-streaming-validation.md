# Region streaming validation — 2026-10-19

Stage 8 is implemented as optional static-content residency in engine_core, with
portable editor commands and an application-owned browser preview. This report
records runs performed for this change, not physical-device acceptance.

## Native and Wasm

- Configured native Debug/MinGW build: `cmake --build build/fx-contact-native --parallel 2`.
- Matching compiler runtime bin on PATH; `ctest --test-dir build/fx-contact-native
  --output-on-failure --parallel 2`: **70/70 passed**, 77.33 s. Includes archive,
  persistence, editor/history/structure, assets, application, prefabs, physics,
  vehicle, terrain, route, FX, spotlight, animation and region regressions.
- Focused `region_stream_tests`: **72 checks**; explicit
  `region_stream_wasm_tests.cjs` in pinned local Emscripten **6.0.11**/Node:
  **72 checks passed** on the Wasm ABI.
- Native evidence: `build/evidence/region-stream-native.log`; the final focused rerun is in
  `build/evidence/region-stream-native-final.log`.

The focused suite covers manifest/bounds/restore-key/budget rejection, exact-state
editor undo/redo, v31 persistence, separate delayed visual/collision readiness,
registered Jolt queries, scoped IDs and fresh handles after reload, transient cache
and resource exclusion, read-only residents, body/kinematic/explicit contact pins,
clear refusal, selected checkpoint data, missing and successful restore handoff,
border union/outside coverage, cancelled late completions, corrupt-source rollback,
retry, region-count/byte backpressure, decoded payload part/type/bounds rejection,
body-scan failure, disabled-branch validation, inactive scene suspension, actual
sleeping bodies, retained support on owner destruction, identity conflicts, the 512-node payload boundary/overflow and transformed-target
rejection before publication, and
real high-speed vehicle traversal with missing-support gating.

## Real renderer runs

Built `web_assets` and `region_stream_wasm_tests` in `build/fx-contact-web`, using the
repository-local SDK. A matched copy of dist, including main/worker Wasm, host
adapters and assets, is frozen in `build/stage8-validation-site`, served at
`http://localhost:8114`. SHA-256 identities are in
`build/evidence/region-stream-site-hashes.json`. Current live dist is also served
on port 8110 for the editor example.

Chrome **155.0.8059.80**, desktop headless, initial viewport 1500x1050 then 1180x850:

| Renderer | DPR | Region suite | Peak speed | Traversal | Failed-support stop | Deferred teleport | Recovery changed pixels |
| --- | --- | --- | --- | --- | --- | --- | --- |
| WebGPU | 1 | passed | 64.95 m/s | 249.44 m | z=13.99 m | z=240 m | 0 |
| WebGL2 forced | 1 | passed | 64.95 m/s | 249.44 m | z=13.99 m | z=240 m | 0 |
| WebGPU | 2 | passed | 64.95 m/s | 249.44 m | z=13.99 m | z=240 m | 0 |
| WebGL2 forced | 2 | passed | 64.95 m/s | 249.44 m | z=13.99 m | z=240 m | 0 |

Each run used `scripts/region-stream-smoke.cjs`. It verifies the actual selected
backend; world-box overlays/forms, atomic invalid edit preservation, exact history,
record addition, static actor capture/download, source validation/import and failed
replacement preservation, application of a registered source, required-restore
failure before gameplay, isolated preview/source hash preservation, delayed/failed
collision at speed, body pins, 4-region / 2 MiB reservations, explicit retry,
destination readiness before teleport, v31 scene reload, resize and device recovery.
The final traversal evicted 12 parts and retained one pinned region / 393216 reserved
bytes. Missing support gated 121 steps instead of crossing the border. Small
traversal-distance differences arise from one presentation step before the test
switches the preview to manual advancement; this is not a renderer physics claim.
No unexpected page/console errors or failed requests were recorded. External font
requests were fulfilled with empty CSS by the test harness.

Evidence:

- `build/evidence/region-stream-final/results.json` and screenshots/source files.
- `build/evidence/region-stream-dpr2/results.json` and screenshots/source files.
- `build/evidence/region-stream-site-hashes.json` for the frozen tested site.

Existing `scripts/vehicle-smoke.cjs` also passed on WebGPU and WebGL2: settling,
acceleration/braking/reverse/steering, wheel rendering, view/MSAA independence,
scene/progress restoration, editor tuning/history, isolated Play, component
removal/preset creation, blur cancellation and resize/recovery. Evidence:
`build/evidence/region-stream-vehicle-regression/results.json`.

## Scope and limits

Supported payloads are explicit static mesh/light and static solid body/collider
archives. Dynamic gameplay state is application selected outside resident caches;
there is no automatic per-region dynamic-entity checkpoint, terrain-page takeover,
worker-based scene construction, background Jolt cooking or solver-exact save.
The three-times source allowance is admission accounting, not a hard process/GPU
memory bound. IO, prediction, wheel/contact footprints, loading UI and playable
restore handoff remain consumer policy. Coverage readiness establishes geometry
presence inside declared volumes, not a continuous ground-support proof.
Synthetic preview delays use steps, not real HTTP timings. Desktop browser checks
do not establish physical Android/iOS behavior, sustained mobile performance,
large-world precision or production network/storage-provider acceptance.
