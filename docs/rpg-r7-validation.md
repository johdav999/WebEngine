# RPG R7 validation

Scope: API 1.6.0, optional `WebEngine::RPGQuests`, private-runtime coordinator,
typed facts/events and reward mappings, generic award ledger, exactly-once grants
and replay suppression. See [contracts and atomic publication proof](rpg-quests.md)
and [bridge tests](../tests/rpg_quest_tests.cpp).

The suite includes R1–R7, existing Modules and Quests tests, both independent RPG
consumers, the seven-case disabled/invalid configuration matrix and a separate
base-RPG build/run with Quests and RPGQuests disabled. The latter compiles the
complete base library and has its own 300-second timeout; the existing disabled
consumer matrix retains its 180-second timeout.

R7 exercises XP plus item rewards, a full bag rejecting a compound award, receipt
and input preservation, retry, replay suppression, duplicate publication, foreign
commands/candidates, stale RPG/quest/queue revisions, callback exception/reentry,
explicit fact/event projection, both evaluator and reward operation budgets,
ledger validation/16384-entry capacity, and R6 spatial publication/recovery through
the coordinator. Existing quest implementation and tests are unchanged.

## Reproduction

The RPG CI workflow now explicitly enables Quests, quest tests and the bridge on
Windows/MSVC Release, Linux native and pinned Emscripten/Node. Base independence
is checked by the separate omission build. No renderer or paid generation is used.

Local Windows-hosted Wasm: Emscripten 6.0.11, Node 24.19.0, CMake 4.2.1, Ninja.
Checkout RPGGame/build/rpg-r7/checkout; build output ../wasm.

```text
cmake -S examples/rpg -B ../wasm -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE=C:/Users/EJOHDAV/source/repos/WebEngine/.tools/emsdk/upstream/emscripten/cmake/Modules/Platform/Emscripten.cmake -DCMAKE_CROSSCOMPILING_EMULATOR=C:/Users/EJOHDAV/source/repos/WebEngine/.tools/emsdk/node/24.19.0_64bit/node.exe -DWEBENGINE_SOURCE_DIR=C:/Users/EJOHDAV/source/repos/RPGGame/build/rpg-r7/checkout -DWEBENGINE_BUILD_RPG=ON -DWEBENGINE_BUILD_RPG_TESTS=ON -DWEBENGINE_BUILD_MODULE_TESTS=ON -DWEBENGINE_BUILD_QUESTS=ON -DWEBENGINE_BUILD_QUEST_TESTS=ON -DWEBENGINE_BUILD_RPG_QUESTS=ON
cmake --build ../wasm --parallel 2
ctest --test-dir ../wasm --output-on-failure
```

## Results

Local Windows-hosted WebAssembly Release build passed all 13 CTest cases in
158.19 seconds, including the base-RPG omission build and unchanged quest suite.
Published platform checks are pending. Native Windows uses CI because no local
MSVC toolchain is available.

## Documentation and remaining scope

Architecture, target/dependency documentation, roadmap and bridge contracts are
updated. Existing installed engine-skill and required CatGame guides are updated
separately while preserving their local context.

RPGGame stays pinned to 56080eca124c9a62e15b146799a8004bdad5a9b2. R8 receives
RPG state and award dispositions, quest receipts/source sequences, retained
catalogues and the coordinator's publication boundary. Full combined snapshots,
restore/migrations and save storage are not implemented in R7. Rendered browser
and Windows integration remains R9; headless results do not establish GPU acceptance.
