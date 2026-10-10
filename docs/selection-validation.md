# Selection and interaction validation

Validated on 2026-10-10 in this checkout, using an isolated MinGW C++20 Debug
build and the pinned Emscripten 6.0.11 Release toolchain.

| Check | Result |
| --- | --- |
| Native regression suite | 31/31 passed in `build/native-selection` |
| Final selection contracts | Passed natively and in WebAssembly under Node |
| WebAssembly production-core build and test-harness link | Passed |

The selection contracts cover nearest/ranged rays, mirrored/scaled/rotated bounds,
singular objects, mesh/proxy/morph/skeletal bounds, physics interpolation, nested
ownership, selection/visibility masks, camera clipping and offset viewports,
orthographic rays, state publication, listener reentrancy, deferred destruction,
generation recycling, action eligibility/execution/self-removal/replacement,
instigator isolation, ordered high-DPI input/cancellation, stationary pointer
repicking, and scene-archive round trips with inert restored actions.

Native commands (include the compiler's `bin` directory in PATH for its runtime):

```powershell
cmake --build build/native-selection -j 6
ctest --test-dir build/native-selection --output-on-failure --timeout 60
```

The reusable Wasm harness builds the production core and runs the same contracts,
without publishing to `dist/`:

```powershell
.tools/emsdk/upstream/emscripten/emcmake.exe cmake -S tests/wasm-selection -B build/wasm-selection -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/wasm-selection --target selection_wasm_tests -j 6
node build/wasm-selection/selection_wasm_tests.js
```

The configure can use `-DFETCHCONTENT_SOURCE_DIR_JOLT=<local Jolt 5.3.0 source>`
when dependencies are already cached. This validation used
`build/dependencies/JoltPhysics-5.3.0`.

These checks establish the reusable engine contracts, native regressions and
WebAssembly execution. Applications must route their active camera/view input
into `processInput`, bind game actions and draw selection feedback as described
in [the integration guide](selection.md). Browser GUI routing, visual highlights
and physical touch-device acceptance are application integration checks.
