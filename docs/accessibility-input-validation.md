# Accessibility and input validation — 2026-10-22

The implementation was built with MSVC/Windows SDK, SDL 3.4.18, and the pinned
repository-local Emscripten 6.0.11 Release toolchain. Shared-checkout rendering and
authoring changes were preserved. The browser site was copied to the task-specific
`build/accessibility-site` directory before acceptance.

Browser Chrome 155.0.8059.85 results:

| Check | Evidence | Result |
| --- | --- | --- |
| GUI controls, accessibility roles/actions and pixels; WebGPU/WebGL2, DPR1/2 | `build/evidence/accessibility-widgets/results.json` | 8 groups per configuration, no browser errors |
| Semantic tree, Unicode textbox, keyboard focus, scroll reveal and Chromium touch; WebGPU | `build/evidence/accessibility-ui-webgpu/result.json` | 7 groups, no browser errors |
| Same UI/input checks; forced WebGL2 | `build/evidence/accessibility-ui-webgl2/result.json` | 7 groups, no browser errors |
| Input adapter contexts, virtual controls, pen pressure and coordinate-free capture cancellation | `node --test tests/input-adapter-tests.cjs` | 5 groups passed |
| Mouse/keyboard, one/two-finger camera gestures, contact transitions, cancellation and virtual controls; each backend | `build/evidence/accessibility-input-webgpu/result.json`, `build/evidence/accessibility-input-webgl2/result.json` | 10 groups each, no browser errors |

The UI/input script retains a historical “WebGPU pixels” label in one check even
when its URL forces WebGL2; the separate widget suite verifies actual selected
backends. Browser artifact hashes and screenshots are in the widget evidence.

Native final CTest run: **9/9 passed**, 204.26 seconds. Evidence:
`build/evidence/accessibility-native-tests.txt` and
`build/windows/Testing/Temporary/LastTest.log`.

| Suite | Verified |
| --- | --- |
| `desktop_accessibility` | Actual Windows UIA HWND discovery, screen geometry, Invoke/Toggle/Value/RangeValue, Unicode and single-line normalization, focus, disabled and hidden targets, retained-provider teardown |
| `desktop_pointer`, `desktop_pointer_d3d12` | Distinct 64-bit SDL contact identities, pressure, DPI/backing mapping, GUI ownership, cancellation/reentrant callbacks, pen hover/tip/barrel/eraser/proximity, malformed contact rejection; actual host scene pointer delivery, GUI activation and synthetic mouse suppression on Dawn/D3D12 |
| `desktop_api`, `desktop_api_d3d12` | Host lifecycle, portable API and rendering integration on both native backends |
| `desktop_text` | Native Unicode selection, clipboard, preedit/commit and focus cancellation |
| `gui`, `input`, `input_adapter` | Portable GUI/input contracts and browser adapter regression |

An initial teardown assertion exposed a Windows client behavior: a disconnected
Value getter may return success with an empty default, while SetValue returns a
failure. The final test checks both absence of stale text and rejected edits.
Providers explicitly disconnect on removal/destruction and retain no GUI borrows.
An initial browser input invocation combined a query-bearing DEMO_URL with the
script's GUI suffix; the final successful runs use a clean base URL and the
supported RENDERER setting, which asserts the active backend.

Physical touchscreen/pen hardware, Narrator/NVDA speech output, physical mobile
devices, installed IME candidate UI and broad Windows driver/DPI matrices were not
tested. No complete accessibility compliance claim follows from these checks.
