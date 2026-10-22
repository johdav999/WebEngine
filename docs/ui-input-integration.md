# UI and input integration

The optional native Windows [DesktopHost](windows-desktop.md) uses these same
InputBindings/Application snapshots and GuiSystem widgets. Stable DesktopControl
IDs keep SDL details private. GUI input takes precedence over gameplay; menu,
scene, focus and shutdown boundaries cancel held sources and component commands
through synchronous InputCancelled events. See the desktop contracts for pointer
coordinates, capture cancellation, gamepad rearming and callback lifetimes.
The private desktop text service connects the same single-line TextInput to SDL
Unicode/IME events and Windows clipboard, with UTF-8 selection editing and focus
cancellation. Honor guiConsumed in application shortcut handlers. Native text
services are owned by DesktopHost; semantic DOM/text services remain browser-owned.

The engine exposes portable device-to-action bindings and retained GUI layout,
scrolling and accessibility data. Browser event, text-service and semantic DOM
integration stay in the optional adapters. Application code supplies control
IDs, action meanings, menus, labels and HUD content.

## Portable bindings

```cpp
InputSystem input;
InputBindings bindings(input); // input outlives bindings
const InputBinding map[] = {
    {1, 10, InputActions::Move, {0, 1}}, // Control 10: held forward
    {1, 11, InputActions::Move, {0, -1}},
    {1, 12, InputActions::Look, {1, 0}, true}, // Scalar displacement
    {2, 10, InputActions::Interact, {1, 0}},
};
bindings.setBindings(map);
const InputContext contexts[] = {{1, true}, {2, false}};
bindings.setContexts(contexts);
bindings.control(10, 1); // Top context owns this control
bindings.control(11, 1); // Transparent top context falls through
input.beginFrame();
```

Stacks are ordered bottom to top. A matching context owns the control and emits
all its matching bindings; `blockLower` stops lookup even without a match.
Held values clamp to [-1,1]; displacement accumulates once in the existing input
queue. Independent bindings retain independent sources and action edges.
Configuration is copied and validated before publication: IDs must be nonzero,
binding vectors finite within [-1,1], context IDs unique. Limits are 1024 bindings
and 64 contexts. Invalid updates preserve configuration and held state.

`setBindings`, `setContexts`, `cancel` and destruction release the mapper's
sources through `InputSystem`, without a global cancellation. Reserve its 1024
source IDs exclusively; the default range starts at `0x80000000`. Several
mappers must receive separate source ranges. The borrowed `InputSystem` must
outlive each mapper. Consume releases at the next frame. Existing queue-overflow
safety can still cancel all input. After a context transition, submit fresh
device state deliberately; the mapper does not reactivate old held values.
At host/lifecycle cancellation, cancel the device mapper as well as the snapshot.
Bindings and context stacks are runtime configuration, outside scene archives.
For the application-owned snapshot, pass an owned submission callback rather
than taking a mutable reference to the read-only snapshot:

```cpp
Application app; // outlives the mapper and its callback
InputBindings bindings([&app](std::span<const InputPacket> packets) {
    return app.submitInput(packets);
});
```

The sink must consume the span synchronously, must not throw, and must follow
the atomic rejection/cancellation contract of `InputSystem::submit`.
`Application::submitInput` supplies that contract and rejects input while
created, suspended or stopped. Paused applications still accept menu/camera
input. Host lifecycle changes reset the underlying snapshot; cancel the mapper
alongside that reset and submit fresh device state after resume.

## Browser contexts and virtual controls

Existing `bindKey`, `clearBindings` and `setContext` remain available.
`setContexts([{name,blockLower}, ...])` replaces a validated stack;
`pushContext(name,{blockLower})` returns an idempotent removal function.
Transitions cancel keys and gestures before activating the new stack.
The first context with a matching physical key/modifier owns that key. An opaque
context blocks lower mappings; a transparent overlay preserves unclaimed keys.
Configured menu keys work while scene gestures are suppressed. Scene gestures
use the highest reachable Camera or Gameplay context. GUI gets first refusal.

```js
const input = EngineInput.instance;
const pop = input.pushContext('PauseMenu'); // opaque
const unbind = input.bindKey('PauseMenu', 'Enter', 100);
// Closing menu:
unbind(); pop();

const stick = input.attachControl(document.querySelector('#move-stick'), {
  action: EngineInput.Actions.Move, context: 'Gameplay', kind: 'stick',
  deadZone: 0.18, radius: 0.4, invertX: false, invertY: true,
});
// Removing or replacing the application control:
stick.dispose();
```

Stick radius is a fraction of the element's smaller dimension, in (0,0.5].
Dead zone lies in [0,1); the remaining radial range rescales continuously to 1
and clamps to a circle. Default Y inversion makes upward gestures positive.
Buttons support a copied two-dimensional `value` within [-1,1]. Configuration
rejects invalid values before replacing an attached control. Reattaching an
element releases its old source and listeners. Disposal releases active contacts
without disturbing another control, and is idempotent. `BrowserInput.dispose()`
releases all owned controls/listeners and its Wasm buffer. Applications must
dispose a control before removing its HTML element. Use native, named buttons
and `touch-action: none` for accessible virtual controls.

## GUI composition and accessibility

For portable custom painting/input hooks, checkbox and horizontal slider
controls, see [custom GUI widgets](gui-widgets.md). Focused sliders consume their
value-navigation keys before scroll-panel navigation; ordinary handled presses
can still be cancelled by scrolling.

```cpp
GuiSystem gui;
gui.setScale(1.25f); // independent of device pixel ratio
auto& panel = gui.root().add<ScrollPanel>();
panel.setSize({300, 200});
panel.setPadding(12);
panel.setLayout(VerticalLayout{8});
panel.setAccessibleName("Settings");
auto& hint = panel.add<TextLabel>("A longer description that wraps to the available width.");
hint.setStretch(true);
hint.setWrap(true); // measured height participates in vertical flow
auto& action = panel.add<Button>("Apply settings");
action.setStretch(true);
action.setAccessibleDescription("Apply the current local settings.");
action.onClick(applySettings);
```

Use `WrapLayout{gap,rowGap}` for rows of fixed-size children. Hidden children
reserve no space. Stretch fills the parent's padded width/height, clamped by
optional size limits. Explicit sizes remain the default; opt-in content sizing
measures containers and controls. Alignment packs groups/cells without flex-weight
distribution; `GridLayout` provides equal columns. See [layout contracts](gui-layout.md).
Call `layout`
after changing geometry, text, font, visibility, scale or scroll offset.
Geometry and input coordinates returned by the GUI are CSS pixels; configured
sizes, spacing and scroll offsets are design units. Scaling also scales glyph
quads, caret, selection and hit testing. Scroll panels always clip their content,
even if `setClipChildren(false)` is set. Vertical content extent includes visible
direct children; use content-height sizing for nested containers when their full
measured contents should contribute to scrolling.

`wheel(position, delta)` consumes GUI input and scrolls the nearest ancestor
that can move, falling back to an outer scroll panel at a boundary. The legacy
`wheel(position)` only reports GUI ownership. Pointer drags scroll after six
scaled pixels, suppress click activation and retain contact ownership through
release. Text input drags select text. Tab/Shift+Tab reveal clipped scroll
descendants; focused scroll regions/buttons accept arrows, Page Up/Down and
Home/End. Native single-line text navigation keeps those editing keys.

`accessibilityNodes()` returns copied semantic nodes with stable IDs, roles,
names, descriptions, values, disabled/focused state and live-region flags.
`focusWidget(id)` reveals and focuses reachable controls; `activateWidget(id)`
uses the same button callback as pointer/keyboard input. IDs are process-unique
and never reused, and foreign, hidden, disabled and stale IDs are rejected.
Tree ownership is unchanged: parents uniquely own widgets, callbacks must not
outlive their captures, and no child-removal API is added.

The browser adapter exposes a visually hidden native semantic tree. Buttons use
native Enter/Space activation, following the [WAI button pattern](https://www.w3.org/WAI/ARIA/apg/patterns/button/).
Accessible textbox edits use the same UTF-8 validation, selection and composition
path as the canvas text service. Semantic focus follows C++ navigation and
reveals scrolled controls. Browser-originated selection is acknowledged before
frame synchronization. Canvas text entry uses its existing textarea; the mirror
of that focused textbox is hidden from assistive technology while it is in use.
`setLiveRegion(true)` opts text into polite announcements. Use
`setAccessibilityHidden(true)` for decorative subtrees, and supply explicit
names for images, regions and text fields. Button/label text supplies defaults.
Accessibility-hidden subtrees also leave keyboard/ID-based focus navigation;
reserve this setting for decorative content, rather than hiding usable actions.

This provides accessibility mechanisms, not a certification of a finished game.
Text shaping, bidi, grapheme wrapping/selection, font fallback and multiline
editing are outside the current contract. Wheel/touch behavior, physical mobile
keyboards/IME and real screen-reader/browser combinations require device UAT.

## Validation

Focused portable coverage lives in `tests/input_tests.cpp`, `tests/gui_tests.cpp`
and `tests/input-adapter-tests.cjs`. `scripts/ui-input-smoke.cjs` exercises the
extensions with real Wasm/WebGPU, the browser accessibility tree, Unicode edits,
scaled geometry, scrolling and Chromium touch. Existing `gui-smoke.cjs` and
`input-smoke.cjs` cover the established input/text/rendering contracts.
Verified on 2026-10-10 using an isolated MinGW 16.1.0 Debug build,
the repository-local Emscripten 6.0.11 Release build, and installed Chrome
154.0.8037.99 with real WebGPU rendering:

| Check | Result | Evidence |
| --- | --- | --- |
| Focused native `input`, `gui`, `input_adapter` suites | 3/3 passed; GUI 168 assertions, adapter 4 test groups | `build/native-ui-input/Testing/Temporary/LastTest.log` |
| Release Wasm/browser adapter build | Passed | `build/web-ui-input-release/` |
| New UI/input browser acceptance | 7 groups passed, no browser errors | `build/evidence/ui-input/result.json` |
| Existing GUI acceptance | 10 groups passed, no browser errors | `build/evidence/ui-input-gui-regression/results.json` |
| Existing desktop/touch input acceptance | 10 groups passed, no browser errors | `build/evidence/ui-input-input-regression/result.json` |

The GUI regression uses `?gui=1&shadows=off` for its existing four-pass baseline.
The extension check uses 2x DPI, browser accessibility-tree inspection, native
Unicode text editing, actual scaled/scrolling pixels and Chromium touch events.
Screenshots are retained alongside the JSON results. It checks that touch
scrolling changes the GUI offset without producing camera action deltas.
Native binding coverage also exercises the `Application::submitInput` sink
through created/running/paused/suspended/resumed/stopped states, including
rejection without replay after resume.

The shared checkout contains concurrent work; these checks used isolated native
and browser build directories and a private copy of the browser site.
Example repeat commands (supply Playwright/pngjs through `NODE_PATH` and a
WebGPU Chrome installation through `BROWSER_EXECUTABLE`):

```powershell
cmake --build build/native-ui-input --target gui_tests input_tests
ctest --test-dir build/native-ui-input -R '^(input|gui|input_adapter)$' --output-on-failure
python scripts/serve.py --port 19473 --directory build/ui-input-site
# In a second terminal:
$env:DEMO_URL = 'http://127.0.0.1:19473/'
node scripts/ui-input-smoke.cjs
```

Physical Android/iOS, OS keyboards/IME, real screen readers and sustained mobile
performance were not tested. No complete accessibility compliance claim or
cross-browser/device acceptance follows from this scoped evidence.


## Optional gamepads

[Stage 3 input contracts](driving-controls.md#optional-browser-gamepad-adapter) add
web/gamepad.js polling through BrowserInput.flush, source-isolated axes/buttons,
context ownership, validated response/dead-zone/inversion settings, controller
selection and neutral re-arm after focus/lifecycle changes. Bindings and devices
remain host state, with read-only status and isolated preview controls in the editor.
