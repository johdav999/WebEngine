# Native Windows desktop host and vehicle example

WebEngine has an optional public Windows x64 desktop host alongside its browser target.
Applications link `WebEngine::Desktop` and include `engine/desktop.hpp`; see
[public API and ownership contracts](windows-desktop.md).
It compiles the existing portable engine to native C++ and uses SDL3 windows/input
and Dawn WebGPU over Direct3D 12. It does not embed a browser.

## Build and run

Install Visual Studio 2026 C++ desktop tools (including a Windows SDK), CMake
with support for your Visual Studio generator (verified with 4.3.3), and Python 3.12+.
The default script uses CMake's `Visual Studio 18 2026` generator; VS2022 can be
selected explicitly if installed. The pinned Dawn package is Release x64 MSVC;
build this proof in Release, not Debug or MinGW.

```powershell
./scripts/setup-windows.ps1
./scripts/build-windows.ps1
./build/windows/Release/webengine_windows.exe
# Optional native GLB preview beside the course:
./build/windows/Release/webengine_windows.exe --model ./assets/formula1-red-driving.glb
# For a separate build while the previous app is open:
# ./scripts/build-windows.ps1 -BuildDir build/windows-desktop
# ./scripts/windows-smoke.ps1 -BuildDir build/windows-desktop
# For an installed VS2022 toolchain, use a fresh build/windows directory:
# ./scripts/build-windows.ps1 -Generator 'Visual Studio 17 2022'
```

Setup downloads official SDL 3.4.18, Dawn v20261016.190333 and Microsoft DXC
v1.9.2609 packages, verifies
their pinned SHA256 hashes, and extracts them under `build/dependencies/windows`.
Normal core and browser builds do not fetch or depend on these SDKs. CMake's
`WEBENGINE_BUILD_WINDOWS` defaults OFF. The browser remains on the repository-local
Emscripten version in `.emscripten-version`, with WebGPU and WebGL2.

Keep `SDL3.dll`, `dxcompiler.dll`, `dxil.dll`, `NotoSans-Regular.ttf` and bundled license notices beside the
executable when using the raw developer output. For a complete ZIP and per-user
installer, use [Windows distribution](windows-distribution.md).
Also retain the staged `assets/fonts` and `assets/audio` directories: the example
loads its font/preview WAV through DesktopFiles. `--assets` and `--user-dir` select
explicit content/profile roots; the normal profile uses the OS user directory.

## Controls and delivered scope

The vehicle HUD also exposes Record/Finish, Review, a replay scrubber, Play/pause,
slow motion, event navigation and Race ghost. Pause/settings includes native
`.weghost` Open/Save dialogs. Review freezes live physics; closing restores the
camera/visuals and prior pause policy. The same files work with compatible browser
driving scenes. See [replay workflow](replay-recording.md).

- WASD/arrows: throttle/reverse and steering; Space: service brake; either Shift:
  handbrake. R resets the chassis and camera; P/Escape opens/closes the pause
  menu; F11 toggles fullscreen. Use Quit or the window close button to exit.
- One SDL gamepad: left stick steering (15% dead zone), RT forward/LT reverse,
  south/A service brake, east/B handbrake. After focus loss/pause/disconnect,
  controls must return to neutral before gamepad driving resumes.
- Drop a GLB file into the window, or use `--model`, to show a normalized static
  preview next to the course. Import validates before publishing; failed drops
  retain the previous preview. This does not replace the vehicle physics/visuals.
- Resizing and display pixel density feed the shared renderer resolution budget.
  Minimize/focus loss cancels commands and suspends Application clocks. Restore
  preserves explicit pause and does not catch up elapsed hidden time.
- The vehicle course, Jolt simulation, wheel presentation, chase-camera rig,
  material/shadow rendering, and read-only C++ HUD use existing engine systems.

## Reusable host and interactive menu

`engine_desktop` owns Application, GUI, SDL lifetime/window/input and renderer
behind a public API. `examples/windows_vehicle.cpp` is an ordinary C++ entry point;
`windows_vehicle_app.hpp` owns driving/camera/content/menu policy and uses the
public host without SDL/Dawn or private renderer headers. DesktopView stores
camera handles; transitions rebind before outgoing scene destruction and removed
cameras are omitted safely. Application remains the sole frame/fixed scheduler.

Click Pause/settings or press P/Escape/Pad Start. The modal menu cancels driving
commands and pauses simulation while showing shadows, 4x MSAA, resolution scale,
fullscreen and restore-default controls. Settings apply while paused and survive
Resume. Tab/Shift+Tab changes focus, Enter/Space activates controls, arrows adjust
the slider, and wheel input scrolls settings. Resume, Reset car and Quit stay
fixed outside the scrolling settings area. Escape closes the menu.
The shared driver-name TextInput accepts Unicode text, selection, editing and
Ctrl+A/C/X/V clipboard shortcuts; Enter returns focus to Resume. Text owns keys
before driving shortcuts. Controller D-pad traverses/adjusts, A activates, B
returns and Start toggles the menu, including while driving triggers await neutral.

The browser and desktop targets retain the same core, WebGPU/WGSL and GUI render
implementation. SDL/Windows/Dawn types stay private. The public browser Renderer
facade retains WebGL2 fallback; native Dawn uses D3D12. See
[desktop API, lifetimes and limitations](windows-desktop.md).

## Validation

```powershell
./scripts/windows-smoke.ps1
```

The separate `webengine_windows_smoke` executable opens a hidden real SDL/Win32 window and renders with Dawn
D3D12. It uses deterministic timestamps through Application and synthetic SDL
keyboard/window events; it checks wheel contact, acceleration/steering, braking,
resize, suspension, pause, reset, failed-import preservation and the actual
checkbox/slider/menu input paths. It also checks keyboard Resume/Quit, small-window
scrolling, fixed actions and slider capture cancellation on blur. Asynchronous GPU
readback captures actual color frames. The wrapper checks image dimensions and
visible variation and writes PNG previews plus machine-readable evidence.
It also checks native text focus/typing and an SDL virtual controller's mapped
navigation, activation and held-trigger cancellation. `desktop_text_tests`
checks UTF-8 editing/selection, OS clipboard copy/cut/paste, SDL preedit/commit,
programmatic-edit precedence, stale queued text and focus/visibility cancellation.
The clipboard test temporarily replaces and restores clipboard text; run it with
normal desktop/clipboard access. CTest name: `desktop_text`.

`desktop_api_tests` compiles as a public-header consumer and covers validated
configuration, scene transition/unload, stale cameras, callback failure/reentry,
pause, shutdown and readback cancellation. Use the existing native `application`, `input`, `vehicle`, `camera_follow`,
`render_frame`, `gui` and `glb_importer` tests for shared logic. Compile with the
pinned local Emscripten SDK and run `scripts/vehicle-smoke.cjs` on its exact staged
site for both browser backends after changes to the shared renderer.

Synthetic SDL events and hidden-window GPU captures do not establish physical
controller, visible-window/monitor/fullscreen, installed IME candidate UI,
accessibility or broad Windows
driver acceptance. Browser/mobile validation remains separate. The host
now adds optional native audio, asset/user directories, save/settings bytes and
native open/save dialogs; see [platform services](windows-platform-services.md).
The [native scene workbench](windows-editor.md) is now available as a separate
developer executable; specialized browser tool panels and automatic publishing
remain extensions. Offline ZIP
distribution, per-user installation, diagnostics and separate symbols are now
available; see [distribution](windows-distribution.md) and its validation limits.
Text remains single-line/codepoint based, without
undo, shaping, bidi or grapheme navigation. Existing rendering code is
shared; the vehicle scene acceptance does not establish every renderer feature on
native Dawn. See [recorded checks](windows-native-validation.md).

The separate [broader acceptance runner](windows-acceptance.md) now tests native
animation, terrain, water/underwater, particles, lighting/shadows, HDR post and
MSAA with actual framebuffer comparisons. It also uses real resize, fullscreen
and minimize/restore operations and induced Dawn device loss. Its report records
which physical controller, mixed-DPI, sleep/wake and driver cases remain open.
