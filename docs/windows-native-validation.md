# Windows native validation — 2026-10-21

## Native platform services

Optional WebEngine::Platform now supplies SDL audio playback, rooted asset loaders,
user save/settings directories and atomic archive-byte storage. DesktopHost adds
Unicode native open/save dialogs. The vehicle example persists settings/checkpoints
and exposes audio/content/file controls. See [platform contracts](windows-platform-services.md)
and [current service acceptance](windows-platform-validation.md): 12 focused native
suites, actual native dialogs, cross-process restore, both vehicle/GLB scenarios
and shared browser audio/vehicle on WebGPU/WebGL2 passed.

## Interactive desktop GUI (prior milestone)

The current extension adds native single-line TextInput/selection, clipboard and
SDL IME preedit/commit ownership, session-local driver-name settings, controller
Back and modal navigation while held driving controls await neutral. See the
[GUI profile, issue ledger, commands and evidence](windows-desktop-gui-validation.md).
Five focused native suites, both native vehicle/GLB menu scenarios and shared GUI
and vehicle browser acceptance on WebGPU/WebGL2 passed. Physical controllers and
installed IME candidate UI remain separate acceptance.

## Reusable desktop host and interactive menu (prior milestone)

This milestone adds public `engine/desktop.hpp` and `engine_desktop` /
`WebEngine::Desktop`. The vehicle entry point no longer includes SDL or private
rendering headers. SDK dependencies/private include paths do not propagate to
public API consumers. The test consumer compiles with only the public target.
See [API and lifetime contracts](windows-desktop.md).

MSVC x64 Release build: `build/windows-desktop/Release/webengine_windows.exe`.
The same pinned SDL/Dawn/DXC dependencies as the initial proof below were used.

Verified for this milestone:

- **4/4 CTest passed:** desktop_api, application, input and gui. The native API
  suite checks concurrent-host rejection, invalid/foreign view handles and invalid
  quality preservation, scene transition/unload, deleted cameras, pause/resume,
  callback exception/reentry, close from a frame callback, scene release and
  pending framebuffer capture cancellation.
- **Native vehicle/menu smoke passed**, both with and without the existing
  `assets/formula1-red-driving.glb` file. Each uses the public host and the same
  example, synthetic SDL input and actual hidden-window Dawn D3D12 framebuffers.
  Each final scenario presents 375 frames and verifies contact/driving/steering,
  braking, resize, menu input cancellation, mouse checkboxes, effective 4x MSAA
  when supported, keyboard resolution adjustment, frozen simulation, keyboard
  Resume and Quit, menu Reset, wheel scrolling at 640x360, visible fixed actions,
  slider capture cancellation on focus loss, explicit pause retention, reset,
  failed model retention and safe shutdown. The render scale is restored before
  comparing the reset image. Actual menu and small-window screenshots were
  inspected in addition to pixel checks.
- **Browser vehicle acceptance passed on WebGPU and WebGL2**, Chrome
  155.0.8059.85 / DPR1, with no captured page/console errors. Built from current
  source with local Emscripten 6.0.11 into `build/web-windows-proof`; generated
  site then current web adapters were staged into the separate
  `build/windows-desktop-browser-site`. The user's dist was not replaced.
- Installed engine-skill and the CatGame copy now document DesktopHost, input
  ownership, view handles, menu callbacks, build commands, test boundaries and
  native limitations. Their Windows references match and CatGame's pin guidance
  is retained. Documentation links and whitespace checks passed.

Ignored evidence for that prior milestone:

```text
build/windows-desktop-final-build.log
build/windows-desktop-tests.log
build/evidence/windows-desktop/{verified.json,binary-hashes.json,*.png,*.ppm}
build/evidence/windows-desktop-glb/{verified.json,binary-hashes.json,*.png,*.ppm}
build/evidence/windows-desktop-browser/results.json
```

Frozen browser modules for this run (SHA256):

```text
webengine.wasm    107e081c3abac426282fb06b04c3fec0e30d3ba56f2dfc410d8f67128f978ff7
asset_decode.wasm c29f77dac3ce3a21ecdea8eccb0a516ca84f7a6013465bdb3cf403418f554543
```

Physical controllers, native monitor/fullscreen/DPI transitions, native text/IME/
clipboard/accessibility, native audio/editor UI, broad driver acceptance and
shipping distribution remain outside this verification. Shared engine/rendering
source is preserved; this does not establish every rendering effect on Windows.

## Initial vehicle proof (historical)

The shared checkout contained concurrent work; no reset/clean was performed.
The Windows executable and isolated browser artifacts below were built from the
working tree. This is a scoped developer proof, not shipping or full-engine
native acceptance.

## Native build and execution

- Windows x64 Release, Visual Studio 18 / MSVC, Windows SDK 10.0.26100.0,
  CMake 4.3.3; SDL 3.4.18, Dawn v20261016.190333, Microsoft DXC v1.9.2609.
- `scripts/setup-windows.ps1` verified the pinned official dependency archives.
  `scripts/build-windows.ps1` / CMake built `webengine_windows` successfully.
  All native targets and Jolt use compatible dynamic MSVC runtime linkage.
- `scripts/windows-smoke.ps1` passed using a hidden real SDL/Win32 window and
  Dawn D3D12; 420 frames were presented. The vehicle reached 8.93091 m/s,
  steering reached 0.5 radians, and a two-second brake interval reduced speed
  from 8.84773 to 2.05125 m/s. Wheel contact, 1280x720 to 960x540 resize,
  focus-loss cancellation/suspension, pause, reset and missing-file import
  preservation passed. Framebuffer checks exclude the HUD when requiring
  visible scene content and compare the different poses before/after reset.
- The same smoke with `--model assets/formula1-red-driving.glb` passed, loading
  the existing asset through the native GLB importer. Actual GPU captures were
  inspected: the imported red car appears beside the shared procedural vehicle
  course, and the C++ HUD and course remain visible.
- Focused MSVC Release CTest: **7/7 passed** — `application`, `input`, `vehicle`,
  `camera_follow`, `render_frame`, `gui`, `glb_importer`. These suites use active
  runtime checks, not disabled Release assertions. The vehicle/camera tests and
  browser suite had stale archive v31 assertions; they now check the current v34
  writer without weakening their roundtrip/behavior assertions.

Local ignored evidence:

- `build/evidence/windows-native/verified.json`, `binary-hashes.json`,
  `driving.png`, `resized.png`, `reset.png` and their original PPM readbacks.
- `build/evidence/windows-native-glb/` contains the equivalent GLB run.
- `build/windows-core-tests.log`, `build/windows-final-build.log` and
  `build/windows-smoke*.log` retain build/run outputs.

## Browser regression

Built with repository-local Emscripten **6.0.11**, Release, in
`build/web-windows-proof`. Its generated site was copied first and current `web/`
adapters last into `build/windows-proof-browser-site`, with matching main and
decode-worker JS/Wasm and fixture/font assets. The user's `dist` was not replaced.

`scripts/vehicle-smoke.cjs` passed on **WebGPU and WebGL2**, Chrome
155.0.8059.85, DPR1. It verified actual framebuffer changes, contact/driving/
braking/reverse/steering, simulation invariance under split views/masks/MSAA,
scene/progress restore, resize/device recovery, editor tuning/history, isolated
Play driving, save/reopen, invalid edits, component/preset undo/redo and blur
cancellation. There were **no captured page or console errors**. Evidence and
screenshots are in `build/evidence/windows-browser-regression/results.json`.

SHA256 of the frozen browser modules used for this run:

```text
webengine.wasm    240AB6478488423EAD5779B4B24CA24150842E1745B5281E29B0FF57B4A2FC01
asset_decode.wasm C29F77DAC3CE3A21ECDEA8ECCB0A516CA84F7A6013465BDB3CF403418F554543
```

## Documentation and limits

Updated README, architecture, Windows build/usage contracts, installed engine-skill
and CatGame's engine-skill (`SKILL.md` plus Windows/build/contract/integration
references). The new Windows reference copies match; CatGame's pin/dependency
guidance is retained. No game submodule pin was changed.

Native input evidence uses synthetic SDL keyboard/window events; no physical
gamepad or visible monitor/fullscreen/DPI transition acceptance is claimed.
Native GUI text/IME/accessibility, audio, editor UI, installer/signing/distribution,
and broad GPU/driver testing are outside this proof. Shared source does not imply
that every rendering effect has been separately accepted on native Dawn. No
performance comparison or mobile acceptance follows from these checks.
