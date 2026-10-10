# Persistence validation — 2026-10-10

Implemented contracts are in [persistence.md](persistence.md). All evidence below
was produced during this change on Windows; the shared checkout contained other
feature work, which was preserved. Tests used isolated build/site directories.

| Check | Actual result |
| --- | --- |
| Native Debug build, `build/native-persistence` | Built successfully; full CTest run passed 38/38 suites |
| `persistence_tests` | 41 checks: saved IDs, disabled/forward references, recreated handles, codecs, schema migration, staged failures, exact reserialization, no load-time ticks, versions 2/3 upgrade and application budgets |
| Existing `scene_archive_tests` | Passed built-in resource/scene/physics/animation tests, version-1 fixtures, structural mutations and real Knarr round trips |
| Emscripten 6.0.11 Release, `build/web-persistence-release` | Browser runtime and optional `persistence_wasm_tests` built successfully using repository-local SDK |
| Node/Wasm persistence runner | Same 41 portable checks passed on the 32-bit Wasm ABI |
| Chrome 154.0.8037.99 storage/render smoke | 37 checks passed, no page/console errors; actual IndexedDB and localStorage, owned bytes, ordered operations, namespace/key isolation, cancellation rollback, quota recovery and malformed records |
| Browser checkpoint | 90,680,069 bytes physically downloaded from IndexedDB; exact WebGPU canvas pixels restored before and after a fresh page |
| Native/browser interchange | Native `scene_archive_tests --load` decoded and reserialized that actual browser checkpoint successfully at the same byte size |
| Mobile layout | Chromium 390×844 emulation showed visible browser-save controls and no horizontal overflow |
| JavaScript | Syntax checks passed for storage adapter, archive workflow and smoke script |

Evidence is in `build/evidence/persistence/results.json`, `before-save.png`,
`after-restore.png`, `mobile.png` and `checkpoint.wescene`. Pixel comparisons hide
HTML overlays, whose scene-description text changes after restoration, and compare
all actual canvas RGB bytes exactly. The final smoke verifies a stored malformed
checkpoint preserves the active scene/canvas, then explicitly deletes the checkpoint.

Reproduce with the README native setup and these focused targets:

```powershell
cmake --build build/native-persistence
ctest --test-dir build/native-persistence --output-on-failure

# Configure the optional runner in the same repository-local SDK build:
.tools/emsdk/upstream/emscripten/emcmake.exe cmake -S . -B build/web-persistence-release -G Ninja -DCMAKE_BUILD_TYPE=Release -DWEBENGINE_BUILD_PERSISTENCE_WASM_TESTS=ON
cmake --build build/web-persistence-release --target webengine persistence_wasm_tests
.tools/emsdk/node/24.19.0_64bit/node.exe build/web-persistence-release/persistence_wasm_tests.js

$env:NODE_PATH = 'path/to/test/node_modules' # playwright and pngjs
$env:BROWSER_EXECUTABLE = 'C:/Program Files/Google/Chrome/Application/chrome.exe'
$env:SITE_DIR = 'build/persistence-browser-site' # Or dist, after publishing the desired build.
node scripts/persistence-smoke.cjs
```

The isolated browser site contains `web/`, the selected build's `site/` outputs,
the three bundled model fixtures and Noto font assets. The smoke owns and closes
its loopback server and browser. It avoids converting the large checkpoint into
a JSON array through the test protocol; the browser downloads a Blob instead.
On this machine native tests needed the selected WinLibs compiler's `bin` first on
the process PATH so Git's older `libstdc++-6.dll` was not loaded. The original
shared `build/native` had locked Ninja recompaction files, so it was not cleaned
or reset. An early test assertion included changing HTML scene labels; the final
comparison uses only renderer pixels with bounded failure output.

The tests establish this Windows/Chrome configuration and Wasm/native data contracts.
They do not establish physical Android/iOS acceptance, sustained mobile performance,
other browsers, power-loss durability or immunity to browser eviction/user clearing.
Custom codec allocations and application side effects remain application-owned;
multi-tab merge/conflict handling and autosave timing are explicit future policy.
