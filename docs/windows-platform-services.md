# Native Windows platform services

Optional `engine_platform_windows` / `WebEngine::Platform` provides public
`engine/desktop_services.hpp` and `engine/audio_desktop.hpp`. It publicly links
core; SDL3 and Win32 types stay private. Linking `WebEngine::Desktop` also supplies
it. Files/audio compose without a host, GUI, renderer or scene. Browser adapters
remain independent over the same resource, audio and archive contracts.

## Asset and user directories

```cpp
engine::DesktopFileSettings settings;
settings.organization = "MyStudio";
settings.application = "MyGame";
engine::DesktopFiles files(settings);
app.assets().registerAsset<engine::Mesh>(engine::AssetId("car@1"),
    files.assetLoader<engine::Mesh>("models/car.glb", [](auto bytes) {
        return engine::GlbImporter::loadMemory(bytes);
    }));
auto request = app.assets().load<engine::Mesh>(engine::AssetId("car@1"));
```

The asset directory defaults to `assets` beside the executable, independently of
process working directory. `assetDirectory` overrides it explicitly. Organization
and application identities accept 1–128 ASCII letters/digits/`-`/`_`.
`SDL_GetPrefPath` chooses the user location (normally Windows roaming AppData);
`saves` and `settings` are separate subdirectories. `userDirectory` overrides that
root for tests or an explicit portable profile. Normal games need not write their
installation folder. Constructor/IO failures propagate for application recovery UI.

`assetPath` and `userPath(DesktopStorage::{Saves,Settings}, relative)` reject
absolute/traversal paths, Windows device/alias names, alternate streams, invalid
characters and resolved directory escapes. Unicode filenames work. These guards
prevent application path mistakes; they assume trusted directory ownership and
are not a sandbox against concurrent filesystem/reparse-point manipulation.

`assetLoader<T>` copies configuration and decoder, retaining no Files/scene borrow.
IO/decode runs synchronously during AssetManager::update, reports source bytes and
publishes an owning immutable resource; the decoder's byte span ends with its call.
Existing request sharing, cancellation, failure/
retry, caching and publication stay authoritative. Large IO/decode blocks that
update. The optional [DesktopAssetExecutor](windows-background-assets.md) adds
explicitly owned bounded background IO/decode, progress, timeout and cancellation
using the same AssetManager contracts. It is not an implicit pool inside
DesktopFiles; hot reload, package mounts and GPU prewarming remain separate.

## Save/settings bytes and existing archives

```cpp
auto bytes = engine::ApplicationArchive::save(schema, applicationState);
files.writeUser(engine::DesktopStorage::Saves, "slot-1.weapp", bytes);
auto candidate = engine::ApplicationArchive::load(
    files.readUser(engine::DesktopStorage::Saves, "slot-1.weapp"), schema);
// Publish validated candidate state at an idle boundary; rebuild game bindings.
```

`readAsset`, `readUser`, `writeUser`, `readDesktopFile` and
`writeDesktopFileAtomic` handle bytes, with a default 512 MiB positive per-call
budget that callers can lower. Oversized/partial reads do not publish resources.
Writes validate budget, create parents, reserve a unique sibling temporary file,
write in 1 MiB chunks, flush/close and replace via MoveFileEx. Failure removes the
temporary and preserves the old destination. Concurrent writers use last successful
replacement; no cloud conflict resolution or cross-process locking is supplied.

Storage adds no archive schema: use ApplicationArchive for versioned application
state and SceneArchive for scenes/assets. CRC, budgets, migration, staged decode
and unsupported-component rejection remain unchanged. Explicit paths returned by
dialogs use `readDesktopFile`/`writeDesktopFileAtomic` without root containment.
Applications validate decoded candidates and choose checkpoint/autosave policy.

## Audio playback

```cpp
engine::AudioSystem audio(std::make_unique<engine::DesktopAudioBackend>());
auto sound = engine::decodeDesktopWav(files.readAsset("audio/preview.wav", 32*1024*1024));
audio.backend().activate();
auto voice = audio.play(sound); // Existing bus/loop/rate/spatial/voice contracts.
audio.setBusVolume(audio.masterBus(), .5f);
audio.update(presentationDelta, currentScene); // Application chooses its clock.
audio.backend().suspend(application.suspended());
```

Construction never opens a device. `activate()` lazily opens default SDL output,
reports failure and permits explicit retry; pending voices retain their cursors.
Controls/diagnostics belong to the owner thread. The SDL callback mixes private
shared immutable sample buffers under a short state lock, never scene/GUI/user
callbacks. Destruction stops callbacks before releasing buffers/device/subsystem.

Output is 48 kHz float stereo with SDL device conversion. Native budget: 256 voices
(AudioSystem defaults to 128). PCM offsets, loops, completion, gain/mute, linear
resampling and rates .125–8 reuse AudioSystem's independent voices, buses/fades and
emitter/listener handles. Spatial output uses equal-power stereo pan and inverse
distance attenuation; multichannel input averages to mono. It is not HRTF.
The device clock advances independently of simulation/render cadence. Suspension
freezes playback; mute keeps cursors advancing. Games own suspension/update policy.
The vehicle suspends on focus/minimize and around dialogs, allowing sound preview
while simulation is paused in menus.

`DesktopAudioBackend::diagnostics()` exposes ready/active, mixed frames, native
voice entries, peak mixed signal and initialization error. Completed entries leave
on AudioSystem::update/stop. Device callback evidence does not prove speakers/
headphones or subjective quality. Runtime voices/devices have no new archive codec.

`decodeDesktopWav` validates chunk/sample/allocation bounds before SDL decoding.
Supported RIFF WAV: PCM 8/16/24/32-bit and IEEE-float 32-bit. Encoded limit: 32 MiB.
Existing SoundAsset limits: 8–192 kHz, 1–8 channels, 15 minutes, 128 MiB PCM, finite
[-1,1] samples. The in-memory decoder remains PCM/float WAV only. Native encoded streams now
use bounded Media Foundation workers, and native low-pass/room DSP implements
the optional capabilities; see [environmental audio](windows-audio-environment.md).
HRTF, device selection/hotplug recovery and sample-accurate automation remain
extensions.

## Native open/save dialogs

```cpp
engine::DesktopFileDialogOptions options;
options.title = "Open model";
options.filters = {{"GLB models", "glb"}, {"All files", "*"}};
options.defaultExtension = "glb";
auto result = host.showFileDialog(options);
if (result.status == engine::DesktopDialogStatus::Selected) openValidated(result.path);
```

The Unicode Win32 picker is owned by the host window. `kind` selects Open/Save;
filters use semicolon-separated bare extensions (`glb;gltf`) or `*`. Initial paths
accept folders/files; save supports default extension and overwrite confirmation.
Open requires an existing file. Results distinguish Selected, Cancelled and Failed;
only Selected has a path. Cancelling does not replace content or write files.

The call blocks on the host owner at idle or in a GUI event callback, and rejects
inside Application frame callbacks. Full options validation precedes input/clock
changes. The host cancels held/captured input, suspends Application for the modal
dialog and restores prior suspension while preserving explicit pause. Timing
baselines reset, preventing dialog-time simulation catch-up. Frame/render callbacks
do not run during the dialog. No asynchronous user callbacks/lifetimes escape it.
Games suspend audio explicitly. Async/multi-file/folder dialogs remain future work.

## Vehicle example

The example loads font and preview WAV from assets. Its menu adds volume,
Play/Stop sound, Open WAV sound, Open model, Save/Load checkpoint and native Save
as/Open checkpoint. Committed driver name, render/fullscreen options and volume
use `settings/settings.weapp`, with a short debounce and flush on Resume/Quit/
window close. IME preedit is excluded. Settings restore on launch; corrupt archives
retain valid session state and expose a status.

Save checkpoint writes `saves/progress.weapp` using ApplicationArchive and the
example's versioned payload in `examples/windows_vehicle_state.hpp`. Loading
validates before moving the existing car to its saved pose **at rest** and resetting
the camera. The example accepts state-only archives up to 64 KiB with a 1 MiB
decode allocation budget. The default slot restores automatically on launch. Commands, solver
history and audio voices are not checkpoints. Save as exports independently and
does not change the default slot. `--assets` and `--user-dir` select explicit roots.

Build: `scripts/build-windows.ps1 -BuildDir build/windows-services`. Focused CTest:
desktop_services, desktop_persistence_write/read, desktop_demo_write/read and the
existing desktop_api/text, audio/persistence/input/gui/application suites.
`desktop_dialog_tests` is opt-in: real dialogs briefly appear and the test accepts/
cancels only its own fixture dialogs. See [platform acceptance](windows-platform-validation.md).
