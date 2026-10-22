# GUI sizing, alignment and grid layout

Portable `engine_gui` measures and arranges the retained tree before either
renderer consumes its draw list. Explicit widget sizes, anchors, flow offsets,
clipping and stretch remain the defaults. New content sizing and alignment are
opt-in. Open `?gui=1&guiLayout=1` for live two/three-column changes, content growth,
alignment changes, hidden-card compaction, nested content-sized cards and scrolling.
This example hides the host's floating camera buttons so they cannot cover the
canvas controls at narrow sizes; ordinary scene mouse/keyboard input remains.

## Size limits and content sizing

```cpp
auto& grid = panel.add<Panel>();
grid.setStretch(true);
grid.setContentSize(false,true); // Derive height; width follows its parent.
grid.setPadding(12);
grid.setLayout(GridLayout{3,12,12});
auto& button = grid.add<Button>("Choose an item");
button.setContentSize();
button.setSizeLimits({100,38},{220,80});
button.setStretch(true); // Fill the cell, then clamp to the maximum.
button.setAlignment(GuiAlign::Center,GuiAlign::Start);
```

`setSizeLimits(minimum,maximum)` atomically configures both design-unit bounds.
`setMinSize` / `setMaxSize` replace one bound while checking the other.
Components must be finite, nonnegative, and minimum <= maximum on both axes.
Defaults are zero minimum and `std::numeric_limits<float>::max()` maximum.
`minSize()` / `maxSize()` return configured values. `setSize` retains a preferred
explicit size; final sizes clamp to limits without overwriting that preference.
Zero explicit sizes remain zero with default limits; nonzero minima take priority.

`setContentSize(width=true,height=true)` opts each axis into natural content size.
Pass `false,false` to return both axes to explicit sizing. Resolution is natural
content or explicit preference, then requested stretch, then minimum/maximum
clamping. Width resolves before wrapped text height. The existing
`TextLabel::setWrap(true)` auto-height behavior is also clamped by limits;
`setWrap(true,false)` retains explicit height unless content height is enabled.

Intrinsic sizes are:

- Labels: longest unwrapped line for natural width; font line height for each
  line, with word/codepoint wrapping at the final constrained width when enabled.
- Buttons: measured text plus 16 horizontal / 12 vertical design units.
- Single-line inputs: value or placeholder plus 18 horizontal / 16 vertical units.
- Checkboxes: measured text plus 32 horizontally; max(font height,22) plus 8 vertically.
- Images: intrinsic texture dimensions; a single content-sized axis preserves the
  texture aspect ratio relative to the other resolved dimension.
- Panels and child containers: visible children, layout gaps and padding, without
  trailing flow gaps. Hidden children contribute no cells or content extent.
- Sliders and other/custom leaf widgets: their existing preferred size. No custom
  intrinsic-measure callback is introduced.

Before a font or image is available, leaves retain their preferred size; measure
again when those resources become ready. Container width measurement ignores
dependent child stretch. Height measurement ignores dependent child height stretch
and vertical packing, then arranges children against the resolved parent size.
This avoids parent-content/child-stretch cycles. Content-width horizontal/wrap
containers prefer one row; apply a maximum width to wrap that result. A natural
grid width uses the widest natural child for each configured column. Absolute
content extents use child offsets and natural sizes; bottom/center anchors do not
create a circular height dependency.

## Alignment

`GuiAlign` is Start, Center or End. `setContentAlignment(horizontal,vertical)`
configures a container's group packing and default cross/cell alignment.
`setAlignment(horizontal,vertical)` explicitly aligns a child; `clearAlignment()`
restores the container default or legacy absolute anchor. Invalid enum values
are rejected before either axis changes.

- Vertical flow packs the group vertically and aligns children horizontally.
- Horizontal flow packs the group horizontally and aligns children vertically.
- Wrap aligns each row horizontally, packs rows vertically, and aligns children
  within each row's height.
- Grid aligns children within their cells and packs the rows vertically.
- Absolute children retain their anchors by default. Explicit self-alignment
  replaces the anchor with alignment in the parent's padded content rectangle.

Position offsets apply after alignment. Corner-anchor offsets retain their existing
inward-margin interpretation. Main-axis group packing uses only positive free
space so overflowing flow content remains scrollable from its start. Cell/cross
alignment can position an oversized minimum-constrained child beyond its cell;
normal clipping still applies. Stretch fills a parent/cell dimension before
limits and alignment; this is not flex-weight or leftover-space distribution.

## Grid

`GridLayout{columns,columnGap,rowGap}` accepts 1..64 equal-width columns and finite
nonnegative gaps. `setLayout(GridLayout{...})` validates the complete candidate
before replacing the old layout. `layoutType()` and `gridColumns()` expose the
current kind/count for tooling; non-grid widgets return zero columns.

Visible children fill cells in row-major tree order. Column width is the padded
content width minus column gaps, divided equally, clamped at zero. A row's height
is its tallest measured child; height stretch fills that row rather than choosing
a different row height. Width/height limits still win over cell stretch. Hidden
children leave no reserved holes. Column spans, explicit cell placement, fraction/
auto track definitions and implicit responsive column selection are not added;
applications choose columns in response to their UI policy.

Grid/container content height includes every measured row, row gaps and padding,
so placing it inside a `ScrollPanel` exposes the full extent. Existing wheel,
drag cancellation, Tab/accessible focus reveal and GUI input ownership remain.
UI scaling changes all dimensions/gaps/bounds/hit targets together; scroll
offsets remain in design units. Browser DPI remains independent.

## Publication, ownership and checks

Each `layout(logicalSize)` stages measurements, rectangles, wrapping lines and
scroll extents before publishing bounds. Nonfinite/overflow geometry and invalid
size/grid/alignment arguments fail without replacing the previous complete
layout or focus. Configuration edits remain the application's current preferred
values and must be corrected before another failing layout is attempted.
Measurements cache by widget, available dimensions and stretch policy per pass.
The pass rejects depth beyond 128 and work beyond 65,536 measurement requests;
there is no iterative layout feedback loop. Cached font glyph generation can
occur during measurement; immutable submitted atlas snapshots remain valid.

Parents retain unique child ownership, IDs/callbacks remain unchanged, and the
existing no-removal and hook traversal/lifetime contracts remain. Themes affect
appearance, not these geometry inputs. Browser diagnostics add names, layout kind
and grid-column counts alongside final CSS bounds and clip rectangles.

Build native `gui_tests` / `input_tests`; run CTest `gui`, `input`, `input_adapter`.
Use the README's pinned local Emscripten SDK to build `web_assets`, stage matching
host/Wasm/fonts and serve the actual chosen directory. With `DEMO_URL`,
`NODE_PATH` (Playwright/pngjs) and `BROWSER_EXECUTABLE`, run
`scripts/gui-layout-smoke.cjs`. It forces WebGPU/WebGL2 at DPR1/DPR2, records
HTTP-served hashes and verifies measured geometry, real reflow pixels, repeated
content/alignment controls, hidden cells, identity/focus/reveal, scale/resize,
pointer activation and scene isolation. Retain existing theme/widget/GUI/UI-input
regressions. See [executed validation](gui-layout-validation.md) for results.
