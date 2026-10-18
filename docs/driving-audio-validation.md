# Driving audio validation — 2026-10-18

Stage 4 is implemented in the existing shared checkout, preserving prior vehicle,
surface, camera and unrelated work. Scene archives remain v26; separate tuning
JSON is v1. Native output uses a deterministic test backend; there is no native
audio output implementation.

## Builds and native checks

Pinned repository-local Emscripten 6.0.11, Release `build/fx-contact-web`, target
`web_assets`, published to `dist`. Native MinGW Debug `build/fx-contact-native`:
**62/62 CTest suites passed** (10.65 s), including extended `audio` tests. See
`build/stage4-ctest.log`, `build/stage4-audio-native-build.log` and
`build/stage4-web-build.log`.

Audio tests cover default/initial rate, [.125,8] bounds, invalid finite/nonfinite
inputs without publication, independent handles/assets, foreign/completed voice
rejection, backend rate rejection retaining the previous value, suspended controls,
and existing volume/fade/emitter/lifecycle contracts.

## Browser acceptance

Chrome 155.0.8059.80 on desktop Windows, forced **WebGPU** and **WebGL2**, 1450x1000
CSS pixels at **DPR 1 and 2**. `scripts/driving-audio-smoke.cjs` passed on all four
hosts with no page/console errors or failed requests. Inspect the machine-readable
measurements and screenshots in:

- `build/evidence/driving-audio/results.json`
- `build/evidence/driving-audio-dpr2/results.json`
- Corresponding `webgpu-editor.png`, `webgl2-editor.png` and driving screenshots.
- `build/evidence/driving-audio/build-metadata.json` records final artifact hashes.

Assertions use actual C++/Wasm voices, Web Audio nodes and analyser samples:
queued pre-gesture rate changes, observed ~440/~110 Hz output from a 220 Hz source
at rates 2/.5, same BufferSource after updates, silence under mute, device clock
freeze, explicit gesture resume, rate-dependent one-shot release and source-second
offsets (~.13 active device seconds for the final .25 source seconds at rate 2).
Suspending and re-enabling the driving example retains its four sources/cursors.

The real car supplies RPM/throttle, gravel contact and handbrake slip. An opt-in
fixture drives the real chassis into surfaced static barriers; it does not inject
collision callbacks or audio results. Reports retain gravel/hard impact selections
and successful hit counts. Voice/buffer counts return to zero after pause/Stop,
scene replacement and shutdown; suspension retains voices without autoplay.

Editor tests cover typed tuning, separate Undo/Redo, invalid file rejection
without replacing tuning, downloaded tuning JSON, valid file import/local reload,
private rate preview with unchanged scene SHA-256, disabled tuning during isolated
Play, and unchanged source hash after Stop. Scene save/open retains tuning as
host configuration and stays silent; no scene audio codec is implied.

Existing `scripts/audio-system-smoke.cjs` passed with each forced backend,
covering MP3/WAV import, corrupt/cancelled replacement, shared buffers, sampled
mute/spatial balance, fade-stop, suspension, shutdown and a 390-pixel emulated
viewport. Evidence: `build/evidence/stage4-audio-webgpu/results.json` and
`build/evidence/stage4-audio-webgl2/results.json`.
`scripts/driving-controls-smoke.cjs` also passed on both renderers:
`build/evidence/stage4-driving-controls-regression/results.json`.

## Scope and diagnostic findings

Screenshots were inspected for readable controls and backend identification.
Early test iterations were corrected to account for suspended AudioParam reads,
render counters not advancing during host suspension, command gating while the
host is suspended and keyboard input requiring canvas focus. A falling car can be
supported entirely by raycast wheels, so actual barrier contacts validate chassis
impact audio. Final acceptance uses real sample/contact observations, not those
initial expectations.

Sounds are quiet generated illustrations, not production recordings. Analyser
samples do not prove physical speaker/headphone output, subjective mix quality,
physical gamepad/mobile acceptance or sustained mobile performance. Independent
pitch shifting/time stretching, Doppler, streaming and built-in sound archives
remain outside this stage.
