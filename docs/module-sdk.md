# Compiled module SDK handoff

M7, 23 October 2026. The SDK is source/CMake composition with one compatible
C++20 toolchain. It supports browser Wasm and native Windows. It does not provide
a cross-compiler binary ABI, DLL/Wasm discovery or live executable unloading.

## Start an independent consumer

Pin a published WebEngine revision as a submodule. A small consumer can add only
`modules/`, then optionally `quests/`; it needs no renderer, Jolt, SDL, Dawn or
asset downloads. Copy `examples/modules` for an ordinary code plugin, or
`examples/quests` for a quest binding module. Pass `WEBENGINE_SOURCE_DIR` to the
checkout rather than copying engine source lists.

```sh
cmake -S examples/modules -B build/sdk-modules -DWEBENGINE_SOURCE_DIR=/path/to/WebEngine -DWEBENGINE_BUILD_MODULE_TESTS=ON
cmake --build build/sdk-modules --config Release
ctest --test-dir build/sdk-modules -C Release --output-on-failure

cmake -S examples/quests -B build/sdk-quests -DWEBENGINE_SOURCE_DIR=/path/to/WebEngine
cmake --build build/sdk-quests --config Release
ctest --test-dir build/sdk-quests -C Release --output-on-failure
```

On Windows use Visual Studio C++ x64. For Wasm, activate the version in
`.emscripten-version` and prefix configuration with `emcmake`; use Ninja and
`-DCMAKE_BUILD_TYPE=Release`. CTest invokes the SDK's Node emulator automatically.
Use separate build trees for native, Wasm and omitted-feature configurations.
`WEBENGINE_BUILD_MODULES=OFF` omits Modules and its consumer; disabling only
`WEBENGINE_BUILD_QUESTS` retains the generic module SDK. The examples provide
explicit disabled consumers and tests for both configurations.

## Implement a reusable code plugin

`webengine_add_module` creates a static C++20 target and public alias. Register
against an application-owned typed registry with `ModuleRegistration<Registry>`.
Declare module ID/version, dependencies, schema providers and required host
capabilities. Module-composition API compatibility is separate from content and
save versions. See [module contracts](modules.md) for exact ranges and limits.

The application supplies registrations explicitly to `ModuleComposition::replace`.
Installation stages a candidate registry and publishes only after validation.
Installers and evaluators must have no external side effects. Capture provider
ownership by value; retain the returned snapshot for every session using it.
Immutable factories/definitions may be shared, but each session owns runtime
state. The counter sample proves non-quest reuse, independent sessions, rejected
replacement and retained old providers. No implicit scheduler or scene owner is
introduced.

## Add quest bindings and game effects

The quest consumer builds `Example::QuestBindings` as a distinct code module. Its
`example.inventory-at-least` objective validates a declared integer fact and
reads that fact through the typed `Evaluation` contract. It has no inventory,
wallet, scene, IO or platform ownership.

The executable composes that registration with `webengine.quests`, creates an
immutable catalogue and two independent runtimes, and supplies accepted game
facts through a batch. It validates effect requests and stages the game wallet
before committing the kernel transaction. Snapshot restore retains the receipt
and emits no second reward. Removing the composition leaves retained catalogues
and their captured providers usable. Its deterministic output is identical on
native and Wasm. Real games must put game data and kernel state in one validated
save envelope, and reconstruct bindings rather than saving callbacks.

Use [quest contracts](quests.md), [host integration](quests-host.md) and
[consumer host boundaries](consumer-hosts.md). Host adapters own persistence,
renderer recovery, audio and OS input. The same application/session policy must
run on browser and Windows. Paws demonstrates all 29 quests with its own puzzle
rules, economy, migration validators and save transport; those policies are not
part of the SDK.

## Release checks and boundaries

`.github/workflows/module-sdk.yml` runs the external consumers and omission
matrix on Linux/Windows native and pinned Emscripten/Node. M7 local Windows x64
MSVC and Wasm checks passed the generic consumer, quest consumer, 71 module
assertions, 242 kernel assertions and both omission configurations. Linux CI
coverage is a workflow gate, not a claim of local Linux acceptance.

For a new published consumer pin, also build the actual generated site, force
WebGPU and WebGL2, and launch the packaged Windows application from an unrelated
working directory with an independent writable profile. Verify original-save
backup, failed-write recovery, second-writer protection, scene travel, pause,
stale commands and file interchange. Console/Node checks alone do not establish
rendered host acceptance. Physical mobile and native macOS/Linux hosts remain
outside the Paws acceptance scope.

M7 general CI also checks the existing scene/effect writer version 34. Five
legacy regression assertions still expected version 31 and are aligned with the
current writer; serialization code and schema compatibility are unchanged.
