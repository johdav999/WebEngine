# Standalone RPG foundation consumer

This consumer links only `WebEngine::RPG` and its public Modules dependency.
It supplies a neutral compiled metadata validator, retains a catalogue across
composition replacement, and allocates IDs in two independent sessions. The additional character_consumer exercises two distinct authored resource and
progression rulesets through R2 transactions. It is not a combat implementation
or rendered example. RPG stays disabled unless selected.

From the WebEngine root:

```sh
cmake -S examples/rpg -B build/rpg-sdk -DWEBENGINE_SOURCE_DIR=/absolute/path/to/WebEngine -DWEBENGINE_BUILD_RPG=ON -DWEBENGINE_BUILD_RPG_TESTS=ON -DWEBENGINE_BUILD_MODULE_TESTS=ON
cmake --build build/rpg-sdk --config Release
ctest --test-dir build/rpg-sdk -C Release --output-on-failure
```

Use MSVC x64 on Windows. For Wasm activate `.tools/emsdk`, prefix configuration
with `emcmake`, add `-G Ninja -DCMAKE_BUILD_TYPE=Release` and choose another build
directory. The emulator is selected by the SDK. No paid services or assets run.
Leaving RPG OFF builds a tiny omission executable; it must not link the kernel.

With tests enabled, CTest also configures separate default-off/all-off consumers
and asserts actionable failures for missing Modules, tests without RPG, and the
undelivered quest/demo switches. The configuration matrix checks for accidental
engine-core, physics, GUI and dependency-download setup. See
[full API/ownership contracts](../../docs/rpg.md) and
[validation](../../docs/rpg-validation.md).

R3 delivers [inventory and equipment transactions](../../docs/rpg-inventory.md),
with [cross-platform validation](../../docs/rpg-r3-validation.md).
The neutral inventory definitions cover weapon/consumable and tool/loadout policies.

R4 adds [abilities, statuses and candidate RNG](../../docs/rpg-abilities.md)
with [native/Wasm evidence](../../docs/rpg-r4-validation.md). The ability sample
provides random damage, item-consuming healing and non-damage resource effects.


R8 adds `save_game.hpp`, a headless example of consumer-owned checkpoint state
and an outer envelope around coordinated RPG/quest snapshots. The sample prepares
and validates a complete replacement before publishing it. See
[snapshot contracts](../../docs/rpg-snapshots.md) for canonical formats, stable-save
boundaries and R9 host responsibilities; no storage or rendered host is added here.
