# Prefab validation

Validated on 2026-10-10 with MinGW GNU C++ 16.1 Debug and the repository-local
Emscripten 6.0.11 Release toolchain.

| Check | Result |
| --- | --- |
| Production core and native prefab contracts | Passed |
| Full native regression suite in `build/native-prefab` | 37/38 passed; `animation_control` failed with `Transition target overflow rejected before entry` in concurrently added animation work |
| Production core and prefab contracts in WebAssembly under Node | Passed |

The prefab contracts cover definition ownership, nested actors/components/plain
nodes, nearest-actor ownership, destination-relative placement, shared geometry,
materials, textures, skeletons/clips, collision shapes and effect definitions;
independent material overrides, morph weights, animation skin poses/playback and
particle playback; stable-key overrides and false flags; disabled ancestors;
ordinary lifecycle/cleanup; tick-time additions; deferred destruction, generation
recycling and cross-scene handle invalidation; factory versions and reentrant
registry removal; invalid configuration, null/throwing factories, malformed
factory-created children and atomic built-in registration; hierarchy budgets,
required skeletal children and built-in instance archive deduplication.

Native commands (put the configured compiler's `bin` directory on PATH for Ninja
and the MinGW runtime; cached Jolt 5.3.0 source was used here):

```powershell
cmake -S . -B build/native-prefab -G Ninja -DCMAKE_BUILD_TYPE=Debug -DFETCHCONTENT_SOURCE_DIR_JOLT=C:/Users/Johan/source/repos/WebEngine/build/dependencies/JoltPhysics-5.3.0
cmake --build build/native-prefab -j 6
ctest --test-dir build/native-prefab --output-on-failure --timeout 60
ctest --test-dir build/native-prefab -R '^prefab$' --output-on-failure
```

A focused harness can build the production core without unrelated native test
targets. This was used while other features were being added in the shared checkout:

```powershell
cmake -S tests/wasm-prefab -B build/prefab-native-harness -G Ninja -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=OFF -DFETCHCONTENT_SOURCE_DIR_JOLT=C:/Users/Johan/source/repos/WebEngine/build/dependencies/JoltPhysics-5.3.0
cmake --build build/prefab-native-harness --target prefab_wasm_tests -j 6
build/prefab-native-harness/prefab_wasm_tests.exe
```

The same test source runs through the production core in WebAssembly without
publishing the playground to `dist/`:

```powershell
.tools/emsdk/upstream/emscripten/emcmake.exe cmake -S tests/wasm-prefab -B build/wasm-prefab -G Ninja -DCMAKE_BUILD_TYPE=Release -DFETCHCONTENT_SOURCE_DIR_JOLT=C:/Users/Johan/source/repos/WebEngine/build/dependencies/JoltPhysics-5.3.0
cmake --build build/wasm-prefab --target prefab_wasm_tests -j 6
node build/wasm-prefab/prefab_wasm_tests.js
```

Both final contract runners printed `Prefab tests passed`. The native full-suite
log is `build/native-prefab/Testing/Temporary/LastTest.log`. These results cover
portable scene construction and WebAssembly execution. This change adds no
browser adapter, renderer or demo controls, so no new browser rendering/input
acceptance or physical mobile-device/performance evidence is claimed. Definition
serialization and asynchronous asset preparation are outside this module's scope.
