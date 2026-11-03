# RPG R4 validation

Scope: API 1.3.0, typed ability evaluation, atomic costs/item consumption/effects,
status stacking/refresh/dispel/phase expiry, and version-1 candidate PCG32 state.
See [contracts](rpg-abilities.md) and [tests](../tests/rpg_ability_tests.cpp).

The eight-case CTest suite includes both independent consumers, configuration
matrix, Modules, R1 foundation, R2 characters, R3 inventory and R4 ability tests.
R4 checks official PCG vectors and a fixed canonical byte fixture, continuation,
rejection sampling and draw-budget exhaustion; complete rollback projections;
provider/schema/parameter failures, exceptions and lifetime; item-consuming healing,
non-damage resources, duplicate/stale candidates, phase epochs, all four phase
hooks, refresh/stack/dispel, modifier overflow and combined/global status limits.

## Reproduction

Follow [standalone build setup](rpg.md#build-without-engine-dependencies). The
existing RPG workflow runs Windows/MSVC Release, Linux native and pinned Wasm
Release. The existing SDK workflow checks module/quest consumers and omission.

Local Windows-hosted Wasm uses Emscripten 6.0.11, Node 24.19.0, CMake 4.2.1 and
Ninja. The isolated checkout is build/rpg-r4/checkout under RPGGame; its build
directory is ../wasm. Configuration sets WEBENGINE_SOURCE_DIR to that checkout,
the existing WebEngine .tools/emsdk toolchain, CMAKE_BUILD_TYPE=Release and
WEBENGINE_BUILD_RPG, WEBENGINE_BUILD_RPG_TESTS, WEBENGINE_BUILD_MODULE_TESTS=ON.

```text
cmake --build ../wasm --parallel 2
ctest --test-dir ../wasm --output-on-failure
```

The final local build and full CTest run passed 8/8 in 80.01 seconds: 1350 R4
checks, 5012 R3 checks, 288 R2 checks, 52 R1 checks, 71 module checks, both
consumers and the six-case configuration matrix. The R4 test took 0.99 seconds.
An earlier full run passed every gameplay test but hit the existing 180-second
configuration-matrix timeout while building omission consumers. The unchanged
matrix passed on retry with the populated cache in 75.45 seconds; its timeout
was not increased. The final run also includes the added targeting/proposal tests.

## CI evidence

Implementation commit `fb9ef17e1ae290ea8648d02b320679621c666b2f` passed the
[RPG library workflow](https://github.com/johdav999/WebEngine/actions/runs/40196153668)
on Ubuntu 24.04 native, Windows Server 2022/MSVC Release and pinned
Emscripten/Node Release. All three jobs built and passed the eight CTest cases,
including official PCG vectors, canonical RNG bytes and deterministic outcomes.
The [Portable module SDK workflow](https://github.com/johdav999/WebEngine/actions/runs/40196153680)
also passed all Linux, Windows and Wasm jobs. These results cover the portable
RPG/module libraries and omission configurations, not rendered engine acceptance.

## Documentation synchronization

Updated and verified SKILL.md and bundled engine references in the existing
WebEngine, RPGGame and CafeGame skill packages; WebEngine's canonical engine.md;
the Johan and EJOHDAV installed engine-skill packages; and the required Johan
CatGame mirror. Preserved local project-context, clothing and simulator guidance.
The legacy packages became available during this task and were inspected before
updating. These local skill updates are separate from the isolated engine commit;
unrelated concurrent skill/source work was not staged into the RPG publication.
Contract links/code fences and all six skill supplements were checked locally.

## Scope limitations

No GPU, rendered browser, native window or physical-device check is claimed;
the affected code is a portable rules kernel. R9 remains the rendered-host gate.
The complete game snapshot is still R8; RNG encoding alone is not a game save.
RPGGame's pinned engine remains unchanged.

R5 consumes the same transaction and explicit status-phase hooks. It must add
encounter-authorized phase order, owner/turn epochs, initiative and turn budgets;
it must not let presentation or wall time advance these rules.
