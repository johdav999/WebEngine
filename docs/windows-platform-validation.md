# Windows platform service acceptance — 2026-10-21

## Scope and reproducible commands

Windows x64 MSVC Release, pinned SDL3 3.4.18/Dawn/DXC, shared working checkout.
Unrelated concurrent cinematic changes were preserved. Services and the vehicle
example were built in `build/windows-services`; the previously running GUI build
was not overwritten. New public service headers require no SDL/Win32/Dawn headers.
The service-only test links WebEngine::Platform without a host/renderer.

```powershell
./scripts/build-windows.ps1 -BuildDir build/windows-services
ctest --test-dir build/windows-services -C Release -R '^(desktop_.*|audio|persistence|application|input|gui)$' --output-on-failure
./build/windows-services/Release/desktop_dialog_tests.exe
./scripts/windows-smoke.ps1 -BuildDir build/windows-services -EvidenceDir build/evidence/windows-platform
./scripts/windows-smoke.ps1 -BuildDir build/windows-services -EvidenceDir build/evidence/windows-platform-glb -Model assets/formula1-red-driving.glb
./build/windows-services/Release/webengine_windows.exe
```

The opt-in dialog test briefly displays native dialogs and automatically accepts/
cancels only fixture dialogs belonging to its own process. Audio and dialogs need
normal Windows desktop/device access. All save/settings tests use explicit roots
inside their build/evidence directories, isolating the user's game profile.

## Verified

- **12/12 focused CTest passed:** application, input, gui, audio, persistence,
  desktop_api, desktop_text, desktop_services, desktop_persistence_write/read,
  desktop_demo_write/read. The final demo pair was repeated after bounding its
  state decoder and excluding IME preedit from saved settings.
- **File/asset adapter:** rooted directory loading publishes through AssetManager,
  remains safe after its originating Files object is destroyed, shares requests,
  preserves another request when one cancels, and reports missing/corrupt files
  without replacing a valid resource. Oversized IO, traversal, alternate streams,
  reserved device names and trailing aliases reject. A locked destination forces
  atomic replacement failure; the previous checkpoint bytes survive.
- **Cross-launch persistence:** distinct processes write/read both settings and
  saves through ApplicationArchive. The vehicle demo restores Unicode driver name,
  volume, shadows/resolution and checkpoint pose. Corrupt CRC retains the live
  vehicle. A transient composing textbox value is excluded from persisted state.
  Vehicle checkpoint restores at rest; this is not a solver/runtime checkpoint.
- **Native audio:** bounded WAV decode produces SoundAsset, malformed WAV rejects,
  voices wait before activation, SDL output mixes a nonzero signal, one-shots
  complete on the device clock, independent loops survive rate/volume changes,
  suspension freezes the device cursor, resume advances it, mute retains voices,
  and stop-all releases native voice entries. This is real SDL callback/device
  evidence, not a physical speaker/headphone listening test.
- **Native dialogs:** actual Win32 open selects the Unicode fixture path, save
  adds `.weapp`, cancel returns no path, and suspension restores. Invalid filter
  options preserve focus/clocks before throwing. Initial paths normalize native
  separators; this fixes the observed FNERR_INVALIDFILENAME failure during UAT.
- **Vehicle/menu regression passed**, with and without the existing GLB preview:
  real hidden SDL/Dawn framebuffers, driving/braking/contact/reset, existing GUI
  mouse/wheel/text/keyboard/controller flows, settings, cancellation and shutdown.
  The platform-controls menu readback was inspected as well.
- **Browser audio:** 12 checks per backend passed on WebGPU and forced WebGL2.
  **Browser vehicle:** both backends passed. Chrome 155.0.8059.85, no captured page/
  console errors. Built current source with repository-local Emscripten 6.0.11;
  generated outputs plus current web adapters/fixtures were staged separately in
  `build/windows-platform-browser-site`, served on localhost:8187, suites serial.

## Evidence

```text
build/windows-services-{configure,final-build,tests,dialog-tests}.log
build/windows-services-demo-final-{build,tests}.log
build/windows-services/service-test-data/{user,demo-user,platform-menu.ppm}
build/windows-services/dialog-test-data/
build/evidence/windows-platform/{verified.json,binary-hashes.json,*.png,*.ppm}
build/evidence/windows-platform-glb/{verified.json,binary-hashes.json,*.png,*.ppm}
build/evidence/windows-platform-audio-{webgpu,webgl2}/results.json
build/evidence/windows-platform-vehicle-browser/results.json
build/evidence/windows-platform/platform-menu.png
```

Frozen staged module SHA256:

```text
webengine.wasm    f07df1d148e837c4b426fed04ddd2c62dc92bd0d3a5819f925ee5fa11bdc3647
asset_decode.wasm c29f77dac3ce3a21ecdea8eccb0a516ca84f7a6013465bdb3cf403418f554543
```

The build encountered low disk space. Only obsolete browser staging folders made
by this chat and the new build's duplicate downloaded Jolt archive were removed;
source, user settings and recorded evidence were retained. Rebuild/tests passed
after that recovery. Concurrent CMake/source edits briefly blocked generation and
browser compilation; their completed source was used, without reverting changes.

## Limits

PCM/float WAV and existing SoundAsset playback are covered. MP3/compressed WAV,
native streams/reverb/HRTF, output device selection/hotplug, physical output quality,
physical controllers/IME UI and broad driver/monitor/fullscreen/DPI acceptance are
separate work. File IO/decode and native modal dialogs are synchronous. Directories
assume trusted ownership; atomic replacement is local storage, not cloud conflict
resolution. No installer/updater, native editor or new audio persistence codec is
claimed. See [delivered service contracts](windows-platform-services.md).
