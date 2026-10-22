# Audio system

`engine::AudioSystem` is an optional, application-owned portable service in
`engine_core`. It owns its `AudioBackend`, buses and independent playback voices.
`engine_audio_browser` implements that backend through `web/audio-system.js` and
Web Audio. Scene construction alone never opens an audio device.
The optional Windows DesktopAudioBackend implements the same interface with lazy
SDL output and bounded PCM/float WAV decoding. AudioSystem buses/fades, scene
handles and update policy remain shared; see [native audio contracts](windows-platform-services.md#audio-playback).

```cpp
#include "engine/audio.hpp"
#include "engine/audio_browser.hpp" // Browser target only.

AudioSystem audio(std::make_unique<BrowserAudioBackend>());
auto effects = audio.createBus("Effects");
auto music = audio.createBus("Music");
// Interleaved finite [-1,1] PCM; immutable, shared by every voice.
auto sound = SoundAsset::create(48000, 1, samples);
AudioPlayOptions options;
options.bus = effects;
options.playbackRate = 1.5f; // Pitch and speed; default 1.
auto first = audio.play(sound, options);
auto second = audio.play(sound, options);
audio.setBusVolume(audio.masterBus(), 0.7f);
audio.setBusMuted(effects, true);
audio.setVoiceMuted(second, true);
audio.setVoicePlaybackRate(first, 0.75f);
audio.fadeVoice(first, 0.2f, 1.0);
audio.stop(first, 0.5); // Fade to silence, then release.
```

Voices retain shared `SoundAsset` ownership through completion/stop. Their
offset, loop flag, gain, mute and fade state are independent. Runtime voice and
bus handles are scoped to one system and never recycled. Foreign/stale voice
controls return false; malformed values throw before changing valid state.
`play` returns an empty handle if the backend cannot start or the voice budget
is full (default 128). Backend start is transactional. Destroying the system
stops all voices before releasing the backend/device.

Buses form an immutable parent tree rooted at master. Empty playback/parent
handles select master. Effective gain multiplies voice volume and every ancestor
bus volume; a mute anywhere on the path sets gain to zero without losing volume
or advancing/ending a voice. Volume and fade targets are finite numbers in [0,1].
The browser applies this composed gain per voice; these buses do not currently
offer arbitrary DSP insert chains. Optional shared room sends are described in
[environmental audio](environmental-audio.md). Buses persist for the system lifetime.

Fades are linear and interruptible. New volume/fade commands replace the current
fade, starting from the last published volume. New voice volume/fade commands
cancel a pending fade-stop. Zero-duration fades apply immediately. Applications
call `update(deltaSeconds, scene)` after scene/physics updates with their chosen
time domain. Fades advance only when the backend reports active audio. The
playground uses presentation time, so simulation pause keeps sound/fades running.
Fade gain is updated at host frame cadence; playback completion and looping use
the browser device clock. This is not sample-accurate fade scheduling.

## Playback rate

`AudioPlayOptions::playbackRate` defaults to 1. `setVoicePlaybackRate(voice, rate)`
changes a live or pending voice, and `voicePlaybackRate(voice)` returns its last
accepted value (0 for an unknown handle). Rates must be finite in [0.125,8]. Zero,
negative values, NaN and infinity throw before publication. Use suspension or stop
for pausing; reverse playback and independent pitch/time stretching are not provided.

The backend must implement `bool playbackRate(uint32_t,float) noexcept`. A false
result preserves the previous rate and voice; AudioSystem publishes only success.
Foreign/stale controls return false. Natural completion may precede AudioSystem's
next cleanup update: controls then return false when the backend no longer owns
that source. Starting at an unsupported rate follows transactional start failure.

Offsets always count **source seconds**, independently of rate. A constant-rate
one-shot takes `(duration - offsetSeconds) / playbackRate` active device seconds.
Loops wrap at the source buffer boundary. Changes preserve source identity and the
current cursor: the browser schedules `AudioParam.setValueAtTime` at the current
AudioContext time, taking effect at its next audio rendering quantum. This is an
immediate, unsmoothed pitch/speed change, not sample-accurate host automation.
Repeated updates do not recreate BufferSources or PCM buffers. Browser AudioParam
reads can retain their last rendered value while suspended; the accepted option
is retained and applies on activation/resume. Pending sources never consume their
offset before activation. Context suspension freezes the cursor/device clock;
mute only changes gain. Rate never changes simulation, fade time, or scene data.

The inspector's Playback rate slider changes its music preview loop and applies
to future emitter/loop starts. Existing one-shots retain their own rates.
See the [driving example](driving-audio.md) for a separate tuning document,
editor preview and telemetry mappings outside the engine.

## Scene emitters and listeners

```cpp
auto& emitter = actor.addComponent<AudioEmitterComponent>("Engine sound");
emitter.sound = sound;
auto& listener = cameraActor.addComponent<AudioListenerComponent>("Listener");
audio.bindListener(listener.id());
options.loop = true;
options.referenceDistance = 1;
options.maxDistance = 100;
options.rolloff = 1;
auto engineVoice = audio.playEmitter(emitter, options);
audio.update(presentationDelta, &scene);
```

The hierarchy owns components; audio retains only generation-checked
`ComponentId` handles. Emitters follow world positions derived from authoritative
scene transforms. Disable, detach, pending destruction, transfer, or switching the
scene passed to `update` stops bound voices on the next update. Pass null before
unloading a scene; the system retains no scene pointers. Stop/all or system
destruction also immediately release voices. Shared sound replacements affect
future plays, keeping currently playing assets valid.

One listener is selected explicitly. Its world -Z and +Y axes define forward/up;
validated orthonormal orientation strips transform scale. Missing/disabled or
foreign listeners reset to an origin listener; a malformed transform retains the
last valid pose. `setListenerPose` supplies a manual world pose and clears binding.
Direct spatial voices use `options.spatial` and `setVoicePosition`. Browser
spatialization uses HRTF and inverse distance attenuation with configurable
reference/max distance and rolloff. Optional physics-ray occlusion, room reverb
and streamed music are documented in [environmental audio](environmental-audio.md).
Emitters remain omnidirectional; cones, Doppler and audio-driven gameplay remain
future extensions.

## Activation, decoding and ownership

Call `audio.backend().activate()` synchronously from a click/key gesture. It
initiates `AudioContext.resume()`; check `backend().active()` on following frames.
Queued voices remain alive while browser activation is blocked and their device
clock is frozen. `suspend(true)` freezes browser audio on host suspension;
`suspend(false)` permits a new activation gesture. The playground calls this from
its visibility/application suspension boundary and shuts audio down before scenes.
Activation errors and context states are observable through the device snapshot
and the inspector. [Browser autoplay rules](https://developer.mozilla.org/en-US/docs/Web/Media/Guides/Autoplay)
and [resume behavior](https://developer.mozilla.org/en-US/docs/Web/API/AudioContext/resume)
remain platform responsibilities.

The adapter lazily owns one AudioContext and its output node. Each active voice
owns a BufferSource, GainNode and optional PannerNode, low-pass and room send.
Stream voices use a media element source instead of BufferSource. Simultaneous PCM voices sharing
one sound identity reuse one AudioBuffer; the last voice completion/stop evicts
that buffer. Future playback uploads it again from the retained shared PCM asset.
The adapter disconnects nodes and nulls completion handlers on explicit stop;
natural completion also releases its nodes. Disposal cancels activation effects,
stops voices and closes the owned context asynchronously.

`BrowserAudioBackend::deviceId()` identifies the adapter's JavaScript device.
`EngineAudioDevices.get(deviceId).decode(file, {signal})` decodes local MP3/WAV
using the browser and returns `{sampleRate, channels, samples}` with interleaved
PCM. Limits are 32 MB encoded, 15 minutes, 128 MB retained PCM, 1–8 channels and
8–192 kHz. PCM construction rejects nonfinite/out-of-range samples. Decode checks
cancellation/disposal after asynchronous boundaries; the caller owns publication
and must reject stale results. The inspector copies a validated candidate into
Wasm, constructs `SoundAsset`, then replaces its shared sound. Failed/stale loads
preserve the previous asset and current voices. Peak transient decoder memory can
exceed retained limits. Generic `AssetManager` callers may publish the same
`shared_ptr<const SoundAsset>`; there is no separate audio content registry.

## Example and persistence scope

The **Audio system** inspector exercises overlapping effects, a nonpositional
music loop, master/bus controls, a fade-stop and an X-positioned emitter with a
listener at the origin. Its application-only code is `src/audio_demo.cpp`, with a
private scene for its demonstration components. Audio controls do not insert
runtime nodes into the model being archived. The original MP3/WAV waveform
player and its archive payload continue independently.

Runtime voices/buses and PCM sounds are not built-in scene-archive resources.
Applications using emitters/listeners in saved scenes must register explicit
component codecs and sound asset resolution, recreating runtime playback on
load. Unknown components still fail saving rather than losing data silently.

## Verification

`tests/audio_tests.cpp` checks shared ownership, voice budgets and identities,
completion/failure cleanup, loops/offsets, gain composition, mutes, interrupted
fades, suspended fade clocks, invalid data, inherited positions, listener rotation,
disable/pending removal/reparent/transfer and scene-unload boundaries.

```powershell
cmake --build build/native --target audio_tests
ctest --test-dir build/native -R '^audio$' --output-on-failure
# With Playwright on NODE_PATH, a browser build and the local server:
node scripts/audio-system-smoke.cjs
node scripts/audio-smoke.cjs
```

The system smoke tests actual C++/Wasm voices and browser nodes: pre-activation queuing,
gesture activation, shared buffers, nonzero mixed samples, silence after mute,
bus gains, one-shot release, looping, HRTF channel balance, fade-stop, MP3/WAV
imports, corrupt replacement, suspension and shutdown. Evidence goes to
`build/evidence/audio-system/`. These sample-based checks do not establish
physical speaker output, physical Android/iOS acceptance or sustained mobile
performance. Portable tests use a deterministic backend; the optional Windows
adapter also has real SDL output, local encoded streams and native filtering/reverb.
See [Windows audio](windows-audio-environment.md) and its separate platform evidence.

See [recorded validation](audio-validation.md) for the checks actually run.
