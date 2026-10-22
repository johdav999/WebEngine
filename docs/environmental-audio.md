# Environmental audio and music streams

The optional application-owned `AudioSystem` adds wall occlusion, room reverb
and encoded music streams. Existing dry PCM playback defaults stay unchanged.
Open **`?scene=audio`** for the audio lab and click **Play environment sound** to
activate audio. Toggle **Obstruction wall**, bypass muffling, and compare Dry,
Small room and Hall. Stop the environment sound to hear the room tail. Stream
sample loops the existing short MP3 fixture; **Stream music file** accepts a local
long track without importing decoded samples. Playback requires a gesture.

```cpp
AudioSystem audio(std::make_unique<BrowserAudioBackend>());
AudioOcclusionSettings walls;
walls.enabled = true;
walls.layers = 1 << 3; // Physics collision layers, independent of render masks.
audio.setOcclusion(walls);
audio.setReverb({.4f, 1.5f, .02f}); // wet, decay seconds, pre-delay seconds.

AudioPlayOptions effect;
effect.loop = true;
effect.occlusion = true;
effect.reverbSend = .7f;
auto voice = audio.playEmitter(emitter, effect);
// The host synchronizes/advances the scene first, then updates audio once.
audio.update(presentationDelta, &scene);

AudioPlayOptions music;
music.loop = true;
auto track = audio.playStream(AudioStreamAsset::create("music/score.ogg"), music);
audio.seekStream(track, 120); // Source seconds; pending metadata defers validation.
audio.setVoicePlaybackRate(track, 1.25f);
audio.stop(track, 1); // Existing composed bus/mute/fade controls apply.
```

## Wall occlusion

Both `AudioOcclusionSettings::enabled` and a spatial voice's `occlusion` must be
true. `playEmitter` makes a voice spatial; direct voices can use `spatial=true`.
Each admitted probe raycasts the existing synchronized physics world from the
listener to the emitter, excluding sensors and the bound listener/emitter owner
actors. It includes static, kinematic and dynamic solid bodies on selected layers.
Direct voices have no bound actor exclusions. No physics world is created merely
for audio. Missing physics, a missing/disabled bound listener, or a null scene
clears the obstruction target. Disabled/destroyed/foreign emitters retain their
existing voice-stop contract. Render interpolation, masks, culling and LOD do not
affect acoustic positions. Queries do not advance simulation or synchronize it.

Defaults: disabled, layers0xffff, **8 queries/update** (valid 1..64), blocked gain
**.35** (0..1), low-pass **800Hz** (20..20000), transition **.1s** (0..10).
Round-robin admission prevents starvation when the voice count exceeds the budget;
unvisited voices retain their last target. `lastOcclusionQueries()` reports actual
raycasts, and `voiceOcclusion(id)` the smoothed 0..1 amount. A ray ends .01m before
the emitter; separation <=.02m needs no ray. Active device time drives exponential
smoothing; frequency interpolates logarithmically from 20kHz to the blocked cutoff.
Zero transition is immediate on the next update. The browser clamps the cutoff
below its Nyquist frequency. Gain multiplies the existing bus/voice volume without
changing authored volume or fades. Suspension freezes smoothing and spends no rays.

This is a single-ray obstruction approximation: no diffraction, wall-thickness or
material transmission, portals, multiray partial occlusion, Doppler or propagation
delay. Applications choose acoustic collision layers; visual-only walls cannot
occlude. Query cost is bounded per update, not per second.

## Room reverb

`AudioReverbSettings` defaults wet0, decay1.5s, preDelay.02s. Wet is 0..1,
decay .1..8s and pre-delay 0..0.2s, all finite. `setReverb` validates the complete
candidate before asking the backend; false retains the previous accepted room.
`AudioBackend::capabilities()` reports filtering, reverb and streaming separately.
New optional backend methods default to unsupported/dry behavior, preserving
existing custom backends. Gain-only occlusion remains possible without a filter.

The browser owns one shared stereo ConvolverNode room per device, fed by each
voice's `reverbSend` (0..1, default0). Sends follow spatialization, occlusion,
voice gain and bus gain. Wet is an additive return gain; it does not reduce dry
volume. A bounded deterministic synthetic impulse response uses
the device sample rate and requested decay, with a short optional pre-delay.
No provider asset or download is required. Setting a room before playback retains
configuration without opening an audio device. Active room changes stage a
replacement graph and publish it only after successful construction. Repeated
room edits are synchronous and should be issued on settings changes, not every
frame. A room holds at most eight seconds of stereo IR; replacement may temporarily
hold both old and new resources. Room changes replace, rather than crossfade, tails.

Stopping/completing an individual voice leaves its natural room tail. Master mute
or zero master volume silences dry and wet output; existing tails still decay on
the device clock. Individual voice/bus mutes stop new input to the room, allowing
earlier tails to decay. Stop-all clears tails, and disposal disconnects the graph.
Host suspension freezes the AudioContext. There are no automatic room volumes or
geometry-derived acoustic simulation; applications select rooms from listener state.
This room model does not accept custom measured impulse-response assets yet.

## Streamed tracks

`AudioStreamAsset` is an immutable shared URI reference, with no PCM payload,
duration estimate, browser object, clock or retained scene pointer. URI length is
1..4096 bytes with no whitespace/control bytes. The browser resolves relative
URLs and accepts HTTP, HTTPS or blob URLs; unsupported schemes reject before
publishing a voice. Encoded formats depend on the browser's media codecs.

`playStream` uses the same scoped, nonrecycled voice handles and combined voice
budget (default128). The browser additionally bounds simultaneous streams to **8**.
It owns one HTMLAudioElement and MediaElementAudioSourceNode per stream, routed
through the same optional filter/panner/gain/send graph. It sets metadata preload,
and the browser progressively fetches/decodes media; it does not call decodeAudioData,
create a full-track AudioBuffer, or copy full PCM into Wasm. Buffering/network memory
and fetch strategy remain browser policy. Serve seekable tracks from a server with
HTTP range support. The local development server can play progressive media but
does not promise efficient seeking for remote tracks without range support.

Start success means a pending voice was accepted, not that metadata or audio has
loaded. Pending voices remain live through activation blocking; async media errors
release the voice and report the device's error. A failed async load does not restore
a previously stopped track. `streamPosition` returns media source seconds (-1 for
unknown/PCM). `seekStream` accepts finite nonnegative source seconds; before metadata
it queues the offset. Initial looped-stream offsets wrap by the duration once
metadata arrives; out-of-range initial offsets on non-looped streams fail/release.
With
metadata, out-of-range seeks reject without changing the cursor. Live/nonseekable
media may reject seeking. Loop, composed volume/mute/fades and .125..8 playback rate
apply; media disables pitch preservation to match PCM speed/pitch semantics.

Activation invokes context resume from a gesture and starts pending media; autoplay
rejection retains the voice for another gesture. Host suspension immediately pauses
media as well as the context so its cursor freezes. Resume requires another explicit
activation. Stop/completion/error clears media handlers, pauses, removes src, aborts
loading and disconnects nodes. Late play/metadata completions check voice identity
and cannot resurrect a stopped voice. The caller owns blob URL lifetime; revoke it
after stopping every voice that uses it. The example owns/revokes its file URLs on
replacement, stop/error and pagehide; local files are not uploaded.

Cross-origin media must permit CORS for Web Audio processing. See the
[Web Audio specification](https://www.w3.org/TR/webaudio-1.0/) and
[media element source](https://developer.mozilla.org/en-US/docs/Web/API/AudioContext/createMediaElementSource)
for routing restrictions. Browser buffering follows
[preload policy](https://developer.mozilla.org/en-US/docs/Web/API/HTMLMediaElement/preload),
which is a hint rather than a strict memory cap.

## Ownership, persistence and verification

Audio remains an explicit optional engine_core service. Physics is borrowed only
during update; device graphs/media belong to engine_audio_browser. No global
registration, second simulation clock, callbacks into destroyed scenes, renderer
dependency, native output backend or archive version is added. Runtime settings,
streams, voices and room graphs are transient; applications resolve resources and
recreate playback when loading saves. Original waveform playback stays independent.

Terminal application shutdown publishes `data-engine=stopped`; subsequent renderer
status callbacks cannot restore ready state. Inspector polling stops, and the native
terrain probe independently rejects stopped applications before touching scene data.
The environmental-audio smoke observes a post-shutdown window to catch late callbacks.

Build `audio_tests` and run CTest `audio` for portable contracts. With the pinned
local Emscripten **6.0.11**, build `webengine web_assets audio_wasm_tests`; run the
Wasm test with Node. `web_assets` stages matched host/main/worker artifacts and
the short MP3 preview. Run `scripts/environment-audio-smoke.cjs` serially at
DEVICE_SCALE_FACTOR1/2 on WebGPU and WebGL2; it measures actual output spectra,
room tails, mute/stop silence, activation and HTTP range streaming of a virtual
twenty-minute track, seeking, suspension, cancellation, corrupt media and blob cleanup.
Run existing audio-system/audio waveform/driving-audio suites for affected integration.
These are browser sample tests, not physical-speaker or physical Android/iOS,
Safari, streaming service, native output or sustained performance acceptance.
See [recorded validation](environmental-audio-validation.md).

## Native Windows adapter

DesktopAudioBackend now implements the same optional filter, reverb and streaming
contracts using native DSP and bounded local-file Media Foundation workers. Its
URI/codec policy and room algorithm differ from the browser. Read the exact
[Windows audio contract](windows-audio-environment.md) and
[separate validation](windows-parity-validation.md); browser evidence does not
establish native acceptance.
