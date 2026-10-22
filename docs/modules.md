# Optional compiled code modules

Implemented M1, 22 October 2026. `engine_modules` / `WebEngine::Modules` is a portable C++20 library with only standard-library dependencies. It supplies validated application composition for optional code plugins. It does not implement quests, an event bus, automatic discovery, dynamic linking, hot unloading or an execution scheduler.

## Consume and extend

The root engine build includes `modules/`; `WEBENGINE_BUILD_MODULES=OFF` omits the target. For a standalone consumer, add `WebEngine/modules` as a subdirectory without configuring the renderer, physics, downloads or engine demos. [The external example](../examples/modules/CMakeLists.txt) is also a copyable template. It builds a non-quest counter plugin with two independent sessions.

```cmake
add_subdirectory("${WEBENGINE_SOURCE_DIR}/modules" engine-modules)
webengine_add_module(my_inventory
  ALIAS MyGame::Inventory
  PUBLIC_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/include"
  SOURCES inventory.cpp
  PUBLIC_DEPENDENCIES WebEngine::Modules)
target_link_libraries(my_game PRIVATE MyGame::Inventory)
```

The helper creates a static C++20 target with public include/dependency propagation and consistent Wasm exception flags. Private dependencies remain private. It does not inspect or copy engine sources. Distribution is source/CMake composition with one compatible toolchain; binary SDK installation and cross-compiler ABI compatibility are not promised.

Use your own typed registry and directly typed services. `ModuleRegistration<MyRegistry>` carries a descriptor and an installer accepting only that registry builder. The application passes registrations explicitly to `ModuleComposition<MyRegistry>::replace`. An optional finalizer validates application/domain invariants before publication. Unknown custom registry entries are the registry's responsibility, not a generic string-based service locator.

```cpp
engine::ModuleComposition<MyRegistry> composition;
std::vector<engine::ModuleRegistration<MyRegistry>> selected{
    inventoryRegistration(), dialogueRegistration()
};
auto installed = composition.replace(selected, hostCapabilities,
    [](const MyRegistry& registry) { registry.validate(); });
// Keep installed alive for every session using its factories/services.
auto game = GameSession(installed);
```

Registries must be default constructible and expose their post-install APIs through const access. Const does not make captured mutable state immutable: builders must publish immutable definitions/factories; each factory creates independent runtime state. The counter sample demonstrates this distinction. There is no implicit tick or subscription; the application explicitly calls its services using the existing Application lifecycle.

## Descriptor and validation contract

`ModuleDescriptor` owns ID, module version, engine API range, dependencies, provided schema versions and required host capabilities. `engineApi` specifically means the module-composition SDK API (`moduleCompositionApiVersion` currently 1.0.0), not a promise about every WebEngine feature. Code-level engine dependencies still compile/link normally. Module, extension schema, game content and saved-state versions are separate identities.

Versions are unsigned major/minor/patch tuples, compared lexicographically. Ranges are inclusive minimum/exclusive maximum, default [1.0.0, 2.0.0). Exact patch compatibility can be expressed with an adjacent upper bound. No implicit major-version wildcard or semver prerelease parsing exists.

Keys are 1–128 ASCII bytes, lowercase letters/digits/underscore/hyphen separated by at least one dot. Leading/trailing/repeated dots, uppercase, slashes and other characters reject. Names should use an owned namespace, such as `studio.inventory`. Each composition permits at most 128 modules; each descriptor permits at most 64 dependencies, schemas and required capabilities; the host permits 64 capabilities. Limits reject rather than truncate.

`validateModules` is pure. It validates keys/ranges/bounds, duplicate module IDs, missing/incompatible dependencies, incompatible host API, duplicate provided schema IDs (even with different versions), duplicate dependencies/capabilities, missing capabilities and cycles. It returns input indices in dependency order, with lexicographically lowest eligible IDs resolving ties. Input order never changes independent-module order. An empty composition is valid.

`ModuleError` supplies `ModuleErrorCode`, owner module ID and readable text. Installer/finalizer exceptions propagate unchanged, with the prior composition preserved. Required capabilities are explicit facts supplied by the host; there is no platform-name hardcoding. Future plugins declare only the capabilities they actually need.

## Ownership, publication and teardown

Composition is synchronous, owner-thread only and non-reentrant. `replace` first validates all descriptors/installers, copies all provider callbacks in dependency order, creates a new private registry, invokes installers, then runs the optional finalizer. Only complete success replaces the current shared snapshot. Allocation, provider-copy, construction, installer or finalizer failure does not publish partial state.

Installers may mutate only the supplied staging registry. External side effects are outside rollback and forbidden by this contract. Callbacks must not publish pointers to a partially built registry or call `replace` recursively. Thread races are not supported; marshal worker results using the application's existing owner-thread/generation mechanisms.

`ModuleSnapshot` exposes const registry and descriptor views. Sessions retain its shared handle for the entire lifetime of objects borrowing provider services. Installer captures remain owned by that snapshot; use owning captures for services needed after installation, never dangling references to installation locals. Registry values are destroyed before retained providers; providers are released in reverse dependency order, including on failed installation. Input registrations or other shared owners may independently extend provider lifetimes.

Replacing/destroying a composition does not invalidate an old snapshot retained by a session. Replacement creates a new registry generation; it does not migrate live state or unload executable code. A session destroys its service instances before releasing its snapshot. Runtime cancellation, scene handles, pending tasks and subscriptions remain the domain module/application's explicit responsibility. No generic async system is introduced.

## Samples and verification

- [counter plugin](../examples/modules/counter/include/counter/counter.hpp): typed factory registry, independent session state and retained snapshot.
- [consumer](../examples/modules/consumer.cpp): external dependency-only integration and retained old session after replacement.
- [Windows host](../examples/modules/windows.cpp): `WebEngine::Desktop`, retained GUI, two rendered bars and controlled smoke mode.
- [browser host](../examples/modules/browser.cpp): the same counter plugin and scene, portable Application plus browser renderer facade. HTML is sample-specific host UI, not part of Modules.
- [tests](../tests/modules_tests.cpp): invalid graphs/ranges/capabilities, deterministic order, failure/reentry atomicity, old snapshot/provider retention and teardown.

See [module validation](modules-validation.md) for commands and scoped evidence. M2 now adds the optional [quest kernel](quests.md) using this composition API; host persistence and Paws save migration remain later milestones.
