# Windows environmental audio and music streaming

`DesktopAudioBackend` now reports `occlusionFilter`, `reverb` and `streaming`.
The existing application-owned AudioSystem APIs, voice/bus budgets, spatial poses,
fades, suspension and explicit update clock remain authoritative. PCM WAV decoding
stays separate from streamed encoded media.

```cpp
engine::AudioSystem audio(std::make_unique<engine::DesktopAudioBackend>());
audio.backend().activate();
audio.setReverb({.35f, 1.8f, .02f});
engine::AudioPlayOptions options;
options.loop = true;
options.reverbSend = .25f;
auto music = audio.playStream(engine::AudioStreamAsset::create(
    engine::desktopAudioStreamUri(L"C:/My Game/music.mp3")), options);
audio.seekStream(music, 30);
audio.update(presentationDelta, currentScene);
```

`desktopAudioStreamUri(path)` encodes an absolute local-drive path, including
spaces and Unicode, into a UTF-8 file URI. Native streaming accepts those file
URIs. HTTP/HTTPS/blob, UNC/network URIs and arbitrary URI schemes are unsupported;
fetch/cache remote music through an application-owned service before opening a
local file. Relative helper paths resolve explicitly against the current directory.
Use DesktopFiles paths when content must be independent of process CWD.

## Decode and mix boundaries

Each accepted stream owns one joined worker. It initializes COM and Media Foundation
on that worker, opens the local source and asks the system codec for 48 kHz stereo
float output. WAV/MP3 are validated by acceptance; AAC/M4A availability depends on
installed Windows codecs. Windows N requires the Media Feature Pack. Media Foundation
DLLs are system dependencies in the package audit; they are not bundled codecs.

Limits: eight streams per backend, shared 256 native voice limit, files at most
16 GiB, duration at most 24 hours, 32768 stereo frames (256 KiB) per ring and decoded
blocks at most 1 MiB. The OS codec's internal allocations are separate from these
engine buffer bounds. The worker waits when the ring is full. The SDL callback does
no file IO, codec work or per-block allocation. It mixes prepared buffers, spatial
pan/attenuation, low-pass filters and room sends. Short mutex sections protect
queues/controls; underruns output silence and do not advance source time. Linear
rate conversion retains the shared .125..8 speed/pitch semantics.

Start accepts a complete pending voice; decoding may fail later. playing includes
pending work. Diagnostics expose stream count, buffered/decoded frames, underruns
and errors. AudioSystem update removes failed/completed voices; the last asynchronous
error remains available after cleanup. Seek invalidates queued PCM with a generation
counter; old decode completions cannot republish it. Seeks before metadata are
pending; known out-of-range seeks reject without replacing current playback.
Looped initial offsets wrap after duration is known. Mute keeps clocks running;
suspension freezes device consumption. Stop removes the voice before joining its
worker, outside the mixer mutex. Shutdown stops the SDL callback before releasing
workers. Local codec/OS calls cannot be forcibly interrupted, so a stalled local
IO/codec can delay joined teardown; there is no hard cancellation deadline.

## Wall filtering and rooms

AudioSystem's existing bounded physics-ray occlusion supplies the same smoothed
obstruction gain and cutoff on both platforms. Opt in through spatial/occlusion
play options and AudioOcclusionSettings. Native output uses two cascaded one-pole
low-pass stages per voice. A dry 22 kHz cutoff bypasses filtering. Query budgets,
sensor exclusions, authoritative transforms and ownership are unchanged.

A prepared Schroeder-style stereo room uses four feedback delays per channel,
RT60-derived decay and explicit predelay. Wet is an additive post-gain/post-filter
send. Settings validate and allocate on the control thread before swapping under
the mixer lock; invalid rooms preserve the previous room. Master mute silences tails;
stopAll clears them. It is algorithmic reverb, not the browser's convolution impulse,
so identical settings do not promise identical samples. Neither adapter chooses
rooms or acoustic volumes automatically. HRTF, custom impulse assets, per-zone
room selection, device hotplug recovery and arbitrary DSP graphs remain extensions.

The Windows vehicle settings now provide Stream music, seek +10 seconds, Room
reverb and existing volume/Stop sound controls. Preview PCM also has a room send.
Music uses the same activation/suspension and cleanup policy as existing sounds.
See [validation](windows-parity-validation.md), [environmental APIs](environmental-audio.md)
and [native audio platform](windows-platform-services.md#audio-playback).
