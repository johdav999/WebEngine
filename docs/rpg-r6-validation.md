# RPG R6 validation

Scope: API 1.5.0 tactical ability/item/move commands, checked AP/movement/resource
costs, immutable spatial quotes and world-stamp publication, bounded prepared
reactions and presentation acknowledgements/recovery. See
[contracts](rpg-tactics.md) and [tests](../tests/rpg_tactical_tests.cpp).

The ten-case CTest suite covers both independent consumers, Modules, R1–R6 and
the six-case omission/configuration matrix. R6 tests blocked/missing paths,
occupancy/residency, mismatched endpoints, target query rejection, stale/foreign
quotes and world identity/revision, omitted publication stamps, duplicate/stale
actions/acks, pending input gating, recovery, costs/cooldowns/item consumption,
reaction initiative/token ordering, nested chains, cycle suppression, budget
exhaustion, RNG/item rollback, callback failure/reentry and terminal outcomes.
A fixed literal tactical state/result trace is asserted on each platform.

R5's raw combat UseAbility rejection assertion is intentionally updated for the
R6 API: raw UseAbility now rejects in encounters; the tactical wrapper owns action
authorization. R4 exploration abilities retain their existing contract/tests.

## Reproduction

Use [standalone setup](rpg.md#build-without-engine-dependencies) with the RPG and
module tests enabled. Windows CI uses MSVC Release; Linux and pinned Wasm have
their own build/test jobs. No renderer/physics download or paid generation is used.

Local Windows-hosted Wasm: Emscripten 6.0.11, Node 24.19.0, CMake 4.2.1, Ninja.
Checkout: RPGGame/build/rpg-r6/checkout; build output: ../wasm.

```text
cmake -S examples/rpg -B ../wasm -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE=C:/Users/EJOHDAV/source/repos/WebEngine/.tools/emsdk/upstream/emscripten/cmake/Modules/Platform/Emscripten.cmake -DCMAKE_CROSSCOMPILING_EMULATOR=C:/Users/EJOHDAV/source/repos/WebEngine/.tools/emsdk/node/24.19.0_64bit/node.exe -DWEBENGINE_SOURCE_DIR=C:/Users/EJOHDAV/source/repos/RPGGame/build/rpg-r6/checkout -DWEBENGINE_BUILD_RPG=ON -DWEBENGINE_BUILD_RPG_TESTS=ON -DWEBENGINE_BUILD_MODULE_TESTS=ON
cmake --build ../wasm --parallel 2
ctest --test-dir ../wasm --output-on-failure
```

## Results

The local Wasm Release build and full CTest suite passed 10/10 in 84.71 seconds,
including the configuration matrix (79.59 seconds). R6 passed 299 checks and its
fixed state/result trace; R1–R5, Modules and both consumers also passed. The
Emscripten/CMake shared-library warning does not affect these static targets.
Documentation checks resolved 30 relative links, and `git diff --check` passed.

Published Windows/Linux/Wasm CI evidence is pending. No local MSVC toolchain was
available; native Windows validation uses the CI Release build/test job.

## Documentation and remaining work

Architecture, roadmap, API/navigation and R4/R5 migration notes accompany the
new R6 contracts. Existing installed engine-skill and required CatGame guides
are updated separately while preserving their local context. Verified existing
WebEngine/RPGGame/CafeGame skill packages, the canonical WebEngine engine.md,
Johan and EJOHDAV installed skills, and the required Johan CatGame mirror.

RPGGame remains pinned to 56080eca124c9a62e15b146799a8004bdad5a9b2. R7 receives
the same transaction boundary, with explicit world-stamp preflight for spatial
candidates. Snapshot validation is R8; host route reservation, frozen writers,
visual playback and recovery are documented R9 responsibilities. No browser GPU
or Windows rendered acceptance is inferred from these portable/headless checks.
