# Driving Stage 3 validation

Verified 2026-10-18 in the shared, uncommitted WebEngine checkout. Scope:
[gamepad input and composed follow cameras](driving-controls.md), editor authoring,
isolated preview, persistence and both browser renderers. Unrelated work was retained.

## Build and environment

- Native: `build/fx-contact-native`, Debug, configured MinGW GCC16.1.0 runtime DLL
  directory on PATH. Camera suite respects BUILD_TESTING.
- Browser: `build/fx-contact-web`, Release, repository-local Emscripten **6.0.11**;
  `web_assets` published to `dist`, served at `http://localhost:8098`.
- Chrome **155.0.8059.80**, headless desktop Windows, actual backend asserted as
  WebGPU and WebGL2. Dedicated suite viewport1450×1000, DPR1 and DPR2; resized
  recovery/editor viewport1250×850. Existing input regressions also exercise
  Chromium mobile touch emulation at390×844/DPR2.
- `build/evidence/driving-controls/build-metadata.json` records artifact SHA256s.
  Main Wasm: `accf97ee7c3c69697a5bea2fa77402bc52f48b0787bc10b92401fe50f4ba14fb`.
  Worker Wasm: `ab98b69bc515e38410684a0c1ca839bee1064d49db9eb2a2b64873b5bb1a6d93`.

## Portable and adapter results

All **62 native CTest suites passed**, including camera_follow, camera_controller,
input/bindings, vehicle/physical_surface, editor/history, prefab, scene_archive,
persistence and terrain/worker regressions. See `build/stage3-ctest.log`.

`camera_follow_tests` and `camera_follow_wasm_tests.js` passed. Checks cover:

- Simulation/presentation target poses, body/component descendants, physical point
  velocity and exact bounded look-ahead, without changing authoritative state.
- Moving vehicle simulation at60Hz with chase updates at30Hz and144Hz: identical
  final chassis poses; camera positions within .25m numerical tolerance. Constant
  target damping also agrees across30/144 updates per second.
- Final sphere-sweep collision after smoothing, reset/teleport, disabled/removed
  targets and camera handles, invalid settings/time retaining valid state, and
  chase-to-orbit adoption retaining displayed eye/orientation.
- Typed camera property undo/redo, invalid radius preservation, source-byte Play
  isolation, v26 round trips, explicit v25 follow-off defaults and duplicate
  internal target remapping.

Vehicle and physical-surface Wasm suites also passed against the v26 codec. No
input packet ABI change was needed. `scripts/gamepad-adapter-tests.cjs` passed
normalization, dead zones, inversion, clamping/invalid samples, independent keyboard
sources, disconnect polling/events, controller selection, context/blur/enable
re-arming, binding validation, API failure and disposal checks.

## Browser results

`scripts/driving-controls-smoke.cjs` passed on **forced WebGPU and forced WebGL2**
at **DPR1 and DPR2**, with empty captured page/console error lists. Evidence:

- `build/evidence/driving-controls/results.json`, screenshots and build metadata.
- `build/evidence/driving-controls-dpr2/results.json` and screenshots.
- `build/stage3-browser.log` and `build/stage3-browser-dpr2.log`.

Synthetic `navigator.getGamepads()` data and connection events exercise the real
BrowserInput->Application->vehicle command path. Trigger .525 and stick .575 produce
.5 throttle/.5 steering with the example's dead zones; .1 steering stays zero.
Full throttle accelerates the vehicle. Disconnect polling and disconnect events
preserve a held keyboard W source; releasing W clears the remaining throttle.
Controller selection, steering inversion, blur, Menu context and suspension require
neutral re-arm and leave no held gamepad commands in a newly active context.

Camera checks sample alpha0/.5/1, verify explicit simulation targets, compare
identical driving under different camera smoothing/look-ahead, drive in reverse,
reset, hand the displayed chase eye/orientation to editor navigation, and constrain the chase boom against a real collision barrier. Both backends
reported barrier eye position approximately **(0,2.6032815,-2.3531837)m**. The camera
sampler does not mutate the vehicle pose. Resize and forced renderer recovery
retain the composed camera and course.

Real editor forms verified .3->.8s look-ahead authoring, undo/redo and invalid radius
rejection. Viewport lens/transform protections remain active while follow fields
are editable. Hold-preview buttons and actual Chromium touch/cancel feed the same
commands during isolated Play; synthetic gamepad input then drives the preview.
Stop preserves the exact source hash. File reopen retains authored settings, and
runtime IndexedDB progress restore retains camera configuration.

Screenshots were inspected for rear chase framing, visible course/car/collision
outlines, readable device/preview controls and camera authoring fields. These are
block-geometry developer fixtures, not a finished driving game's art or handling.

Existing regression suites passed:

- `vehicle-smoke.cjs`: both renderers, including final reset cancellation; evidence
  `build/evidence/stage3-vehicle-final/results.json`.
- `physical-surface-smoke.cjs`: both renderers; evidence
  `build/evidence/stage3-surfaces-regression/results.json`.
- `input-smoke.cjs`: forced WebGPU and WebGL2, desktop and emulated touch, including
  repeat/reset, overflow, text/GUI and simultaneous virtual source ownership;
  evidence `build/evidence/stage3-input-webgpu/result.json` and
  `build/evidence/stage3-input-webgl2/result.json`.

A DPR2 attempt completed functional checks but captured a transient Windows
`ERR_NO_BUFFER_SPACE` resource request failure. Its report/log are retained at
`build/evidence/driving-controls-dpr2/network-failure-retained.json` and
`build/stage3-browser-dpr2-network-failure.log`. The isolated rerun passed with
empty page/console error and failed-request lists; it is the accepted DPR2 result.

Initial acceptance caught viewport-camera follow fields being disabled by the broad
navigation guard. Per-property authoring eligibility now allows only follow fields
without loosening lens/transform/structure ownership. A test incorrectly waited for
render-frame counters while the host was suspended; it now waits for browser RAF
instead. The older input smoke assumed initial demo framing equalled the canonical
reset pose; setup now explicitly resets before pixel baselines and supports forced
backend selection. A final review also found stale orbit state at chase-to-editor handoff; explicit
adoption now preserves the displayed eye/orientation and its native/Wasm/browser
regression passes. Final results above are new successful runs.

## Reproduce

```powershell
cmake --build build/fx-contact-native -j 2
# Add the configured compiler's runtime DLL directory to PATH.
ctest --test-dir build/fx-contact-native --output-on-failure -j 2
cmake --build build/fx-contact-web --target web_assets camera_follow_wasm_tests vehicle_wasm_tests physical_surface_wasm_tests -j 2
node scripts/gamepad-adapter-tests.cjs
node build/fx-contact-web/camera_follow_wasm_tests.js
node build/fx-contact-web/vehicle_wasm_tests.js
node build/fx-contact-web/physical_surface_wasm_tests.js
python scripts/serve.py --port 8098 --directory dist
# Another terminal:
$env:NODE_PATH = "$PWD/build/detail-browser-tools/node_modules"
$env:BROWSER_EXECUTABLE = 'C:/Program Files/Google/Chrome/Application/chrome.exe'
$env:DEMO_URL = 'http://localhost:8098'
node scripts/driving-controls-smoke.cjs
$env:DPR = '2'
$env:EVIDENCE_DIR = 'build/evidence/driving-controls-dpr2'
node scripts/driving-controls-smoke.cjs
# Separate evidence folders for regressions; remove DPR for their default configuration.
node scripts/vehicle-smoke.cjs
node scripts/physical-surface-smoke.cjs
$env:RENDERER = 'webgpu'
node scripts/input-smoke.cjs
$env:RENDERER = 'webgl2'
node scripts/input-smoke.cjs
```

Architecture/contracts and installed/CatGame engine-skill copies were updated,
including defaults, lifecycle, v26 schema, build/test commands and limits. CatGame
pin/dependency guidance was retained; documentation does not advance its engine pin.

## Acceptance boundaries

Controller inputs here are synthetic and touch is Chromium emulation. No physical
controller, Android/iOS device, haptics, measured input latency, sustained fleet
performance or human handling approval is claimed. Device exposure/activation is
browser policy. Gamepad preferences are not stored in scenes. Camera collision is
boom shortening against simulation geometry; it does not solve arbitrary initial
penetration or guarantee unobstructed framing. Camera/input runtime history is
transient, and scene archives are not deterministic solver replay.
