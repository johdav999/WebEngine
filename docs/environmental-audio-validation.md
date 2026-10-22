# Environmental audio validation

Verified **2026-10-21** in the shared WebEngine checkout; unrelated changes were
preserved. No commit, push, consumer pin change or deployment was requested.

## Portable and Wasm contracts

- Native Debug `audio_tests` / CTest `audio`: **passed**. Existing ownership,
  playback/fade/rate and scene-handle tests remain, extended with URI validation,
  stream ownership and no-PCM representation, seek/rate/scoped handles, backend
  failure retention, room rollback, global wet mute/stop cleanup, actual collision
  occlusion, smoothing, suspension, sensor/self exclusions, layer masks, fair
  one-query admission, moving/disabled walls and emitter scene-unload cleanup.
- Explicit `audio_wasm_tests`: **passed** on Node using the same source and the
  32-bit Wasm ABI. Output: `Audio contracts passed, including environment and streams`.
- Repository-local Emscripten **6.0.11**, Release `build/web-release`, targets
  `webengine web_assets audio_wasm_tests`. Staged host and binaries match source;
  the short preview MP3 is copied from the existing synthetic test fixture.
- Logs: `build/environment-audio-native-build.log`, `environment-audio-native-tests.log`,
  `environment-audio-wasm-tests.log`, `environment-audio-web-build.log`, and final
  `environment-audio-web-final-build.log`.

## Actual browser output

Chrome **155.0.8059.85**, Windows desktop, localhost:8137. Tests ran serially on
actual WebGPU and forced WebGL2, at **DPR1 and DPR2**. Effects use real AudioContext,
Panner/Biquad/Convolver/MediaElement nodes and output analysers, not mock audio.

`scripts/environment-audio-smoke.cjs` passed on both backends at both DPRs:

- A pre-gesture voice remains suspended, then explicit activation produces samples.
- The synchronized obstruction wall reduces RMS from approximately **.0250 to
  .00843** and suppresses the 3kHz band relative to 220Hz by over **15dB**. Bypass
  and removing the wall restore the signal. Native tests enforce query budgets.
- Hall reverb retains measurable RMS **.00117** after the dry voice stops and
  more than the analyser's history window has elapsed; it decays to below .00001.
  Master mute and stop-all silence wet output. Invalid room settings retain the graph.
- A **twenty-minute** virtual 16kHz mono WAV streams through real HTTP 206 byte-range
  responses, seeks to **900s**, plays nonzero samples, changes rate, and advances
  while muted. Across both backends each DPR run transferred **983,040 bytes** of
  the virtual **38,400,044-byte** resource. No full resource allocation occurs in
  the fixture server, no decodeAudioData call occurs, no shared PCM AudioBuffer is
  created for the track, and Wasm heap growth stays below 4MiB during playback.
- Host suspension pauses both context and media cursor; return stays inactive until
  another gesture. Stop removes src, pauses media and disconnects its nodes.
- Pending play cancellation cannot revive a stopped track. Corrupt media releases
  the voice; local-file streaming uses no PCM decode and revokes owned blob URLs
  after stop/error. The built-in short sample also streams successfully.
- Controls fit a 390px viewport. Application shutdown disposes all audio devices;
  an explicit 350ms observation window checks late callbacks and the native terrain
  probe returns 0 after shutdown. Final reports contain no console/page errors.

Evidence and screenshots: `build/evidence/environment-audio/results.json` and
`build/evidence/environment-audio-dpr2/results.json`.

## Integration regressions and diagnosed failures

Existing `audio-system-smoke.cjs` passed on both renderers: overlapping/shared
sounds, gains/mutes, completion, HRTF movement, fades, MP3/WAV imports, corrupt and
cancelled loads, host suspension, compact controls and shutdown. Reports:
`build/evidence/audio-existing-environment-{webgpu,webgl2}/results.json`.
`driving-audio-smoke.cjs` passed both renderers for retained variable-rate sources,
actual samples, vehicle telemetry/physical-surface selection, tuning history/files,
isolated editor preview/Play, scene replacement and shutdown. Report:
`build/evidence/environment-driving-audio/results.json`.
The existing `audio-smoke.cjs` MP3/WAV waveform suite also passed: actual output,
play/pause/stop/seek, asynchronous import cancellation, invalid replacement,
renderer-failure independence, 390px layout and disposal. Evidence is under
`build/evidence/environment-waveform-audio/`.

Initial test development caught an unfilled analyser reference window, a missing
browser closure argument, and a missing test include. These were corrected before
the final portable/browser acceptance. More significantly, the post-shutdown window
reproduced `RuntimeError: null function` in **terrain.js's inspector polling**:
the browser still reported ready after Application shutdown, allowing a timer to
borrow the released scene. The fix publishes terminal data-engine=stopped, ignores
late renderer status callbacks, stops terrain polling and guards the native probe
before displayedScene access. The final DPR1/2 runs pass with that window retained.
The waveform suite additionally found two existing controls overflowing the narrow
page: the header's appended editor button and the crowd button row. Their flex
containers now wrap; the 390px document measures 390px wide with no overflowing
elements, and the original waveform test passes without relaxing its assertion.

Installed and CatGame engine-skill copies were updated, including SKILL.md and
environmental audio/integration/contract-map/build references. Their feature
references match; CatGame dependency/pin and current native host guidance remain.

## Final build and limits

Main Wasm SHA256:
`242256A8BA1C9CC29FF22950B46CBA9C6A51404AD2535031FAE98B95308E578D`.
Decoder worker:
`C29F77DAC3CE3A21ECDEA8ECCB0A516CA84F7A6013465BDB3CF403418F554543`.
The audio changes add no resource/archive fields or format version.

This validates output samples in desktop browsers. It does not establish physical
speaker/headphone quality, physical Android/iOS, Safari, native audio output,
arbitrary codecs/CORS servers, sustained mobile performance or a hard browser
buffer-memory ceiling. The example track is a synthetic preview, not authored
music. Occlusion is a single-ray approximation, and one explicit synthetic room
per device is supported. See [contracts and limits](environmental-audio.md).
