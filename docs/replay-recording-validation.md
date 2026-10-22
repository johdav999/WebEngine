# Replay/ghost validation — 2026-10-21

This change adds bounded pose/event recording and sampled visual playback, not
deterministic solver replay. Evidence covers the shared portable implementation,
browser driving lab/Formula integration and the native Windows vehicle GUI.
Other work is present in this shared checkout; it was preserved.

## Portable and native checks

- Repository Emscripten **6.0.11**, Release browser/Wasm build. Node
  `replay_wasm_tests.js` passed **8080 checks**.
- Native MSVC Release `replay_tests.exe` also passed **8080 checks**. Focused
  CTest `replay`, `vehicle` and `camera_follow` passed **3/3**.
- Earlier MinGW development checks passed before the final budget/ownership
  additions; the final native acceptance above is MSVC Windows, not that older run.
- Tests cover quaternion/TRS conversion/interpolation, telemetry, cut boundaries,
  events, clocks/speed/loop/end, invalid/corrupt retention, file budgets/roundtrip,
  sample/event exhaustion, foreign/removed handles, transient archive exclusion,
  source pose/visibility independence and racing speed reset.
- The budget-edge fixture admits180000 pose samples and2048 events, rejects further
  admission without discarding valid data, then roundtrips that complete recording.

Logs: `build/replay-web-build.log`, `build/replay-windows-build.log`. Build commands
use `webengine web_assets replay_wasm_tests` and native `windows_replay_smoke
replay_tests vehicle_tests camera_follow_tests`. Syntax/content/link/whitespace
checks are recorded separately from runtime acceptance. Existing unrelated compiler
warnings and line-ending notices are not replay failures.

## Browser evidence

Actual Chrome **155.0.8059.85**, sequential WebGPU and forced WebGL2 runs on the
same staged host/Wasm. Desktop1440×1000 and compact390×844 layouts were exercised.

| Fixture | Result | Report |
|---|---|---|
| Driving lab, DPR1 | Both renderers passed | `build/evidence/replay-browser-final/results.json` |
| Driving lab, DPR2 | Both renderers passed | `build/evidence/replay-browser-dpr2-final/results.json` |
| Formula, DPR1 | Both renderers passed | `build/evidence/replay-formula-final/results.json` |

`scripts/replay-smoke.cjs` drives using real browser input and captures over two
seconds. It inspects command/bug events, downloads/imports the actual recording,
reviews at arbitrary times, checks repeatable ghost poses and unchanged live
chassis position, and verifies slow motion plus1× speed on Race. It checks event
jumps, inert source controls, competing-preview rejection, blocked live unpause,
paused racing clocks, GPU recovery, corrupt-file retention, file roundtrip,
compact overflow and terminal shutdown with a late-callback observation window.
Source-scene saves are rejected during temporary review presentation. The driving
lab enables Gameplay controls by default; acceptance uses that default configuration.
Each backend is confirmed from the renderer report; final reports have no page or
console errors. Native pose/velocity checks below complement browser position checks.

The Formula pass initially found that full-screen mode hid the inspector and its
replay controls. The delivered collapsible overlay now lives outside that hidden
ancestor, remains accessible on compact layouts, and keeps recording/ghost clocks
active when collapsed. This was repaired and the complete Formula suite passed on
both renderers. Event lists are transferred on recording revisions rather than
reserialized on every playback status update.

A subsequent Formula run exposed asynchronous import completion replacing a newly
started race. File reads/validation now disable conflicting controls until completion;
the harness waits for the loaded revision, and all final serial runs passed. Failure
state/screenshot diagnostics are retained by the smoke script if an assertion recurs.

Browser logs: `build/replay-browser-final.log`,
`build/replay-browser-dpr2-final.log`, `build/replay-formula-final.log`.
Desktop/compact review and ghost screenshots are retained alongside each report.

## Actual Windows/Dawn acceptance

Built the public native vehicle application and `windows_replay_smoke` with the
configured Visual Studio/MSVC x64 toolchain, SDL3 and Dawn/D3D12. The executable
uses an actual native hidden window and swapchain, not browser emulation.
`scripts/replay-windows-smoke.ps1` passed with a browser-generated compatible file:

- `build/evidence/replay-windows-final/report.json`: passed; browser file accepted.
- Native session: **3.13333 seconds,95 frames,10 events**.
- GUI Record/Finish/Review/Race and the native Slider were activated through SDL
  test input. Real vehicle movement and reset produced recorded poses/cut events.
- Review held both live chassis position **and velocity**; seek paused playback.
- Native file roundtrip and corrupt-load retention passed; a browser `.weghost`
  loaded with matching course/geometry context.
- The ghost had no rigid-body/vehicle components and paused with simulation time.
- Actual **1280×720** Dawn framebuffer readbacks were validated and inspected:
  `review.ppm/png`, `ghost.ppm/png`. `tools/verify_replay_windows.py` rejects blank
  or identical review/race frames. This is native rendering evidence, separate
  from the browser reports.

The evidence directory includes `native.log` and `binary-hashes.json`. Test-only
SDL injection is isolated in `tests/windows_replay_smoke.cpp`; no synthetic input
or test exports were added to application/runtime APIs. Initial build retries
encountered concurrently added desktop-runtime files/header dependencies; the
completed native build and acceptance passed after those unrelated updates settled.

## Tested identities

| Staged artifact | SHA256 |
|---|---|
| `dist/webengine.wasm` | `acd0d7d6c4017b1d61be1d4798a024149abcc09a20b8d8867f1126486a682999` |
| `dist/webengine.js` | `1aee9a482520a4940a00822b032fa9373695a3e5b8f5c09f1bf0fccb0ac1d986` |
| `dist/app.js` | `65da184ddd7cd82b0db29c0b6ad483ea84a3c2f687232be185fae4bdb3e0c06a` |
| `dist/replay.js` | `81d340b490e379eaac77e921e4dd512e22014958d8b04d2a85c8c2a56837ad0f` |
| `dist/replay.css` | `5daf0366f2d507d6e08e82e4cd9fb6ace35a5269fe145db94ae6a87c7ee95ce6` |
| Native replay smoke | `2918211aba10d707af48863fc6db165b02cc4cf98fd615db9bf29cc6a24c5ac2` |
| Native vehicle application | `de360255dee55651ffae00111e80fe630464a21202c6b61dcb534f4c27b8f630` |

Both required engine-skill copies were updated and verified while preserving
CatGame's dependency/pin preamble and concurrently added Windows guidance.
No consumer gitlink, shipping package or published deployment was changed.

## Acceptance boundaries

Capture is bounded in-memory up to30Hz in the shared driving example. Dropped
render frames are not fabricated solver samples. Wheel spin above the sample
Nyquist limit cannot be reconstructed, and reset markers are quantized to capture
boundaries. Files omit scenes/resources and full-world state. Diagnostics/events
do not execute gameplay, replay sound, reproduce contacts or rerun physics.
The provided proxy supports static mesh parts; skeletal/morph/scenery/NPC capture,
dynamic-world rewind, compressed/video export and competitive lap validation are
not delivered. Course fingerprints are compatibility hints, not authenticity.
No physical mobile/controller, Safari, high-DPI native display, driver-reset,
long-duration memory or sustained-performance acceptance is implied by these tests.

See [workflow and API contracts](replay-recording.md).
