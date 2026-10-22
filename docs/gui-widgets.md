# Custom widgets, checkbox and slider

Portable APIs live in `include/engine/gui.hpp` and optional `engine_gui`.
Both WebGPU and WebGL 2 consume the same GUI draw list. Open
`?gui=1&guiSettings=1` for a sound-settings preview: the checkbox and slider
update local application state and a custom painted reset button. This demo does
not configure audio playback. Existing `?gui=1` and `guiExtensions=1` demos retain
their controls. See [base GUI contracts](gui.md).

## Value controls

```cpp
auto& settings = gui.root().add<Panel>();
settings.setSize({280,180});
settings.setPadding(12);
settings.setLayout(VerticalLayout{8});
auto& sound = settings.add<Checkbox>("Enable sound", true);
sound.setStretch(true);
sound.onChange([](bool checked) { /* Application chooses audio policy. */ });
auto& volume = settings.add<Slider>();
volume.setStretch(true);
volume.setAccessibleName("Volume");
volume.setRange(0,100,5);
volume.setValue(65);
volume.onChange([](float value) { /* Update application state. */ });
```

`Checkbox` defaults to unchecked and size 240x38. It toggles on release inside
its clipped bounds, accessible activation, Enter or Space. Scroll takeover,
outside release and pointer cancellation do not toggle. Repeated activation
keys do not toggle repeatedly. `setText` validates UTF-8 before publication.

`Slider` defaults to horizontal, size 240x38, range 0..1, step .01, value .5.
`setRange(minimum,maximum,step=.01)` requires finite values, minimum < maximum,
finite representable span and 0 < step <= span; it clamps/snaps the existing
value after replacing a valid range. Invalid arguments leave the prior state
and callbacks unchanged. `setValue` rejects nonfinite input, clamps finite
input and rounds to the nearest step relative to minimum; ties round upward.
The maximum is explicitly reachable even when the step does not divide the
range. Both setters notify only when the effective value changes. Checkbox
`setChecked` follows the same changed-value notification rule. Register callbacks
after initial configuration to avoid setup notifications when appropriate.

Slider pointer-down updates immediately and reserves capture through release
or cancellation, including movement outside the widget/canvas. Diagonal drags
do not scroll an ancestor. Left/Down decrease one step, Right/Up increase one,
Page Down/Up change ten steps, and Home/End choose the endpoints. Held navigation
keys repeat. These keys act on a focused slider before scroll-panel navigation.
Cancellation ends the drag and retains the last published value. Escape also
clears focus. Hiding, disabling or fully clipping a captured widget on layout
delivers cancellation. Call `pointer(Cancel,...)` and `blur()` on host blur or
input ownership changes; the supplied browser adapter already does this.

## Application-defined widgets

```cpp
class ColorButton : public Widget {
public:
    std::function<void()> clicked;
protected:
    bool interactive() const override { return true; }
    bool focusable() const override { return true; }
    void onPaint(GuiPainter& painter) override {
        const auto& style = painter.style();
        const auto size = painter.size();
        painter.fill({0,0,size.x,size.y},
            painter.focused() ? style.accent : style.background);
        painter.text("Choose color",{8,6},style.foreground);
    }
    GuiPointerResult onPointer(const GuiPointerEvent& event) override {
        if (event.action == GuiPointer::Up && event.inside) onActivate();
        return GuiPointerResult::Handled;
    }
    bool onActivate() override {
        if (clicked) clicked();
        return true;
    }
    void describeAccessibility(GuiAccessibleNode& node) const override {
        node.role = GuiRole::Button;
        node.name = "Choose color";
    }
};
// panel.add<ColorButton>().clicked = ...;
```

All hooks default to no-op, so existing widgets remain compatible:

- `onPaint(GuiPainter&)`: called after built-in drawing, before children, for a
  visible widget with a nonempty clip. `fill`, `image` and single-line `text`
  append primitives in widget-local design units. `size`, `measureText` and
  `lineHeight` use design units too. The painter applies logical scaling,
  ancestor/canvas clipping and resolved style opacity. Use `painter.style()` for
  [theme-aware colors](gui-themes.md). `hovered`, `pressed`, `focused`,
  `enabled` and caller-provided `seconds` expose presentation state. Text is
  validated and its glyphs populated before immutable atlas references are used.
- `onPointer(const GuiPointerEvent&)`: receives local design-unit `position`
  and widget `size`, action, selection modifier and `inside` for the current
  clipped bounds. The system selects the topmost interactive widget and captures
  its press. Captured moves/releases continue outside its bounds. `Handled`
  permits scroll takeover; `Capture` returned on Down reserves the drag from
  scrolling. `Ignored` permits existing built-in behavior. There is no event
  bubbling or second widget ownership. Hover moves reach an uncaptured target.
- `onKey(const GuiKeyEvent&)`: focused-widget navigation before scroll fallback;
  return true to consume. Tab and Escape remain system-owned. `repeat` lets a
  control distinguish held navigation from one-shot actions.
- `onActivate()`: shared Enter/Space and ID-based accessible activation. Pointer
  hooks call it explicitly when their release policy permits activation. Return
  true when handled. Keyboard activation repeats are suppressed by the system.
- `describeAccessibility(GuiAccessibleNode&)`: supplies a supported role, name,
  description, value and role-specific checked/range fields. System-owned IDs,
  bounds, disabled/focused state and live-region opt-in are restored after this
  hook. Use `onAccessibleValue(float)` for numeric semantic actions; the system
  rejects invalid IDs, inactive targets and nonfinite values first.

The painter, events and semantic output are borrowed for the hook only. Do not
retain their references or reenter GUI traversal/input/layout from hooks. Do not
structurally modify the tree during layout/paint/semantic traversal. Value
callbacks may update application state and existing widget properties. Parents
still uniquely own children; there is no child-removal API. Callback captures
must outlive the widgets; detach browser/renderer references before destruction.
Explicit sizing, clipping and existing font limitations remain unchanged; there
is no general custom measurement/layout hook or arbitrary semantic-role plugin.

## Browser semantics and validation

`GuiRole::Checkbox` and `GuiRole::Slider` append to the existing role values.
The browser adapter exposes hidden native checkbox/range controls with names,
disabled state, checked state and numeric range/current value. Native actions
route through `activateWidget` / `setAccessibleValue` into the same C++ setters.
The range mirror uses `step=any` because C++ owns quantization and explicit
maximum reachability. Native input handling must not apply a second value change
after a consumed C++ keyboard action. `GuiKey::Left`/`Right` append to the existing
portable key values. Text input retains native single-line cursor navigation.

Build `gui_tests` and run CTest `gui`; use `input` / `input_adapter` as routing
regressions. Build the browser with the repository-local SDK pinned by
`.emscripten-version`, publish matching Wasm/host/fonts, and serve the selected
site. With Playwright/pngjs available through `NODE_PATH`:

```powershell
$env:DEMO_URL = 'http://localhost:8080'
$env:BROWSER_EXECUTABLE = 'C:/Program Files/Google/Chrome/Application/chrome.exe'
node scripts/gui-widgets-smoke.cjs
```

The script forces WebGPU/WebGL2 at DPR1/DPR2 and checks actual canvas pixels,
checkbox pointer/keyboard activation, slider navigation/capture/cancellation,
scroll arbitration, custom-widget activation, native semantic actions/AX nodes,
scaling, resize and scene-input isolation. Evidence defaults to
`build/evidence/gui-widgets`. Retain `gui-smoke.cjs`, `ui-input-smoke.cjs` and
affected input smoke checks as regressions. See
[executed validation](gui-widgets-validation.md) for results and scope. Desktop
Chrome and AX-tree checks do not establish real screen-reader, physical mobile,
OS IME or sustained mobile performance acceptance.
