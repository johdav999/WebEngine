# Optional RPG library (R1-R9)

R1 implements `engine_rpg` / `WebEngine::RPG`: a standard-C++20 static library
depending only on [Modules](modules.md). It supplies typed identities, validated
catalogue metadata, provider registration and independent runtime ownership.
R2 adds [characters, modifiers, progression and shared transactions](rpg-characters.md).
R3 adds [inventory, durable items and equipment transactions](rpg-inventory.md).
R4 adds [abilities, statuses and deterministic randomness](rpg-abilities.md).
R5 adds [encounters, initiative, turn allowances and cooldowns](rpg-encounters.md).
R6 adds [tactical actions, spatial quotes, reactions and presentation gates](rpg-tactics.md).
R7 adds the separately optional [quest bridge and award ledger](rpg-quests.md).
R8 adds [canonical snapshots and safe restore](rpg-snapshots.md).
R9 adds the optional [shared rendered hosts](rpg-hosts.md); the library owns no scheduler or renderer. See later milestones
in the [architecture](rpg-library-architecture.md) and
[release prompts](rpg-library-release-1-prompts.md).

## Build without engine dependencies

From the engine checkout, with CMake 3.22+ and a C++20 compiler:

```sh
cmake -S examples/rpg -B build/rpg-sdk -DWEBENGINE_SOURCE_DIR=/absolute/path/to/WebEngine -DWEBENGINE_BUILD_RPG=ON -DWEBENGINE_BUILD_RPG_TESTS=ON -DWEBENGINE_BUILD_MODULE_TESTS=ON
cmake --build build/rpg-sdk --config Release
ctest --test-dir build/rpg-sdk -C Release --output-on-failure
```

Use MSVC x64 on Windows. For Wasm, activate the repository-local Emscripten
version in `.emscripten-version`, prefix configuration with `emcmake`, use
`-G Ninja -DCMAKE_BUILD_TYPE=Release` and a distinct build directory. CTest uses
the toolchain's Node emulator. R1 is portable code execution, not a rendered game.

The external sample configures only `modules/` and `rpg/`; it does not configure
engine_core, Jolt, GUI, renderer, platform SDKs or dependency downloads. A consumer
uses the public targets and include paths, without copying source lists:

```cmake
set(WEBENGINE_BUILD_RPG ON CACHE BOOL "Use RPG foundation")
add_subdirectory("${WEBENGINE_SOURCE_DIR}/modules" engine-modules)
add_subdirectory("${WEBENGINE_SOURCE_DIR}/rpg" engine-rpg)
target_link_libraries(my_game PRIVATE WebEngine::RPG)
```

Root builds include the same optional `rpg/` subdirectory through `cmake/rpg.cmake`.
All four RPG options default OFF. `WEBENGINE_BUILD_RPG=ON` requires the existing
Modules target. Tests require RPG enabled. `WEBENGINE_BUILD_RPG_QUESTS=ON` additionally requires the existing Quests target.
`WEBENGINE_BUILD_RPG_DEMO=ON` selects the root-only rendered sample and requires
RPGQuests. Standalone kernel consumers reject this host option explicitly.
Omitting RPG creates no RPG target/test and has no effect on other libraries.
This is source/CMake composition with one compatible toolchain, not a binary ABI.

## Public types and metadata

Headers are `engine/rpg/types.hpp`, `bindings.hpp`, `catalog.hpp` and `runtime.hpp`.

- `ContentId` owns a validated, explicitly constructed string. Keys are 1-128
  ASCII bytes, contain at least one dot, and use lowercase letters, digits,
  underscore or hyphen within nonempty segments. Leading/trailing/repeated dots,
  uppercase, whitespace and non-ASCII bytes reject.
- `ContentRevision` is an explicitly constructed positive uint64 selected by the
  application. It is independent of `apiVersion` (currently 1.7.0).
- `SessionId` is a nonzero uint64 durable namespace chosen by the host. The host
  must ensure uniqueness between distinct persistent sessions. No global ID
  registry or random namespace generator is supplied. Restore requires a matching namespace.
- `EntityId` contains a SessionId and a numeric value. The allocator issues positive
  values monotonically, including UINT64_MAX once; zero cursor then means exhausted.
  `EntityIds` supports an explicit initial cursor for construction/testing; it is
  not a save validator. Copying an allocator does not establish a new namespace.
  Runtime's allocator is private and each new Runtime starts at one.
- `Definition` holds content ID, handler ID and exact positive handler version.
  It remains metadata only; R2 adds separate typed CharacterDefinition and
  ProgressionDefinition records, not a generic payload/expression format.
- `Error` contains an `ErrorCode` and readable message. Module graph errors use
  existing `ModuleError`; provider exceptions propagate unchanged.

`Limits` caps keys at 128 bytes, catalogue records at 4096 and registry handlers
at 256. R2 adds character, modifier, producer and transaction caps documented in
[the character contract](rpg-characters.md). Limits reject without truncation. R8 defines a separate version-1 catalogue/snapshot wire format and exact catalogue
compatibility descriptor, independent of module API and application content revision. Runtime IDs are not scene handles.

## Explicit composition and validation

`moduleRegistration()` supplies `webengine.rpg`, API version 1.7.0, with no implicit
handlers or schema claims. Game modules declare a dependency on this registration
and install typed `DefinitionHandler` entries into staging `Bindings`. A handler
has a positive uint32 version and a required `void(const Definition&)` validator.
It validates metadata against an application-owned immutable schema; later stages
will introduce typed domain definitions rather than a universal service locator.

```cpp
#include "engine/rpg/runtime.hpp"
#include <array>
namespace rpg = engine::rpg;

engine::ModuleRegistration<rpg::Bindings> rules;
rules.descriptor.id = "mygame.rules";
rules.descriptor.dependencies.push_back({"webengine.rpg", {}});
rules.install = [](rpg::Bindings& builder) {
    builder.addDefinitionHandler(rpg::ContentId("mygame.marker"),
        {1, [](const rpg::Definition&) { /* pure metadata validation */ }});
};
rpg::Composition composition;
std::array selected{rpg::moduleRegistration(), rules};
auto bindings = composition.replace(selected);
auto catalog = rpg::Catalog::create(
    {rpg::ContentId("mygame.catalog"), rpg::ContentRevision(1),
     {{rpg::ContentId("mygame.start"), rpg::ContentId("mygame.marker"), 1}}}, bindings);
rpg::Runtime session(catalog, rpg::SessionId(1));
auto id = session.allocateEntityId();
```

Bindings reject duplicate handlers, zero versions, empty validators and capacity
overflow before insertion. Modules validate registration IDs, versions, dependency
order and reentry. Failed installers/finalizers preserve the published snapshot.
Installation may mutate only the staging builder and must not retain mutable
references into it. Published bindings are available only through const access.

`Catalog::create` takes owned metadata and a retained module snapshot. It requires
the matching `webengine.rpg` API registration, sorts definitions by ID, checks all
duplicates/handler references/versions and bounds, then invokes validators in that
order. Structural validation finishes before any provider executes. Only complete
success returns a shared const catalogue. Empty catalogues are valid. `find` returns
a borrowed definition or nullptr; borrows live as long as that catalogue does.

Validators are trusted, bounded, synchronous and side-effect-free. They borrow the
definition only for the call; no scene, IO or mutable game/runtime access is allowed.
Const function wrappers cannot enforce purity of captured state. Use owned immutable
captures, never references to installation locals. Catalogue construction is not a
live-state transaction and must not mutate other sessions through a callback.

## Ownership and next milestone

One owner thread composes bindings, constructs catalogues and operates runtimes.
Runtimes are noncopyable/nonmovable, hold a shared const catalogue, and own their
own entity allocator. `allocateEntityId` only reserves an identity; it creates no
actor, character or entity record. `nextEntity` exposes the next cursor for
diagnostics, not save bytes. Exhausted allocation throws without changing state.
R2 adds character state and a shared transaction boundary; see
[command, revision and lifetime contracts](rpg-characters.md). Allocation now
advances revision and invalidates prepared commands/candidates. R4 adds candidate
RNG with a versioned state encoding; there is no event scheduler or full snapshot.

Composition replacement/destruction does not invalidate retained sessions.
Runtime state is destroyed before its catalogue. The catalogue retains immutable
bindings and all installer/provider captures. The SDK destroys registry callbacks
before releasing installer captures in reverse dependency order. Other retained
registrations/snapshots may extend those lifetimes; no dynamic code unload exists.

R2 extends these types with character/resource state, owner/generation/revision
stamps and producer sequencing. R3 reuses that boundary for inventory/equipment,
and R4 for abilities/statuses/RNG. Future full snapshot versions remain separate. The [external consumer](../examples/rpg/consumer.cpp)
proves isolated identity allocation and usable retained definitions. See
[validation evidence](rpg-validation.md) for actual checks and limitations.
