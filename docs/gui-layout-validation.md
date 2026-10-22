# GUI measured layout validation — 2026-10-19

Delivered optional minimum/maximum sizes, per-axis content sizing, container and
child alignment, and equal-column grid layout through portable `engine_gui`.
Explicit size/anchor defaults and existing theme/input/render integration remain.
The layout demo is `?gui=1&guiLayout=1`. See [API and limits](gui-layout.md).

## Executed checks

| Check | Result | Evidence |
| --- | --- | --- |
| Native Debug `gui`, `input`, `input_adapter` | 3/3 passed | `build/gui-layout-native-tests.log`, `build/gui-layout-native-final.log` |
| Repository-local Emscripten 6.0.11 Release `web_assets` | Passed | `build/gui-layout-web-verified.log` |
| Measured layout, forced WebGPU/WebGL2, each DPR1/DPR2 | Four runs passed; six groups each, no browser errors | `build/evidence/gui-layout/results.json` and captures |
| Theme/state regression on both renderers/DPRs | Four runs passed; five groups each | `build/evidence/gui-layout-theme-regression/results.json` |
| Checkbox/slider/custom-widget regression on both renderers/DPRs | Four runs passed; eight groups each | `build/evidence/gui-layout-widgets-regression/results.json` |
| Existing GUI regression on both renderers | 10 groups per backend passed | `build/evidence/gui-layout-gui-webgpu/`, `build/evidence/gui-layout-gui-webgl2/` |
| Existing UI/input integration on both renderers | Seven groups per backend passed | `build/evidence/gui-layout-ui-webgpu/`, `build/evidence/gui-layout-ui-webgl2/` |
| Existing input suite on both renderers | 10 groups per backend passed | `build/evidence/gui-layout-input-webgpu/`, `build/evidence/gui-layout-input-webgl2/` |
| Installed and CatGame engine-skill | Both validated; new references identical; consumer guidance retained | Both `references/gui-layout.md` files; `build/update-gui-layout-skills.py` |

Native cases cover complete size-limit rejection, constrained stretch, text and
image intrinsic sizes/aspect ratio, nested visible content/gaps/padding, width-
constrained wrapping and clipped height, dependent stretch sizing, offset flow
extents, absolute/cross/group/cell alignment, hidden-grid compaction, grid row
height, scaling, nested scroll extent and focus reveal. Layout overflow preserves
the prior complete bounds and focus, rather than publishing a partial tree.

Browser tests use real canvas events, native accessible focus/actions, CSS bounds,
and PNG comparisons. They repeatedly change columns, text and alignment, hide and
restore a middle card, retain IDs, reveal and click scrolled descendants, and
resize at independently scaled UI/DPR values. Reflow changes GUI pixels while
scene pixels remain unchanged; no GUI action issues camera deltas. Initial and
narrow/scaled WebGPU/WebGL2 captures were visually inspected. The layout example
hides host zoom overlays that would otherwise cover controls at narrow widths.

The first demo implementation stored mutable toggle values inside copied callback
objects, so repeated content/alignment activation did not advance. It was fixed
using persistent widget/shared demo state, without changing engine callback-copy
lifetime behavior. Focus-reveal acceptance deliberately expands the description
and selects two columns so the target button starts outside the viewport; an
already fully visible target correctly leaves the scroll offset unchanged.

Parallel final regression runs encountered local HTTP asset-loading failures
(`ERR_CONNECTION_REFUSED` and consequent missing host globals). The loopback
server was restarted on the same port with output redirected to
`build/gui-layout-server.log`; the suites were rerun serially against unchanged
served assets. The table above describes the clean serial results, rather than
counting those failed transport runs as acceptance.

## Build and reproduction

Windows desktop Chrome **155.0.8059.80**, headless. Tests use the isolated
`build/gui-layout-site` snapshot at `http://localhost:8158`, with matching main
JS/Wasm, current host files, fixtures and fonts. `scripts/gui-layout-smoke.cjs`
records HTTP-served SHA-256 and byte counts for `webengine.js`, `webengine.wasm`,
`gui.js` and `app.js` in its result JSON. Regression suites run against the same
staged main JS/Wasm identity.

Follow [README build instructions](../README.md) and `.emscripten-version`.
Native tests use the configured WinLibs `mingw64/bin` directory on PATH for
matching runtime DLLs. With the exact served `DEMO_URL`, Playwright/pngjs via
`NODE_PATH`, and Chrome via `BROWSER_EXECUTABLE`, run `scripts/gui-layout-smoke.cjs`
and the theme/widget suites. Force each renderer for the GUI/UI-input/input
scripts, using separate evidence directories. The measured-layout suite forces
both renderer/DPR combinations itself.

Equal columns and measured rows are implemented; spans, per-track sizing,
flex-weight distribution, custom intrinsic-measure hooks and automatic column
policy are not. Existing font shaping/bidi/fallback limits remain. Desktop Chrome,
native semantic checks and Chromium touch emulation do not establish physical
Android/iOS, OS IME, real screen-reader/browser combinations or sustained mobile
performance acceptance.
