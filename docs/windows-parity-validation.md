# Windows audio/editor parity validation - 2026-10-21

The dirty shared checkout contained other work; it was preserved. This increment
adds native local-file streamed audio/filtering/reverb and an initial native scene
workbench, rather than claiming every browser authoring panel is now native.

## Windows MSVC/Dawn

Built Release using Visual Studio 18 2026 and the repository's pinned SDL/Dawn/DXC
packages. The normal sandbox could not read Windows SDK registry entries; the
native build/tests ran with approved SDK/device access. No SDK replacement or
renderer backend change was made.

- Focused CTest audio_dsp, desktop_audio_environment and desktop_services passed
  3/3 on the final audio build. Audio checks exercised a generated twenty-minute
  WAV without full-track decode, a real MP3, local Unicode/space file URIs,
  generation-checked seek with distinct before/after frequencies, end-to-start
  looping, rate, actual SDL device consumption, mute/suspension/resume, an
  eight-stream budget, invalid encoded failure retention and joined cancellation.
  A real physics wall fed AudioSystem occlusion into DesktopAudioBackend. Offline
  native DSP checks measured >20 dB high-frequency suppression, predelay,
  decaying reverb tails, clear and invalid-room rejection.
- desktop_editor_tests passed against real hidden native SDL/Dawn windows, including
  native SDL text/mouse events, retained GUI commands, transforms/history,
  rejected zero scale, duplicate, save/open and dirty/corrupt preservation,
  isolated Play, source restoration, resize and transaction cancellation before
  shutdown. Its final run also opened a scene edited and saved by the browser.
- Evidence: build/evidence/parity-windows-editor-final/report.json, editor.ppm,
  browser-exchange.ppm, PNG previews and pixels.json. Pixel inspection excluded
  UI and found 4723 and 3576 cyan mesh pixels in the actual 1440x900 / 1280x800
  readbacks. These are rendered geometry, not generated UI illustrations.
  tools/verify_windows_parity.py checks these readbacks.
- Existing scripts/windows-smoke.ps1 passed driving/input/menu and native framebuffer
  regression after audio/menu changes. Evidence: build/evidence/parity-vehicle-final/.
- Windows package tooling's four Python unit tests passed after adding Media
  Foundation DLLs to the explicit system-dependency list. No new installer/clean-VM
  or signing acceptance was performed.

Commands (after scripts/build-windows.ps1):

```powershell
ctest --test-dir build/windows -C Release -R '^(audio_dsp|desktop_audio_environment|desktop_services)$' --output-on-failure
./build/windows/Release/desktop_editor_tests.exe build/evidence/parity-windows-editor-final build/evidence/platform-parity-browser/webgpu-roundtrip.wescene
python tools/verify_windows_parity.py build/evidence/parity-windows-editor-final
./scripts/windows-smoke.ps1 -EvidenceDir build/evidence/parity-vehicle-final
python tests/test_windows_package.py
```

Use fresh evidence directories on subsequent runs. The browser roundtrip input
requires the scene-exchange fixture below first; desktop_editor_tests also runs
without that optional second argument.

## Browser/Wasm

Repository-local Emscripten 6.0.11 Release built webengine/web_assets and explicit
audio_wasm_tests/audio_dsp_wasm_tests. Both Node/Wasm test programs passed.
A sandbox temporary-directory permission failure was resolved by setting TEMP/TMP
to a workspace-owned build directory; no toolchain version was changed.

The matched site was frozen under build/parity-site and served separately from
concurrent dist updates. Chrome 155.0.8059.85 passed sequential WebGPU and forced
WebGL2 acceptance at DPR1:

- scripts/environment-audio-smoke.cjs: real Web Audio sample spectra, room tails,
  long range-stream seeking, native media elements, pending/corrupt cancellation,
  suspension, mute and blob lifetime. Report: build/evidence/parity-audio-browser/results.json.
- scripts/editor-smoke.cjs: ordinary editor/GUI, actual viewport/gizmos, history,
  invalid edits, downloads/reopen and isolated Play. Report:
  build/evidence/parity-editor-browser/results.json.
- scripts/platform-parity-smoke.cjs: open native-scene.wescene, preserve x=2.5,
  edit x=4, save from each browser backend. The WebGPU result was then opened
  in the native workbench above. Report:
  build/evidence/platform-parity-browser/results.json and renderer roundtrip files.

## Remaining limits

Native music is local-file Media Foundation streaming; HTTP/blob streams remain
browser-owned. The native room is algorithmic and the browser room is convolution,
so parameter parity is not sample identity. Physical speaker quality, installed
codec combinations, physical controllers/IME, mixed-DPI/fullscreen monitor setups,
clean Windows guests, signing, sustained performance and physical mobile acceptance
were not established by these tests. No direct D3D12-vs-Dawn performance comparison
was run. The native workbench's specialized-panel limits are listed in
[its contract](windows-editor.md); [audio bounds](windows-audio-environment.md)
remain explicit. Installed and CatGame engine-skill documentation were updated
without advancing any consumer engine pin.
