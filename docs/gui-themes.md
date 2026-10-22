# GUI themes and state styles

`engine_gui` resolves appearance in portable C++ and emits the same draw list for
WebGPU and WebGL 2. `GuiSystem` owns a theme value; widgets retain local overrides.
`setTheme` copies a fully validated candidate and changes appearance on the next
draw. It does not recreate widgets, change IDs/values, move focus, or alter layout,
capture, scale or accessibility. No browser/GPU objects enter the theme API.

Open `?gui=1&guiTheme=1` for dark/light switching, all existing controls, a custom
painted preview, local purple button states and disabled controls. The profile
demo is hidden in this mode. The showcase scrolls when content exceeds its
explicit height. A checked checkbox uses a green component-level theme override.

## Defaults and partial overrides

```cpp
GuiTheme theme = GuiTheme::light(); // GuiTheme{} is the default dark theme.
theme.defaults.background = {0.95f,0.96f,0.98f,1};
theme.defaults.accent = {0.02f,0.36f,0.5f,1};
theme.common.normal.border = {0.55f,0.62f,0.7f,1};
auto& buttons = theme.forKind(GuiStyleKind::Button);
buttons.normal.background = {0.85f,0.9f,0.95f,1};
buttons.states.hovered.background = {0.7f,0.82f,0.9f,1};
buttons.states.pressed.background = {0.5f,0.7f,0.82f,1};
buttons.states.focused.focusRing = {0.02f,0.3f,0.55f,1};
theme.common.states.disabled.opacity = 0.4f;
gui.setTheme(theme);

button.style.background = {0.3f,0.14f,0.48f,1}; // Existing assignment spelling.
button.stateStyles.hovered.background = {0.48f,0.24f,0.68f,1};
button.stateStyles.pressed.background = {0.18f,0.06f,0.3f,1};
button.style.background.reset(); // Resume inheriting this field.
button.style = GuiStyleOverrides{}; // Clear all local normal overrides.
button.stateStyles = GuiStateStyles{}; // Clear all local state overrides.
```

`GuiStyle` is a concrete resolved value with `background`, `foreground`, `border`,
`accent`, `focusRing`, `selection`, `placeholder` RGBA colors and overall `opacity`.
Colors and opacity must be finite and within 0..1. Transparent colors are explicit
overrides, including zero alpha. Image component defaults use white foreground
for neutral tint; changing global text foreground does not tint images.

`GuiStyleOverrides` contains optional color overrides plus `std::optional<float>`
opacity. Color assignment validates before replacing that field. `GuiColorOverride`
provides `hasValue()` / `reset()` and conversion to its local color value, with
legacy dark fallback when unset. Effective theme values must be read through
`gui.resolvedStyle(widget)` or `painter.style()`. The legacy `widget.style.color =
{r,g,b,a}` syntax is supported. Color wrappers do not expose writable `.r/.g/.b/.a`
members; edit a `GuiColor` value and assign it back. Assigning an entire `GuiStyle`
to `widget.style` explicitly overrides every normal field; prefer partial overrides
when later theme changes should propagate.

`GuiTheme::forKind` supports Widget, Panel, ScrollPanel, TextLabel, Image, Button,
TextInput, Checkbox and Slider. Custom widgets default to Widget; override the
public virtual `styleKind()` to opt into another component's defaults. Theme
kind changes do not change a widget's input behavior or semantic role.

## State resolution

Normal fields resolve in this order: `theme.defaults`, `theme.common.normal`,
the component's `normal`, then `widget.style`. Only explicitly supplied fields
replace earlier values. State fields then apply in order:

1. Checked.
2. Hovered.
3. Focused.
4. Pressed.
5. Disabled.

For each active state, apply theme common, component, then widget-local overrides.
This makes pressed colors visible even when the control also has focus. Local
normal colors can be superseded by theme state colors; supply a local state
override to keep a color in that state. Disabled controls have no hovered/pressed
state and lose focus on layout. A checked disabled checkbox retains checked state
before disabled styling. Inherited disabled state comes from `Widget::active()`.
Styles are not inherited from parent widgets; a parent's local opacity applies
to that parent's own primitives. Theme defaults affect each widget individually.

The dark theme preserves the existing palette, with button hover/focus/press
defaults and shared disabled opacity .4. The light factory supplies readable dark
text, light surfaces, selection/placeholder colors and corresponding button
states. Custom themes can override the disabled opacity, including setting it to
1, without changing whether a widget is interactive.

`background` fills surfaces, `foreground` supplies text/image tint/check marks
and the slider's idle thumb, `border` supplies outlines/tracks, and `accent`
supplies checked fill, slider fill and interactive thumb/scroll indicators.
`focusRing` supplies focused outlines/indicators. Text input uses `selection` and
`placeholder` directly rather than hardcoded colors. A field only affects the
primitives a component draws; labels, for example, do not gain a background box.
Opacity multiplies primitive alpha once, without being baked into resolved colors.

## Custom painting and validation

```cpp
void MyWidget::onPaint(GuiPainter& painter) {
    const auto& style = painter.style();
    const auto size = painter.size();
    painter.fill({0,0,size.x,size.y},style.background);
    painter.text("Preview",{8,6},style.foreground);
}
```

The painter supplies resolved colors and applies opacity to primitives. Do not
apply `style.opacity` again or read raw local overrides for theme-aware drawing.
Existing custom-widget clipping, design-unit coordinates and borrowed context
lifetimes remain; see [custom widgets](gui-widgets.md).

`setTheme` validates every normal/state layer, including inactive states, before
replacing the current theme. Unknown kinds and invalid colors/opacity throw
`std::invalid_argument`. Public local patches are validated before draw-list
clearing; invalid local opacity edits throw and retain the previous complete
draw list. Correct the local patch before drawing again. `GuiTheme::validate()`
and `GuiStyleOverrides::validate()` are available for staging edits.

`GuiSystem::visualState(widget)` reports current flags; `resolvedStyle(widget)`
returns a copied effective style. Browser diagnostics include `styleKind`,
hovered/pressed/disabled flags and resolved color/opacity values in
`EngineGui.snapshot()`. These are read-only diagnostic values, not setters.

Build native `gui_tests` / `input_tests`, run CTest `gui`, `input`, `input_adapter`,
and build `web_assets` with the repository-local SDK in `.emscripten-version`.
With the README's Playwright/pngjs setup, run `scripts/gui-theme-smoke.cjs` with
the actual served `DEMO_URL`, `NODE_PATH`, and `BROWSER_EXECUTABLE`. It forces
WebGPU/WebGL2 at DPR1/DPR2, records served build hashes, and checks actual state
pixels, theme propagation, inline overrides, semantic disabled actions, value/
focus/layout preservation, text selection, custom painting, scaling, resize and
scene isolation. Retain GUI/widget/UI-input regressions on the same built site.
See [executed validation](gui-themes-validation.md) for results and limits.

This adds appearance colors and opacity. Font selection, layout metrics,
transitions/animations, theme persistence and CSS-style selector systems remain
application choices or future work.
