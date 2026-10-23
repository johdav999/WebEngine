# Compiled-module M1 validation

22 October 2026. Working-tree acceptance based on engine HEAD `f4cae84a2ebef51a2e2449409653f6e2901eb211`, with concurrent engine changes retained. This is not a published engine revision or a migrated Paws build. Contracts are in [modules.md](modules.md).

## Results

| Check | Evidence/result |
| --- | --- |
| MSVC 19.51, Release module tests | 71 checks passed |
| Pinned Emscripten 6.0.11 / Node Wasm module tests | Same 71 checks passed |
| Standalone external consumer | Native and Wasm pass independent sessions, reset, rejected replacement and old-provider retention |
| Disabled module | Native and Wasm build/run `module_disabled` without a modules target |
| Chrome 155.0.8059.85 WebGPU | Actual HTML button inputs, A=2/B=1, invalid replacement preserved, A reset leaves B=1; 1,417 changed canvas pixels; no page errors |
| Same Chrome WebGL2 | Same state and pixel checks, 1,417 changed pixels; no page errors |
| Actual Windows DesktopHost, Dawn | Sample rendered, state/replacement/reset/pause/shutdown passed; 3,088 changed scene pixels below GUI |
| Actual Windows DesktopHost, direct D3D12 | Same checks; 3,088 changed scene pixels below GUI |

Native smoke invokes the same command functions as GUI callbacks programmatically; it does not claim native pointer automation. Rendered images were inspected. The generic module contract has no renderer dependency and no platform-specific module algorithm. Host integration is exercised through the two rendered counter sessions, not inferred from console tests.

Saved local evidence: `build/evidence/modules/report.json`, `webgpu.png`, `webgl2.png`, `native-pixels.json`, and `windows-dawn` / `windows-d3d12` capture directories. Browser artifact: `build/web-release/module-site/module_demo.wasm`, SHA-256 `2b249625986063021ae994c56b5c9a1684b6f9c502f89ac32a2426d285baac9b`. Paws migration baselines and their hashes are independently retained in CatGame `tests/fixtures/quest-migration-m0`.

## Reproduce

From the engine repository, with Visual Studio C++ tools and the repository-pinned Emscripten SDK:

```powershell
cmake -S examples/modules -B build/modules-native -G "Visual Studio 18 2026" -A x64 -DWEBENGINE_SOURCE_DIR="$PWD" -DWEBENGINE_BUILD_MODULE_TESTS=ON
cmake --build build/modules-native --config Release --parallel 4
ctest --test-dir build/modules-native -C Release --output-on-failure

./.tools/emsdk/upstream/emscripten/emcmake.exe cmake -S examples/modules -B build/modules-wasm -G Ninja -DWEBENGINE_SOURCE_DIR="$PWD" -DWEBENGINE_BUILD_MODULE_TESTS=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build/modules-wasm --parallel 4
node build/modules-wasm/engine-modules/module_tests.js
node build/modules-wasm/module_consumer.js
```

For feature omission, configure separate native and Emscripten directories using the same standalone project with `-DWEBENGINE_BUILD_MODULES=OFF`; build/run `module_disabled`. Never reuse the enabled tree as the omission proof.

Rendered samples use normal engine setup/dependencies. Configure the root with `WEBENGINE_BUILD_MODULE_DEMO=ON` and, optionally, `WEBENGINE_BUILD_MODULE_TESTS=ON`:

```powershell
cmake -S . -B build/windows -DWEBENGINE_BUILD_WINDOWS=ON -DWEBENGINE_BUILD_MODULE_DEMO=ON -DWEBENGINE_BUILD_MODULE_TESTS=ON
cmake --build build/windows --config Release --target module_demo_windows module_tests --parallel 4
./build/windows/Release/module_demo_windows.exe --smoke build/evidence/modules/windows-dawn
./build/windows/Release/module_demo_windows.exe --d3d12 --smoke build/evidence/modules/windows-d3d12
python tools/verify_module_pixels.py build/evidence/modules

./.tools/emsdk/upstream/emscripten/emcmake.exe cmake -S . -B build/web-release -G Ninja -DCMAKE_BUILD_TYPE=Release -DWEBENGINE_BUILD_MODULE_DEMO=ON -DWEBENGINE_BUILD_MODULE_TESTS=ON
cmake --build build/web-release --target module_demo module_tests --parallel 4
```

Serve only `build/web-release/module-site` on an unused localhost port. `MODULE_URL` selects that URL in `node scripts/modules-smoke.cjs`; default is `http://localhost:8146`. `NODE_PATH` may point to existing Playwright/pngjs packages; `BROWSER_EXECUTABLE` may select installed Chrome. Pixel verification uses Pillow. No test dependency is a runtime dependency.

The local validation reused existing engine build directories but only built the explicit module targets; it did not publish the playground or change Paws' served build. The sample HTML supplies the renderer's status/stats/performance callbacks. Missing callbacks caused an initial startup failure and were fixed before the successful evidence above. Node/Playwright package and browser paths were explicitly configured after the environment lacked default installations.

## Limits

No quest execution, schema migration of live Paws saves, Paws desktop port, dynamic binary loading, live executable unload, native mobile, macOS/Linux, device-failure recovery or sustained performance acceptance is implied. A clean pinned-release consumer check follows deliberate engine publication in the later integration milestone.
