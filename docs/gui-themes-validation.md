# GUI theme validation — 2026-10-19

Implemented portable GUI-owned theme defaults, component defaults, partial local
normal/state overrides, dark/light factories, resolved custom-painter styles and
the `?gui=1&guiTheme=1` showcase. Both renderers consume the same styled draw list.
See [theme contracts and migration](gui-themes.md).

## Executed checks

| Check | Result | Evidence |
| --- | --- | --- |
| Native Debug `gui`, `input`, `input_adapter` | 3/3 passed | `build/gui-theme-native-tests.log`; native build logs |
| Repository-local Emscripten 6.0.11 Release `web_assets` | Passed | `build/gui-theme-web-build.log`, `build/gui-theme-web-final.log` |
| Theme suite, forced WebGPU/WebGL2, each at DPR1/DPR2 | Four runs passed; five check groups each; no browser errors | `build/evidence/gui-theme/results.json` and PNG captures |
| Custom widgets/checkbox/slider regression, both renderers at DPR1/DPR2 | Four runs passed; eight groups each | `build/evidence/gui-theme-widgets-regression/results.json` |
| Existing GUI suite on both renderers | 10 groups per backend passed | `build/evidence/gui-theme-gui-webgpu/`, `build/evidence/gui-theme-gui-webgl2/` |
| Existing UI/input integration on both renderers | Seven groups per backend passed | `build/evidence/gui-theme-ui-webgpu/`, `build/evidence/gui-theme-ui-webgl2/` |
| Existing input suite on both renderers | 10 groups per backend passed | `build/evidence/gui-theme-input-webgpu/`, `build/evidence/gui-theme-input-webgl2/` |
| Installed and CatGame engine-skill | Both validators passed; new reference identical; game-specific dependency guidance retained | Both `references/gui-themes.md` files; `build/update-gui-theme-skills.py` |

Native tests cover global/component/inline inheritance, single-field and complete
resets, neutral image tint, checked state, hover/focus/press/disabled precedence,
local state precedence, inherited disabled state, custom-painter effective colors,
once-applied alpha/disabled opacity overrides, theme preservation on invalid
values/kinds/inactive layers, prior draw-list preservation on invalid local edits,
and unchanged control identity, bounds, checked value and focus after replacement.

Browser checks use actual canvas mouse/keyboard input, native accessibility
controls, resolved read-only diagnostics and PNG comparisons. Dark/light changes
leave scene pixels unchanged; local purple styles survive theme replacement.
Labels, images, input, checkbox, slider and the custom preview receive effective
theme values. Disabled pointer/native actions remain blocked, text selection uses
the selected theme, and values/focus/layout survive replacement. UI scale, DPR,
resize and accessible theme activation retain behavior and issue no scene input
deltas. Light/disabled captures on both renderers were visually inspected.

The legacy WebGL2 GUI suite had one second-DPI-page initialization timeout with
no reported browser errors; a focused rerun against the unchanged served build
passed all 10 groups. No product fix or weakened assertion was used for that rerun.

## Tested artifact and commands

Windows desktop Chrome **155.0.8059.80**, headless. The browser used the isolated
`build/gui-theme-site` snapshot at `http://localhost:8157`. The suite records
SHA-256 of HTTP-served assets in `results.json`:

- `webengine.wasm`: 4,464,788 bytes,
  `ff8ab061606db799618c6ba6c7fbe2bd02192dd736b638ba88406e1b3e4b50c2`.
- `webengine.js`:
  `1e41cd568b87a1e025bc7b0d56772dc1791865c3610889b70da505a6d1dc5c9c`.
- `gui.js`:
  `ce8d70742e8b7a383651fe0cfed4936233c5b8ee26d133827f49cb07bca5b7be`.

Use the [README build commands](../README.md) and SDK version from
`.emscripten-version`. Native execution uses the configured WinLibs compiler's
`mingw64/bin` on PATH for matching runtime DLLs. Stage matching generated main
JS/Wasm plus current `web/` adapters, fixture assets and Noto fonts. Serve the
actual selected directory/port. With `DEMO_URL`, `NODE_PATH` (Playwright/pngjs)
and `BROWSER_EXECUTABLE` set, run `scripts/gui-theme-smoke.cjs` and
`scripts/gui-widgets-smoke.cjs`. Force each renderer for existing GUI/UI/input
suites and keep separate evidence directories.

Only appearance colors and opacity are added; no font/layout metrics, theme
serialization or transitions are claimed. Desktop Chrome, AX/native semantic
checks and Chromium touch emulation do not establish real screen-reader/browser,
physical Android/iOS, OS IME or sustained mobile performance acceptance.
