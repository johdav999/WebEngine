# In-engine GUI

The optional Windows `DesktopHost` integrates this same retained widget tree and
renderer with SDL mouse, wheel, focus/capture, keyboard navigation and menu
activation. See [desktop API and GUI ownership](windows-desktop.md) and the vehicle
pause/settings example. This adapter supports buttons, checkboxes, sliders,
scroll panels and single-line TextInput editing, clipboard shortcuts and SDL IME
preedit/commit. OS services are private to the desktop host; browser text/semantic
adapters remain separate. Native Windows exposes the same semantic controls through
[UI Automation](accessibility-input.md).

The GUI is a retained C++ widget tree rendered into the same WebGPU canvas as the
scene. `Panel`, `ScrollPanel`, `TextLabel`, `TextInput`, `Image`, `Button`,
`Checkbox` and `Slider` live in
`include/engine/gui.hpp`. There are no visible HTML controls for these widgets.

Run the browser build and open `http://localhost:8080/?gui=1`, or use the
**In-engine GUI demo** link. The profile panel demonstrates an atlas-rendered
label, avatar image, editable name, Join/Clear buttons, and a camera-reset
callback. This is a local interaction example; Join updates the C++ welcome
label rather than connecting to a multiplayer server.

Open `?gui=1&guiSettings=1` for the sound-settings preview, with a checkbox,
stepped slider and application-defined painted/interactive widget. These update
local preview state; they do not change an audio bus. See
[custom widgets and value controls](gui-widgets.md) for extension contracts.

## Creating widgets

```cpp
#include "engine/gui.hpp"
using namespace engine;

GuiSystem gui; // Keep alive as long as the renderer and browser adapter use it.
auto& panel = gui.root().add<Panel>();
panel.setPosition({20, 20});
panel.setSize({300, 260});
panel.setPadding(12);
panel.setLayout(VerticalLayout{8});

auto& label = panel.add<TextLabel>("Player name");
label.setSize({276, 24});

auto& input = panel.add<TextInput>();
input.setSize({276, 40});
input.setPlaceholder("Enter your name");

auto& image = panel.add<Image>(avatarTexture); // shared_ptr<const Texture>
image.setSize({48, 48});

auto& button = panel.add<Button>("Join game");
button.setSize({276, 40});
button.onClick([&] { joinGame(input.text()); });
input.onSubmit([&] { joinGame(input.text()); });
input.onChange([](const std::string& text) { /* Update application state. */ });
```

Parents own children through `unique_ptr`. `add<T>()` returns a stable reference;
widgets cannot be copied. Callback captures must outlive their use. This first
version provides visibility and enabled state rather than child-removal APIs.

`setVisible(false)` hides the entire subtree and removes it from flow layout.
`setEnabled(false)` suppresses interaction and dims the subtree. A focus or
capture whose widget becomes inactive is cleared on layout; fully clipped focus
is also cleared. Later children paint on top of earlier children and receive
hits first.

## Layout and appearance

Layout uses logical CSS pixels with a top-left origin. Explicit sizes remain the
default. Optional minimum/maximum sizes, per-axis content sizing, alignment and
equal-column `GridLayout` are described in [measured GUI layout](gui-layout.md).
With default limits, zero size hides geometry. Invalid sizes, limits, gaps and
alignment are rejected before replacing configuration or a complete layout.

Containers support absolute positioning, `VerticalLayout{gap}` and
`HorizontalLayout{gap}` and `WrapLayout{gap,rowGap}`. Wrap layout starts a new
row when the next visible child exceeds the content width. Padding reduces the available content rectangle.
Absolute children can use `GuiAnchor::TopLeft`, `TopRight`, `BottomLeft`,
`BottomRight` or `Center`. Corner positions are inward margins; centered
positions are offsets from the centered rectangle. Flow containers use each
child's explicit size and position offset, not its anchor.

Children are clipped to the parent's content rectangle by default. Use
`setClipChildren(false)` to allow an extending child, such as a popup. Hit
testing follows clipping, including children extending outside their parent.
The root always clips to the canvas. `setStretch(width,height)` fills either
content dimension. `TextLabel::setWrap(true)` wraps words, breaks oversized words
at UTF-8 codepoint boundaries, honors newlines and derives its height from the
font; pass `false` as the second argument to retain an explicit height.
Other controls keep explicit heights unless content sizing is enabled.
Unwrapped labels also honor newlines.

`GuiSystem::setScale(0.25..4)` scales design dimensions, glyph geometry, spacing,
caret/selection and hit testing together. `layout()` and pointer coordinates
remain CSS pixels; browser DPI scaling is independent. `ScrollPanel` clips its
children, computes vertical content extent, clamps a design-unit scroll offset,
and draws a scroll indicator. Wheel, pointer drag, arrows, Page Up/Down and
Home/End scroll it. Tab and accessible focus reveal controls outside its viewport.
Scroll drags suppress pending button activation; text-selection drags remain
with `TextInput`. See [UI/input integration](ui-input-integration.md).

`widget.style` supplies partial local overrides such as
`widget.style.background={r,g,b,a}`. The GUI-owned theme supplies global/component
defaults and checked, hovered, focused, pressed and disabled styles. Inputs and
buttons use `focusRing` for focus; images use neutral white tint by default.
Read effective colors from `GuiSystem::resolvedStyle` or `GuiPainter::style`.
See [themes and per-state styling](gui-themes.md). Transparent panel backgrounds/borders
allow nested layout containers without visible boxes. Images fill their explicit
rectangle, so set dimensions with the source aspect ratio when appropriate.

## Rendering and application integration

The portable `engine_gui` library links to `engine_core`. The optional
Emscripten `engine_gui_browser` target supplies the text/input adapter. The
existing `Renderer` integrates `src/gui_renderer.cpp` and the generated
`shaders/gui.wgsl` shader.

```cpp
#include "engine/gui_browser.hpp" // Browser builds only.

renderer.setGui(&gui);
attachBrowserGui(&gui);
// After each renderer.render(...):
syncBrowserGui();

// Before releasing the GUI:
renderer.setGui(nullptr);
attachBrowserGui(nullptr);
```

Keep `web/gui.js` in the host before `web/input.js` and `web/app.js`. The supplied
`#viewport` host uses `EngineInput.BrowserInput` to route GUI gestures first,
then forwards scene gestures as engine input actions. The adapter also uses
`EngineGui.ownsKeyboard()` to protect focused GUI controls from game bindings.
Applications with a different host can call `GuiSystem::pointer`, `wheel`,
`key` and `editText` directly and honor their consumed-event results. Pointer
coordinates passed to the core are logical pixels, not framebuffer pixels.

The renderer lays out widgets for the canvas's current logical size, generates
the draw list, and appends a fourth pass after Background, World and Overlay.
The GUI pass loads the scene color, uses the full canvas viewport, has no depth
attachment, and blends premultiplied alpha. Rectangles, images and glyphs share
one pipeline. Adjacent commands with the same texture and clip batch; order is
preserved for transparency. Scissors are converted using the actual ratios
between framebuffer and logical dimensions, including the host's DPI/resolution
caps. GPU vertex/index buffers retain capacity and upload only when data changes.
Expired GPU image/atlas snapshots are reclaimed.

For a custom renderer, call `gui.layout(logicalSize)` and `gui.draw(seconds)`.
The returned `GuiDrawList` contains vertices, uint32 indices and commands with
texture references, index ranges and logical clip rectangles. Its references
remain valid until the next draw call. The atlas snapshots keep their own pixels
and are safe to retain for a submitted frame.

## Fonts and text editing

The browser adapter loads the bundled Noto Sans Regular by default. To use a
different vetted font, set it before attaching the browser adapter:

```cpp
gui.setFont(GuiFont::loadTrustedTrueType(fontBytes, 22));
```

This API accepts **trusted application font assets only**. stb_truetype assumes
well-formed font tables and is not a parser for arbitrary player-supplied fonts.
The shipped font and rasterizer have pinned provenance and licenses under
`third_party/noto` and `third_party/stb`.

Fonts measure proportional Unicode glyphs and rasterize them lazily into 512×512
RGBA pages. Cache identity uses the font's glyph index, so unsupported codepoints
share the missing glyph rather than growing duplicate atlas entries. Each font
has at most sixteen atlas pages; exhaustion falls back to the missing glyph.
Text is UTF-8. Malformed UTF-8 is rejected atomically in widget text setters.
Font coverage determines which scripts render. This version lays out individual
codepoints; it does not implement shaping, bidi, grapheme-aware pointer selection,
font fallback, kerning, emoji color fonts, or multiline text editing.

`TextInput` is single-line; line breaks and tabs become spaces. The browser
textarea service handles insertion/deletion, native selection shortcuts,
clipboard operations and IME events. The canvas draws text, selection,
horizontal scrolling, a blinking caret and a composition indicator. Selections
in the C++ API use UTF-8 byte offsets snapped to codepoint boundaries; the browser
adapter converts to/from UTF-16 offsets and preserves selection direction.
Text/selection revisions and immediate edit acknowledgements prevent animation-frame
synchronization from overwriting a browser selection before its `selectionchange`
event reaches C++. Browser composition sessions are associated with the focused
widget, so a late commit cannot overwrite another field after focus changes.

Click/drag selects by glyph advances. Buttons retain capture until release or
cancel and activate only when released over the pressed button. Tab/Shift+Tab
cycle active, visible focusable controls. Enter submits an input or activates a
button; Space activates a focused button. Escape clears focus. Enter during
composition does not submit. Native browser focus changes and window blur release
GUI focus and capture. The text service currently permits 2048 UTF-16 code units
for browser typing.

## Metrics and verification

Performance totals include GUI draw calls, triangles, queue writes, passes and
GPU texture/buffer payloads. The JSON also exposes `guiDrawCalls`, `guiTriangles`,
`guiUploadCalls`, `guiUploadBytes`, `guiTextureBytes` and `guiBufferBytes`.
GUI CPU generation runs during `encodeMs`. Existing GPU timestamps cover only
Background, World and Overlay, and do not measure GUI execution. Font source
bytes, mutable atlas storage and widget metadata are outside the memory estimate.

`window.EngineGui.snapshot()` provides read-only widget state and bounds for
application diagnostics and browser acceptance. It is not needed to render or
interact with widgets.

```powershell
ctest --test-dir build/native --output-on-failure
# With a local browser build/server and Playwright/pngjs supplied through NODE_PATH:
$env:DEMO_URL = 'http://localhost:8080/?gui=1'
node scripts/gui-smoke.cjs
```

The GUI browser script captures actual WebGPU pixels, types into the native
textarea service, exercises focus/capture and checks resizing at 1× and 2× DPI.
Its composition events are synthetic: physical OS IME sessions, mobile keyboards
and a browser/device matrix require separate acceptance. Canvas buttons/labels
have a browser accessibility representation with native buttons/textboxes,
names, descriptions, disabled state and opt-in polite live regions. Real
screen-reader and physical device acceptance remain separate checks.
Evidence defaults to `build/evidence/gui`.
