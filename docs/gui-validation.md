# GUI validation — 2026-10-09

Implemented C++ retained widgets (`Panel`, `TextLabel`, `TextInput`, `Image`,
`Button`), explicit/anchored/flow layout, nested clipping, pointer capture, focus
and keyboard navigation, TrueType atlas text, selection/caret rendering, and a
dedicated WebGPU GUI pass. The visible GUI runs entirely in the engine canvas.
The invisible browser textarea supplies text services and composition events.

The implementation is packaged as `engine_gui` with optional
`engine_gui_browser`; the supplied renderer accepts `setGui(&gui)`. The demo is
available at `?gui=1`, with a two-input acceptance fixture at
`?gui=1&guiTest=1`. See [API and architecture](gui.md).

## Passed automated evidence

- Native Debug build: **15/15 CTest suites passed**, including GUI, performance
  accounting, and the concurrently added shared input system. The Windows test
  process used the configured MinGW compiler's `bin` directory on PATH to select
  its matching runtime DLLs.
- Emscripten **6.0.11 Release** browser build compiled and published to `dist`.
- Installed **Chrome 154.0.8037.99**, headless WebGPU: **10 GUI browser checks
  passed**, with no recorded browser errors. Checks cover actual canvas changes,
  Unicode text and UTF-8/UTF-16 selection, replacement, backward selection and
  deletion, submission, Clear, Tab/Shift+Tab, Space, Escape, capture, camera
  isolation, wheel isolation, scene orbit/reset, synthetic composition events,
  independent text fields across composition/focus changes, responsive resizing,
  2× DPI input coordinates, and the fourth pass/resource counters.
- Existing browser smoke suite: **14 scenario groups passed**, including Knarr
  rendering, lights, pause/orbit/zoom/reset, import/texture/error recovery,
  resize/mobile shell and unsupported-browser handling. GUI is opt-in, so the
  normal playground preserves its three scene passes.
- Visually inspected the canvas and full-page screenshots at ordinary and 2×
  DPI. The GUI demo hides the HTML scene-title decoration to avoid overlapping
  its engine controls.

Evidence files (ignored build outputs):

- `build/evidence/gui/results.json`
- `build/evidence/gui/gui-initial.png`
- `build/evidence/gui/gui-selection.png`
- `build/evidence/gui/gui-resized.png`
- `build/evidence/gui/gui-high-dpi.png`
- `build/evidence/gui/canvas-before.png`
- `build/evidence/gui/canvas-after-drag.png`
- `build/evidence/gui-regression/` (existing browser smoke evidence)

GUI interaction does not move scene pixels. Pixel comparisons exclude the GUI
rectangle and the canvas's CSS focus outline, which changes when keyboard focus
returns from the textarea to the canvas. The actual scene region remains fixed
until a scene-owned gesture is performed.

## Scope

Composition acceptance uses synthetic browser composition events. Physical OS
IME use, mobile virtual keyboards, and a browser/GPU/device matrix remain separate
acceptance work. This is correctness evidence, not an FPS benchmark.

Text fields are single-line. Glyph coverage follows the chosen font. Complex
script shaping, bidi, grapheme-aware pointer selection, kerning, color emoji,
multiline text, automatic sizing/wrapping, and a screen-reader representation of
canvas labels/buttons are not implemented in this version. Font parsing accepts
only vetted application assets, not arbitrary player uploads.

Existing timestamp-query measurements still cover the three scene passes; GUI
GPU execution is not timed. GUI draws/uploads and nominal texture/buffer payload
are included in totals and exported separately. The memory estimate excludes
font source bytes, mutable atlas pages and widget metadata.

The checkout was already untracked at the start. No commit, branch reset or
worktree transfer was performed. Concurrent shared-input edits were preserved;
GUI capture and keyboard ownership now integrate through that adapter.
