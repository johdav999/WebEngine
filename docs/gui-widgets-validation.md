# Custom GUI widget validation — 2026-10-19

Delivered portable `Widget` painting, pointer/key, activation and accessibility
hooks; scoped `GuiPainter`; built-in `Checkbox` and horizontal stepped `Slider`;
native browser checkbox/range mirrors; and `?gui=1&guiSettings=1`, including an
application-defined painted reset preview. See [contracts](gui-widgets.md).
Settings update local preview state rather than an audio bus.

## Executed checks

| Check | Result | Evidence |
| --- | --- | --- |
| Native Debug `gui`, `input`, `input_adapter` | 3/3 passed | CTest execution; `build/gui-widgets-native-final.log` records the build |
| Repository-local Emscripten 6.0.11 Release `web_assets` | Passed | `build/gui-widgets-web-verified.log` |
| Settings/custom widgets on forced WebGPU and WebGL2, each at DPR1 and DPR2 | Four runs passed, eight check groups per run; no browser errors | `build/evidence/gui-widgets/results.json` and PNG captures |
| Existing GUI acceptance on forced WebGPU and WebGL2 | 10 groups per backend passed | `build/evidence/gui-widgets-final-gui-webgpu/`, `build/evidence/gui-widgets-final-gui-webgl2/` |
| Existing UI/input integration on both backends | Seven groups per backend passed; no browser errors | `build/evidence/gui-widgets-final-ui-webgpu/`, `build/evidence/gui-widgets-final-ui-webgl2/` |
| Existing input suite on both backends | Passed, including Chromium touch/DPR emulation and GUI text isolation | `build/evidence/gui-widgets-final-input-webgpu/`, `build/evidence/gui-widgets-final-input-webgl2/` |
| Installed and CatGame engine-skill copies | Both validators passed; new GUI reference identical; CatGame dependency guidance retained | `build/update-gui-widget-skills.py`; each copy's `references/gui-widgets.md` |

Native contracts cover custom first-frame paint/clip/scale/input coordinates,
capture outside bounds, cancellation, semantic identity, generic activation,
checkbox repeat/outside/cancel behavior, slider scrolling arbitration, keyboard
navigation, clamping/snapping/endpoints, invalid-state preservation and
inactive/stale semantic actions. Hidden/disabled/clipped captures are cancelled;
disabling an ordinary press also clears pending scroll ownership.

Settings browser checks use real canvas events and pixels, native semantic
actions and Chrome's AX tree. They verify changed checkbox/slider/preview pixels,
unchanged scene pixels, no scene action deltas, diagonal/outside slider capture,
Escape cancellation, checkbox scroll takeover, custom pointer/keyboard
activation, UI scaling independent of DPR, and resized canvas layout. Initial
and scaled captures were inspected; the settings panel clears the host's camera
buttons and footer. Content overflow remains deliberately scrollable.

## Tested build and reproduction

Chrome **155.0.8059.80**, Windows desktop, headless. Browser tests used the
isolated `build/gui-widgets-site` snapshot served at `http://localhost:8156`,
with matching main JS/Wasm, current host adapters, fixture assets and fonts.
`scripts/gui-widgets-smoke.cjs` records SHA-256 of the HTTP-served files:

- `webengine.wasm`: 4,426,263 bytes,
  `14281ef4b95377905f0e51ce930b2f3955ea625fa59c85767c4023c344b606b5`.
- `webengine.js`:
  `4c509db3f24113ea7a9b2502b582d922696889f05889778e57769cd1a8702096`.
- `gui.js`:
  `ce8d70742e8b7a383651fe0cfed4936233c5b8ee26d133827f49cb07bca5b7be`.

Build using the [README](../README.md) and local SDK pinned by
`.emscripten-version`; stage matching host/generated files and fonts. Configure
Playwright/pngjs via `NODE_PATH`, Chrome via `BROWSER_EXECUTABLE`, and the actual
server URL via `DEMO_URL`. Run `scripts/gui-widgets-smoke.cjs`; run the existing
GUI, UI/input and input scripts with each renderer forced. The new suite always
tests both renderer/DPR combinations. Use distinct evidence directories for the
existing suites.

The native input executable initially failed to load a runtime entry point
(`0xc0000139`) with the ambient PATH. All three native suites passed after
prepending the configured WinLibs compiler's `mingw64/bin` directory to PATH.
The legacy GUI browser script initially asserted WebGPU-only retained GUI
resource-byte counters on WebGL2. It now verifies that backend's actual GUI
upload/draw counters; the complete WebGL2 GUI suite then passed. No renderer
resource-accounting functionality was added.

Desktop checks and Chromium touch emulation do not establish physical
Android/iOS, real screen-reader/browser combinations, OS IME, or sustained mobile
performance acceptance. Existing font shaping/bidi/fallback and single-line
editing limitations remain. Custom measurement, tree removal, theme systems and
additional controls were not part of this change.
