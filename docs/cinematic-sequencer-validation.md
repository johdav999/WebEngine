# Cinematic sequencer validation — 2026-10-21

Scope: immutable assets/clock, independent native authoring history, isolated
preview, browser timeline/editor integration and the procedural greeting example.
The shared checkout contains other engine work; this is focused contract evidence,
not a claim that every modified subsystem or consumer pin was revalidated.

## Core and builds

- README toolchains, repository Emscripten **6.0.11**, Release Wasm and Debug native.
- Built `cinematic_sequence_tests`, `audio_tests`, `animation_control_tests` and
  `fx_composition_tests`; focused CTest run passed **4/4**.
- Direct native and Node `cinematic_sequence_wasm_tests.js` each passed **204 checks**.
- Built/staged `webengine web_assets`, including matched main/worker/browser files.
  Logs: `build/cinematic-native-build.log`, `build/cinematic-wasm-build.log`,
  `build/cinematic-web-build.log` and `build/cinematic-stage.log`.
- Syntax checks and scoped `git diff --check` passed. Existing unrelated compiler
  warnings and line-ending normalization notices are not cinematic test failures.

Checks cover complete validation and failed-state retention, strict WECINE file
roundtrip, interpolation/shortest Euler arcs, conflicts, clock/end/loop, undo/redo,
source independence and byte preservation, clone/binding/physics/projection guards,
absolute animation/camera sampling, deterministic FX and sound lifecycle. A one-hour
film with a late FX clip reconstructs at most600 variable steps; seeking outside
active FX spends zero replay steps. Contact-driven FX reject; nullable decal-only
composition triggers remain valid. Silent sequences do not need an audio device.

## Actual Chrome acceptance

Chrome **155.0.8059.85**, localhost8137, sequential WebGPU and forced WebGL2 runs:

| Suite | Result | Report |
|---|---|---|
| Cinematic, DPR1 | Both passed, no page/console errors | `build/evidence/cinematic-sequence-final/results.json` |
| Cinematic, DPR2 | Both passed, no page/console errors | `build/evidence/cinematic-sequence-dpr2/results.json` |
| Environmental audio regression | Both passed | `build/evidence/cinematic-audio-regression/results.json` |

`scripts/cinematic-sequence-smoke.cjs` tests all five track types, rendered motion,
animation and camera cuts; repeated paused FX seeks compare pixels. It measures
nonzero real streamed audio on gesture Play, silent seek/Pause, source-offset
wrapping beyond the short audio fixture, end/Rewind, and preview audio disposal.
It also tests GPU recovery while paused, source archive bytes before/after preview,
exact key edits/history, single-command drags, Escape cancellation, download/import,
corrupt/invalid retention, compact390px overflow, visible expanded editor controls,
native competing-preview rejection, inert source controls and terminal shutdown
with a late-callback observation window. Each backend is confirmed by its report.

The audio regression additionally measures wall filtering and reverb, twenty-minute
HTTP range streaming without PCM import, seeking/rate/mute, activation/suspension,
pending cancellation, bad-media/blob cleanup and shutdown. Its run predates the last
timeline-only layout change; final cinematic runs cover streaming on the final build.

Visual review captured `workbench.png`, `editor-workbench.png` and
`compact-timeline.png` under the DPR1 evidence directory. It found and repaired a
hidden-ancestor problem when the expanded timeline entered the scene editor.
Expanded panels now live outside hidden inspector ancestors; compact/docked panels
return to the appropriate inspector. Source gizmos and keyboard/pointer controls
are disabled during preview, and prior inert states are restored on Close.

During development a fixture's collision-less Impact trigger was corrected to
Birth. The harness also stopped assuming unrelated app audio devices belonged to
the sequencer, and enters editor mode through its actual UI. One intermediate
source-byte assertion failed and was not reproduced in subsequent instrumented
complete runs; the harness now retains exact before/after archive diagnostics if
it recurs. Final DPR1/DPR2 reports include the strict source-byte assertions.

## Tested staged files

SHA256 after final stage; no subsequent runtime build is required for documentation:

| File | SHA256 |
|---|---|
| `dist/webengine.wasm` | `9edf4829d7e03e0e08726a39f2eaefde69d833b9917046996fc79cdb964fb55d` |
| `dist/webengine.js` | `12a1fe0edb56e8e8b3210eacd3e261e5d1a6012da95abbe48843186fc49f152c` |
| `dist/cinematic.js` | `2727ef2f65925b5336436d65a002d962eacd2c7ca420324169fdd8b673521365` |
| `dist/cinematic.css` | `e505db1b2d7e9b721e7e7de19b46f4bbef9e369a5f681be065cf62d9ae531ea7` |
| `dist/audio-system.js` | `8a862873abb02bf055db862cba902ad9787d96898218e7ff494e1ed19bbaeb93` |

## Boundaries

Scrubbing/frame stepping is silent. FX clips are limited to ten seconds and reject
external-contact surface trails; reconstruction is bounded work, not a frame-time
guarantee. The preview freezes other cloned simulation writers and fixed physics.
Shot crossfades, nested sequences, executable gameplay events, Bezier/quaternion
curves, recording/apply-back, waveform editing and offline video export are absent.
Files retain references; save scenes/resources/audio separately. Runtime games
choose bindings and handoff. Native PCM audio support does not implement encoded
URI stream playback. Desktop Chromium/DPR checks do not establish physical mobile,
Safari, native rendering/audio output, physical speakers or sustained performance.

README, architecture, feature contracts and both installed/CatGame engine-skill
copies were updated. CatGame dependency/pin guidance remains intact; no gitlink
change or consumer implementation is implied. See [feature contracts](cinematic-sequencer.md).
