# Audio validation — 2026-10-10

The audio implementation was verified in this Windows checkout using the pinned
repository-local Emscripten 6.0.11 SDK and Chrome 154.0.8037.99.

| Check | Result |
| --- | --- |
| Release browser build in `build/web-audio-release` | Passed; core, browser adapter and application example compiled/linked. |
| `ctest --test-dir build/native -R '^audio$' --output-on-failure` | Passed, 1/1 focused native suite. |
| `node scripts/audio-system-smoke.cjs` | Passed, 12 checks; no console/page errors. |
| Existing `node scripts/audio-smoke.cjs` | Passed, 16 MP3/WAV waveform-player regression checks; no console/page errors. |
| Desktop and 390-pixel screenshots | Inspected; labeled full-width volume/position sliders and wrapping buttons. |

The browser scripts used `NODE_PATH=build/detail-browser-tools/node_modules`,
the installed Chrome executable and an isolated copy of the site served on
`http://localhost:8107`. Test results are in
`build/evidence/audio-system/results.json` and
`build/evidence/audio-player-regression/audio-results.json`.

The native suite covers PCM validation and ownership, independent/stale/foreign
voices, budgets, transactional backend failure, loops/offsets, hierarchical
gains/mutes, linear/interrupted fades, inactive fade clocks, inherited emitter
positions, listener orientations, invalid-pose recovery and component lifetime
boundaries including pending destruction and scene transfer/unload.

The browser checks exercise the actual C++/Wasm service and real Web Audio
objects. They observe one shared buffer for three simultaneous voices, nonzero
mixed PCM samples, silent/restored output through master mute, composed bus gains,
one-shot completion, continued loops, HRTF left/right balance after moving the
scene emitter, intermediate fade gains during simulation pause, released buffers,
real MP3/WAV decoding, corrupt/cancelled replacement preservation, host suspension,
gesture reactivation, emulated narrow-layout fit and complete device disposal.

The explicit activation gate is enforced even when the browser grants autoplay
permission. This establishes pending voice and gesture activation behavior;
it does not establish all browser autoplay policy variants. The measured samples
do not prove physical speaker output. Mobile viewport emulation is not physical
Android/iOS acceptance. No sustained-performance benchmark, native audio device,
audio archive codec, streaming or DSP/occlusion acceptance is claimed.

Stage 4 extends these historical audio checks with playback-rate and driving
example coverage. See [new contracts](driving-audio.md) and
[Stage 4 evidence](driving-audio-validation.md); the earlier records above are
not a new validation run of those extensions.
