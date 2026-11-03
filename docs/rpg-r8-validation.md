# RPG R8 validation

Scope: API 1.7.0, canonical catalogue/RPG snapshot wire version 1, strict detached
validation, standalone restore, coordinated RPG/quest restore and sample-owned
outer envelope. See [contracts](rpg-snapshots.md).

## Reproduction

Local Windows-hosted Wasm uses Emscripten 6.0.11, Node 24.19.0, CMake 4.2.1 and
Ninja. From the isolated RPGGame/build/rpg-r8/checkout:

```text
cmake -S examples/rpg -B ../wasm -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE=C:/Users/EJOHDAV/source/repos/WebEngine/.tools/emsdk/upstream/emscripten/cmake/Modules/Platform/Emscripten.cmake -DCMAKE_CROSSCOMPILING_EMULATOR=C:/Users/EJOHDAV/source/repos/WebEngine/.tools/emsdk/node/24.19.0_64bit/node.exe -DWEBENGINE_SOURCE_DIR=C:/Users/EJOHDAV/source/repos/RPGGame/build/rpg-r8/checkout -DWEBENGINE_BUILD_RPG=ON -DWEBENGINE_BUILD_RPG_TESTS=ON -DWEBENGINE_BUILD_MODULE_TESTS=ON -DWEBENGINE_BUILD_QUESTS=ON -DWEBENGINE_BUILD_QUEST_TESTS=ON -DWEBENGINE_BUILD_RPG_QUESTS=ON
cmake --build ../wasm --parallel 2
ctest --test-dir ../wasm --output-on-failure
```

The RPG CI workflow builds Release on Windows/MSVC and pinned Emscripten, plus
Linux native. Both new suites run alongside R1–R7, Modules, unchanged Quests,
independent consumers, disabled/invalid option matrix and bridge-off base build.
The checked-in full byte fixture is compared exactly on every platform.

## Results

Local Windows-hosted WebAssembly Release build passed all 15 CTest cases in
170.95 seconds. Focused results: 4,355 snapshot checks, 136 combined-save checks,
and exact comparison of the 3,965-byte canonical fixture. The bridge-off build
passed in 68.12 seconds; the disabled/invalid configuration matrix passed in
96.77 seconds. Patch whitespace and 65 relative documentation links passed.
Published implementation: `8e6d28e166e8fc5351e6a7e1d4acafa2573d5935`.

- [RPG library CI](https://github.com/johdav999/WebEngine/actions/runs/40202819123):
  Windows/MSVC Release, Linux native and WebAssembly all passed, including the
  exact 3,965-byte fixture comparison on each platform.
- [Portable module SDK CI](https://github.com/johdav999/WebEngine/actions/runs/40202819117):
  Windows, Linux and WebAssembly all passed.

These results cover the RPG and portable module workflows, not the unrelated
root renderer workflow.

Existing engine-skill entry points and reference guides were updated and verified
in WebEngine, RPGGame, CafeGame, the required Johan installation and CatGame mirror,
and the EJOHDAV user installation, preserving their host-specific guidance.

## Coverage and handoff

Tests cover catalogue/snapshot round trips, content/binding mismatches, inventory
and equipment corruption, turn references, exhausted counters, stale commands/
transactions/spatial quotes, RNG continuation, presentation-pending save rejection,
all truncated snapshot prefixes, bounded malformed/checksummed inputs, combined
second-kernel failure, receipt disagreement, queue preservation/clearing, source
watermarks and reward/replay receipts after reload. The sample also proves that
outer checkpoint state loads with both runtimes and survives corrupt-load failure.

RPGGame remains pinned to 56080eca124c9a62e15b146799a8004bdad5a9b2. No scene archive
version, storage policy or consumer engine pin changes. Native Windows relies on
CI because local MSVC is unavailable. Rendered hosts, storage adapters and GPU
checks belong to R9 and remain unverified here.
