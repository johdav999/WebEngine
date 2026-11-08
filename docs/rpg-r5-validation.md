# RPG R5 validation

Scope: API 1.4.0 encounter lifecycle, fixed rosters, stable initiative, turn/encounter
epochs, policy outcomes and AP/movement allowances, cooldowns, and transactional
phase progression. See [contracts](rpg-encounters.md) and
[tests](../tests/rpg_encounter_tests.cpp).

The nine-case CTest suite covers both independent consumers, the six-case
configuration matrix, Modules and R1–R5. R5 exercises equal initiative, once-per-round
score evaluation, a full 64-participant round, duplicate/stale EndTurn, foreign/
missing participants, defeated active/future actors, all-ineligible entry/continuation,
start-phase incapacitation, terminal outcomes, entry/exit policy rejection,
cooldown expiry/limits, phase ownership/scope, action/status rollback, work/effect
budget exhaustion, cyclic eligibility, callback failure and reentry. A fixed literal
scripted state/result trace is asserted on every platform, including epochs, phase
counter, RNG, active actor, allowances, outcomes and ordered encounter records.

## Reproduction

Use [standalone build setup](rpg.md#build-without-engine-dependencies).
Windows CI uses MSVC Release, Linux uses the native toolchain, and Wasm uses the
repository-pinned Emscripten. No renderer or paid asset generation is configured.

Local Windows-hosted Wasm: Emscripten 6.0.11, Node 24.19.0, CMake 4.2.1, Ninja.
The isolated checkout is RPGGame/build/rpg-r5/checkout with ../wasm for build output.

```text
cmake -S examples/rpg -B ../wasm -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE=C:/Users/EJOHDAV/source/repos/WebEngine/.tools/emsdk/upstream/emscripten/cmake/Modules/Platform/Emscripten.cmake -DCMAKE_CROSSCOMPILING_EMULATOR=C:/Users/EJOHDAV/source/repos/WebEngine/.tools/emsdk/node/24.19.0_64bit/node.exe -DWEBENGINE_SOURCE_DIR=C:/Users/EJOHDAV/source/repos/RPGGame/build/rpg-r5/checkout -DWEBENGINE_BUILD_RPG=ON -DWEBENGINE_BUILD_RPG_TESTS=ON -DWEBENGINE_BUILD_MODULE_TESTS=ON
cmake --build ../wasm --parallel 2
ctest --test-dir ../wasm --output-on-failure
```

## Results

The local Release Wasm build passed. The full CTest suite passed 9/9 in 94.06
seconds, including the six-case configuration matrix (89.75 seconds). R5 passed
525 checks and its literal state/result trace. Existing R4 (1350 checks), R3
(5012), R2 (288), R1 (52), Modules (71) and both consumer executables passed.
CMake's Emscripten shared-library warning does not affect these static targets.

Implementation commit `5a278455106be171d60ab52116d50711e92af608` passed the
[RPG library workflow](https://github.com/johdav999/WebEngine/actions/runs/40197708322)
on Ubuntu 24.04 native, Windows Server 2022/MSVC Release and pinned
Emscripten/Node Release. All three jobs built and passed the nine CTest cases;
each ran the identical literal R5 state/result trace assertion.
The [Portable module SDK workflow](https://github.com/johdav999/WebEngine/actions/runs/40197708308)
also passed every Linux, Windows and Wasm job, including existing module/quest
consumers and omitted-feature configurations. No local MSVC toolchain was available;
Windows evidence is the successful CI Release build/test, not a local GPU run.

Documentation checks resolved 27 relative links across the R5 contracts/evidence,
public RPG guide and roadmap. `git diff --check` passed. This evidence update
changes documentation only; the implementation SHA above is the tested source.

## Documentation and limits

Updated architecture, roadmap status, API guide, phase contracts and README links.
Updated and verified the existing WebEngine/RPGGame/CafeGame skill packages,
canonical WebEngine engine.md, Johan and EJOHDAV installed engine-skill packages,
and the required Johan CatGame mirror. Updates preserve project/clothing guidance.
Those existing local skill packages are outside this isolated source commit.

RPGGame's engine pin remains 56080eca124c9a62e15b146799a8004bdad5a9b2.
R6 receives committed roster/order/active/epoch/allowance/cooldown state and the
same transaction boundary. Tactical action authorization/spending, spatial queries,
reactions, presentation pending, snapshots and rendered hosts remain later work.
Headless Wasm/Node and native tests do not establish GPU, browser UI or desktop
rendered acceptance. No rendered behavior was changed or claimed in this milestone.
