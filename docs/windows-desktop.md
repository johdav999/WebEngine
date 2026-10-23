# Reusable Windows desktop host

Optional `engine_desktop` (`WebEngine::Desktop`) supplies `engine/desktop.hpp`.
It owns an SDL3 window/event dispatcher, input adapters, retained GUI, Application
and a selected private graphics backend. Dawn shares browser WebGPU/WGSL; direct
D3D12 provides [vehicle, GUI, conventional/layered terrain PBR/lighting, skin/morph, water/underwater, FX and sky/fog/HDR rendering](windows-d3d12.md).
`DesktopSettings::backend` defaults to `DesktopBackend::Automatic`, trying Dawn
D3D12, Vulkan and D3D11 in order. Explicit selections remain strict; direct D3D12
is independently selectable. `backend()` reports the current candidate, while
diagnostics expose readiness and all attempts. See [fallback and recovery](windows-graphics-fallback.md). Public headers contain no
SDL, HWND, Dawn or D3D12 types. Link the public target; consumers need no private source
or SDK include directories. Windows x64 MSVC Release is currently supported.

## Minimal application

Water materials use the shared `MaterialShader::Water`/`WaterSettings` contract
on Dawn and direct D3D12. Call `setWaterInteractions(span)` after scene activation
or from a frame callback to publish up to 16 copied contacts/wakes. Use
`application().clocks().simulation.seconds` for their `startTime`; the host supplies
that same simulation clock to water while GUI presentation continues independently.
Invalid input retains the previous snapshot. View edits/recovery retain contacts;
successful scene handoff clears them. Applications own contact detection/emission.
See [water](water.md) and [native scope](windows-d3d12.md).

```cpp
#include "engine/desktop.hpp"

int main() {
    engine::DesktopSettings settings;
    settings.title = "My game";
    engine::DesktopHost host(settings);
    auto scene = std::make_unique<engine::Scene>("Level");
    auto& camera = scene->spawn("Camera").addComponent<engine::CameraComponent>();
    camera.transform.position = {0, 2, -8};
    const auto sceneId = host.application().loadScene(std::move(scene));
    const engine::DesktopView view{camera.id()};
    host.setViews(sceneId, {&view, 1});
    host.application().start(sceneId);
    return host.run();
}
```

Link `target_link_libraries(my_game PRIVATE WebEngine::Desktop)` in an engine
build configured with `WEBENGINE_BUILD_WINDOWS=ON`. The public API test consumer
demonstrates this without linking SDL or Dawn directly. The desktop implementation
handles SDL main initialization; application entry points use ordinary C++ `main`.

Build/setup commands and pinned runtime dependencies are in
[the Windows guide](windows-native.md). `scripts/build-windows.ps1 -BuildDir
build/windows-desktop` provides an independent build directory. The equivalent
smoke command takes `-BuildDir build/windows-desktop` too.

## Ownership and frame execution

- One DesktopHost owns the process's SDL event queue and one window. All host,
  Application, GUI and rendering operations occur on its creation thread.
  Concurrent hosts reject before acquiring SDL subsystems. Multiple windows and
  integration with an externally owned SDL event dispatcher are not delivered.
- The host owns Application and its scene-binding slot. Use `application()` for
  loading, preparation, start, transitions, pause and shutdown; do not replace its
  scene-binding callback. `setViews(SceneId, span<DesktopView>)` copies validated
  camera handles/composition settings. Unknown/foreign scenes, invalid cameras and
  malformed view/quality candidates reject before replacing valid state.
- Scene binding resolves handles before activation. Transitions rebind before
  outgoing scene destruction; shutdown releases the renderer before scenes.
  A camera removed during simulation is omitted on the next presentation instead
  of dereferencing an expired pointer. Register views before entering a scene.
- `step(callbacks, optionalTimestamp)` polls SDL events, services graphics work,
  calls Application::advance once and renders after `callbacks.present`. Callers
  supply gameplay input/simulation/camera policy through the existing
  ApplicationFrameCallbacks. Optional timestamps are monotonic seconds for tools
  and deterministic tests; ordinary `run(callbacks)` uses a monotonic host clock.
  Do not advance Application/scene/physics again from these callbacks.
- Step is non-reentrant. Exceptions from user callbacks unwind host guards and
  propagate to the caller. Changing scene view configuration requires an idle
  Application boundary; rendering settings can change before rendering a frame.
  Dawn graphics acquisition is asynchronous; diagnostics().ready reports it.
  Initialization failures throw from step with the graphics diagnostic.
- `requestClose()` is safe inside GUI/frame callbacks. The host completes the
  current safe boundary, cancels input and calls Application::shutdown before
  returning false. Destruction clears event callbacks before releasing runtime
  owners. Callback captures must remain valid while installed/used; application
  code owns frame and GUI callback capture lifetimes.

## Input and GUI

`bindings()` returns the existing portable InputBindings. Bind stable
`controlId(DesktopControl::W)` etc. to application-defined actions and install the
usual context stack. There are no default gameplay bindings. Keyboard letters,
arrows, Space, Shift, Escape, Enter, Tab, F11, mouse buttons/wheel and one gamepad
map through public engine control IDs. Unconsumed mouse records include logical
and render-pixel positions in Application's input snapshot. Wheel bindings should
use the displacement flag. Gamepad sticks have a 15% dead zone, triggers 5%; games
own forward/reverse/brake meanings. Reconnection/control cancellation requires
neutral axes and released action buttons before driving resumes.

`gui()` owns the existing retained widget tree and font. The host lays it out in
window logical coordinates, forwards mouse capture/drag, wheel and supported GUI
keys before gameplay mappings, and renders it through the shared GUI renderer.
Tab/Shift+Tab, Enter/Space, arrows and Escape use GuiSystem's existing behavior.
Gamepad D-pad up/down traverses focus, left/right adjusts controls, and south/east
activate/cancel. Modal controller navigation remains available while held driving
axes wait for neutral; Start can always open the application menu. GUI-consumed
actions cancel held gameplay sources; a GUI-owned
mouse drag cannot leak into scene pointer records.

`setEventHandler` receives copied portable control, file-drop, focus, resize,
cancellation and close events. Returning true consumes a control or vetoes a
close request; `guiConsumed` records GUI ownership. Escape/P/F11 behavior is
application policy. `cancelInput`, `setPaused`, `setInputEnabled`, scene boundaries,
focus loss and minimize cancel the relevant input ownership. The synchronous
InputCancelled event lets applications clear held component commands immediately.
Focus/minimize also suspends all Application clocks, cancels GUI capture/focus,
and preserves explicit pause on restore. Input cannot accumulate hidden-time work.

### Native text input

Focusing an existing `TextInput` automatically starts SDL OS text input; blur,
hidden/clipped controls, focus loss, minimize and cancellation stop it. No public
SDL API or second widget system is needed. Applications load the shared GUI font
and create/focus widgets normally. `onChange` and `onSubmit` use existing contracts.
Typing is consumed before application driving shortcuts; event handlers must
respect `guiConsumed`. Text focus also cancels held bindings and gates gameplay
axes, including when focus is acquired programmatically without pausing.
Tab/Shift+Tab moves focus and Enter submits. Escape cancels
an active IME preedit first; a subsequent Escape follows normal GUI/menu policy.

The private adapter accepts UTF-8 text commits, mouse caret/selection, Shift with
Left/Right/Home/End, Backspace/Delete, Ctrl+A/C/X/V, Ctrl+Left/Right and Ctrl with
Backspace/Delete. Selection offsets remain UTF-8 bytes; movement/deletion uses
codepoint boundaries. Word movement uses ASCII whitespace. Clipboard/preedit
payloads and resulting native edits are bounded to 32 KiB; malformed/oversized
input preserves the previous value. Pasted CRLF, CR, LF and Tab become spaces.
AltGr text comes through SDL text events rather than guessed keyboard characters.

SDL composition offsets convert from characters to UTF-8 bytes. Repeated preedit
replaces the same saved selection, commit replaces it once, and cancellation
restores committed text/selection. Programmatic changes to text or selection win
over pending preedit. Switching/losing focus discards queued text for the old
widget. Composition can temporarily invoke `onChange`; inspect `composing()`
before publishing committed application values. The candidate area follows the
textbox/caret in logical window coordinates. OS services stop before window/GUI
destruction, without calling application widget hooks during teardown.

This is single-line editing, without undo/redo, grapheme segmentation, shaping,
bidi or multiline editing. Native Windows accessibility is provided through
[UI Automation](accessibility-input.md). Automated SDL preedit
tests establish adapter behavior, not acceptance of a physical keyboard layout,
installed IME candidate UI or every Windows input method.

## Presentation and diagnostics

`setQuality` validates a complete RenderQuality candidate and preserves the
existing shared resolution/MSAA/post/LOD contracts. `setShadowsEnabled`,
`setFullscreen`, `setWindowSize`, `size` and `capabilities` provide presentation
control. These never alter physics state. `diagnostics` returns readiness, actual
render dimensions, effective MSAA, frame count and existing render/performance
reports. Render scale affects framebuffer size; input/GUI use logical coordinates.
`retryGraphics` requests the existing bounded recovery mechanism after a graphics
failure. Desktop performance submission timing includes native presentation.

Acceptance tools may opt in through `DesktopSettings::allowGraphicsFaultInjection`
(default false) and call `simulateGraphicsLoss()` at an idle boundary. Disabled
or reentrant calls throw `logic_error`; a renderer that is not ready returns false.
The call cancels input and destroys the actual Dawn device. The existing lost
callback reacquires a device and rebuilds GPU resources from shared CPU assets.
This is an induced device loss, not a Windows driver reset/TDR. Native renderState
JSON includes the selected adapter's vendor/device/description, IDs and software
flag. See [the acceptance runner](windows-acceptance.md).

`size()` returns logical content units, with display scale included in pixelRatio.
The render extent remains subject to RenderQuality resolution limits. Initial
width/height and `setWindowSize` use SDL window coordinates (physical pixels on
Windows). Mouse/wheel positions are converted to logical GUI units, then to
backing pixels for scene pointers. Native IME rectangles convert back to window
coordinates. Display-scale events relayout without advancing simulation.

Native power suspend/resume notifications cancel input and reset frame timing at
an idle boundary, preserving explicit pause. Resume waits for focus and restore
when necessary. The window hook owns no application callbacks and is removed
before the host destroys its window.

`captureFrame(path)` requests one asynchronous PPM readback; poll capturePending
and captureError while stepping. The callback owns its readback buffer/data and
does not borrow application objects. Shutdown cancels pending capture and exposes
its cancellation through captureError. `baseDirectory` resolves bundled files.
Capture/diagnostic APIs are suitable for acceptance tooling, not a claim of a
complete resource/loading/file service.

## Vehicle pause/settings example

`examples/windows_vehicle.cpp` is a public API entry point.
`windows_vehicle_app.hpp` owns course creation, commands, chase-camera policy,
model preview and menu callbacks. It shares `src/vehicle_example.hpp` with the
browser. Click Pause/settings or press P/Escape/Pad Start. The modal menu pauses
simulation, cancels held driving input and exposes shadows, 4x MSAA, resolution,
fullscreen, defaults and a persisted driver-name textbox. Driver names update
the HUD after committed edits; Enter returns focus to Resume. P and R typed into
the textbox cannot trigger menu/reset shortcuts. D-pad chooses/adjusts controls,
A activates, B returns and Start toggles the menu. Resume, Reset car and Quit stay outside the scrolling
settings area. Settings apply during pause and survive Resume. Test coverage
includes 960x540 and 640x360 logical viewports. Escape closes the menu; Quit exits.

SDL/Dawn code and synthetic SDL event injection live in implementation/test files.
`webengine_windows_smoke` uses the same application example with the public host;
the interactive executable contains no test driver.

See [native platform services](windows-platform-services.md) for audio playback,
asset/user directories, atomic archive byte storage and showFileDialog. The example
now retains settings and an explicit checkpoint across launches; it also opens
models/WAV/checkpoints through native pickers. See [GUI acceptance](windows-desktop-gui-validation.md)
and [validation evidence](windows-native-validation.md). The separate
[native scene workbench](windows-editor.md) now provides ordinary scene editing.
Distribution is available through a separate [packaging workflow](windows-distribution.md).
Publisher signing, clean-VM sign-off and broad driver acceptance remain separate
release gates. Broader
rendering/window acceptance is tracked separately in [Windows acceptance](windows-acceptance.md).


Direct D3D12 now supports bounded device recovery, ordinary mesh instancing and
semantic BC7 uploads with RGBA fallback. See [recovery/performance contracts](windows-d3d12-recovery-performance.md)
and [milestone 8 evidence](windows-d3d12-milestone8-validation.md).
