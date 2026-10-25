# RPG release 1 handoff (R10)

Status: **RPG release-1 scope validated by L1**; see [L1 evidence and limitations](rpg-l1-validation.md).
API 1.7.0 and wire version 1 are unchanged. R10 adds consumer evidence and the
adoption contract. L1 closes R10's two rendered CI gates on source `c7db4b4`.

## Architecture audit and supported surface

| Promise | Implementation / evidence | Boundary or limitation |
| --- | --- | --- |
| Optional independent composition | `rpg/CMakeLists.txt`, `examples/rpg`, `rpg_cmake_contract`, `rpg_bridge_off` | Modules only; Quests explicitly optional; no binary ABI |
| Retained immutable definitions, isolated sessions | `catalog.hpp`, `bindings.hpp`, foundation tests | Trusted compiled providers; one owner thread |
| Checked resources, modifiers, progression | `characters.hpp`, character tests | Integral scaled units, additive modifiers; no built-in classes |
| Inventory and equipment | `inventory.hpp`, inventory tests | Slots/capacity, not weight, crafting or arbitrary slot callbacks |
| Abilities, statuses, deterministic RNG | `abilities.hpp`, `random.hpp`, ability tests | Bounded pure proposals; no expression VM |
| Turn policy and lifecycle | `encounters.hpp`, encounter tests | Fixed roster; no summons, delay-turn or simultaneous turns |
| Spatial actions and reactions | `tactics.hpp`, tactical tests | Host reserves destination and freezes competing writers; no continuous physics refunds |
| Quest awards once | `quests.hpp`, quest tests | Coordinator owns both kernels; campaign effects need an enclosing game transaction |
| Canonical restore | `snapshot.hpp`, snapshot/save-game tests | Stable decision boundaries; exact catalogue and handler compatibility |
| Two independent rulesets | `examples/rpg/rulesets.cpp`, `rpg_rulesets` | Scholar vs operator: different units, equipment, effects, initiative, AP and movement |
| Rendered browser and Windows sample | `host_controller.hpp`, `host_scene.hpp`, host tests and L1 evidence | CI WARP/SwiftShader and recorded Intel hardware passed; not a universal driver certification |
| RPGGame adoption | [adoption mapping](rpg-game-adoption.md) | Planned; game pin and runtime unchanged |

The independent consumer builds from its own CMake project and links only public
targets. It does not include private engine implementation headers. The two
rulesets exercise an equipped encounter, tactical ability, stable save/restore
and rejected overdraft without modifying the engine. Operator charge is expressed
in hundredths of joules; precision in hundredths of percent. A socket probe adds
250 precision. Lower precision acts first, pulse costs 200 charge / 2 AP and deals
the caster's precision in charge damage without RNG. Scholar uses integer points,
hand/blade equipment, higher insight first, 1 AP and random strike damage.
This is an executable consumer example, not a second rendered game or AI system.

## Copyable consumer composition

```cmake
cmake_minimum_required(VERSION 3.22)
project(MyRpg LANGUAGES CXX)
set(WEBENGINE_SOURCE_DIR "" CACHE PATH "Pinned WebEngine source")
set(WEBENGINE_BUILD_RPG ON CACHE BOOL "Enable RPG")
set(WEBENGINE_BUILD_QUESTS ON CACHE BOOL "Enable Quests")
set(WEBENGINE_BUILD_RPG_QUESTS ON CACHE BOOL "Enable bridge")
add_subdirectory("${WEBENGINE_SOURCE_DIR}/modules" engine-modules)
add_subdirectory("${WEBENGINE_SOURCE_DIR}/quests" engine-quests)
add_subdirectory("${WEBENGINE_SOURCE_DIR}/rpg" engine-rpg)
add_executable(my_game main.cpp)
target_link_libraries(my_game PRIVATE WebEngine::RPGQuests)
```

For base RPG, omit Quests/bridge options and subdirectory, and link
`WebEngine::RPG`. Do not also add these directories when using the engine root,
which already composes them. All RPG options default OFF. See the compiled
`operatorCatalog()` in [rulesets.cpp](../examples/rpg/rulesets.cpp) for a complete
module registration, version range, provider installation and retained catalogue.
The minimal metadata registration is copyable in [rpg.md](rpg.md).

Construct a runtime only after catalogue validation. Retain provider ownership by
value in registrations. Send commands through one owner thread, keep prepared
results private until commit succeeds, and copy borrowed views before publication.
Use the coordinator when rewards cross RPG/Quests; never expose independently
mutable runtimes. Application/host owns frame advance; camera and animation do not
advance turns. Scene IDs are transient bindings to durable RPG IDs.

## Versions, formats and migrations

API 1.7.0 is source/CMake composition with one C++20 toolchain. No cross-compiler
binary ABI, plugin DLL discovery or automatic source migration is promised.
Future breaking API changes require a major version; additive API extensions a
minor version; compatible fixes a patch. Content revision and handler semantics
are independent: bump handler versions whenever semantics change, including
captured policy data. Never keep a handler version while changing saved rules.

Snapshot/catalogue/coordinator envelopes use wire version 1: explicit LE64 fields,
tags/lengths and FNV-1a integrity checksum. RNG is PCG32 version 1. See the exact
[wire contract](rpg-snapshots.md), including limits and canonical ordering.
Checksums detect accidental corruption, not hostile tampering. Unknown versions
and nonidentical catalogue descriptors reject; there is no implicit migration.
Games retain old source bytes, select a known migration outside the active runtime,
validate the new aggregate, atomically publish it, then rebuild scene bindings.
Never replay reward effects as a substitute for restoring receipts.

Limits reject rather than truncate: 4096 catalogue records, 128-byte keys,
1024 characters and inventories, 64 participants/values/producers, 16384 items
and award receipts, 256 bag slots, 32 equipment slots, 128 statuses per character,
16384 total statuses, 256 effects/reactions/path points, reaction depth 8,
512 result records, 4096 random draws/encounter work, and 16 MiB snapshots.
`Limits` in `types.hpp` and per-domain contracts define additional nested bounds.
The architecture's 128 pending commands is a host ceiling, not a kernel queue;
the rendered example deliberately uses a stricter 32-command queue. Budgeting
cannot preempt malicious compiled callbacks.

## Release checklist

- [x] Public source/CMake targets and complete registration examples.
- [x] Resources/equipment/ability/initiative vary without kernel edits.
- [x] Canonical fixtures and rollback/overflow/stale/reentry/corruption suites.
- [x] Linux/Windows native and pinned Wasm CI includes Modules/Quests regressions,
  default/RPG-off, bridge-off and missing-dependency configurations.
- [x] Independent consumer excludes renderer/physics and dependency downloads.
- [x] API, format, ownership, limits, migration and adoption documentation.
- [x] Rendered-host CI gates green on WARP and SwiftShader; separate local Intel
  hardware acceptance is recorded in L1 evidence.
- [ ] Explicit RPGGame adoption task and validation before advancing its pin.

Deferred: multiplayer/replication, real-time combat, crafting/economy, dialogue
editors, procedural campaigns, full RPG editor, continuous physics combat,
physical mobile acceptance, sustained performance and real driver-reset coverage.
The neutral sample has primitive visuals and manually controlled actors; it does
not implement The Shattered Sea. Consumer adoption remains separate from library
release readiness. The pre-existing general-engine vehicle/spotlight failures are
documented in L1; this is not a claim that every engine test or GPU is certified.
