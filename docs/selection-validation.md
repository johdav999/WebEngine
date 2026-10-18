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

## Precise picking and manipulation extension (2026-10-10)

Fresh validation uses isolated `build/native-precise-selection` (MinGW C++20 Debug)
and `build/wasm-precise-selection` (repository-local Emscripten 6.0.11 Release).
Both final selection and manipulation suites pass natively and under Node/Wasm.
The broader native build passed; its first full CTest run passed 39/40 tests.
The failure was the concurrently added `physics_joints` hinge-limit assertion
(`got -0.360652, expected 0.500000`), outside this picking/manipulation change.
Other checkout work is preserved. The final focused suites were rebuilt and rerun
after the additional edge cases; the broader run is not a claim that all ongoing
checkout changes pass. [Native focused log](../build/native-precise-selection/Testing/Temporary/LastTest.log).

Selection coverage adds empty AABB gaps, two-sided/mirrored/scaled triangles,
small-surface unit normals, barycentric/UV/triangle identity, indexed parts,
degenerate faces and malformed indices, morph/skinning/interpolation, pointer
query forwarding and distance limits. Occlusion cases include noninteractive and
ownerless meshes, blocker/visibility masks, invisible/disabled/pending-destroy
geometry, gaps inside blocker bounds, focus and explicit proxy compatibility.

Manipulation coverage adds plane/axis translation, picked offsets, normalized
rays/axes, behind/parallel rays, pivot rotation and angle wrap, transformed/mirrored
parents, shear rejection, commit/cancel/destructor rollback, competing writes,
reparenting/parent motion, disable/selectability/surface loss, destroyed/recycled
and foreign handles, rigid-body/motor ownership and invalid command rejection.

```powershell
cmake --build build/native-precise-selection --target selection_tests manipulation_tests -j 6
ctest --test-dir build/native-precise-selection -R '^(selection|manipulation)$' --output-on-failure
.tools/emsdk/upstream/emscripten/emcmake.exe cmake -S tests/wasm-selection -B build/wasm-precise-selection -G Ninja -DCMAKE_BUILD_TYPE=Release -DFETCHCONTENT_SOURCE_DIR_JOLT=C:/Users/Johan/source/repos/WebEngine/build/dependencies/JoltPhysics-5.3.0
cmake --build build/wasm-precise-selection --target selection_wasm_tests manipulation_wasm_tests -j 6
.tools/emsdk/node/24.19.0_64bit/node.exe build/wasm-precise-selection/selection_wasm_tests.js
.tools/emsdk/node/24.19.0_64bit/node.exe build/wasm-precise-selection/manipulation_wasm_tests.js
```

No browser adapter, demo input capture or renderer integration was added in this
extension, and `dist/` was not published. Node/Wasm establishes portable execution
on the browser ABI; it does not establish an integrated drag/rotate UI, GPU pixel
agreement, physical touch input or sustained mobile query performance. See the
[application integration and geometric limits](selection.md).
