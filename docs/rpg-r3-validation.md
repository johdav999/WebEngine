# RPG R3 validation

Scope: portable inventory/item/equipment operations through the existing R2
transaction. API 1.2.0; no snapshot format or renderer change. See
[contracts](rpg-inventory.md) and [test fixtures](../tests/rpg_inventory_tests.cpp).

The RPG workflow builds Windows/MSVC Release, Linux native and pinned
Emscripten/Node Release.
The new inventory test joins both consumers, module tests, R1/R2 tests and the
configuration matrix. Two authored rulesets assert fixed canonical state strings.
Generated sequences exercise conservation and rejection invariants. Stress cases
reach 1024 inventories and 16384 items, including retirement/reallocation.

## Commands

Use [standalone setup](rpg.md#build-without-engine-dependencies), then:

```text
cmake --build build/rpg-sdk --config Release --parallel 2
ctest --test-dir build/rpg-sdk -C Release --output-on-failure
```

Local Windows-hosted Wasm uses the existing WebEngine .tools/emsdk 6.0.11 and
Node 24.19.0, CMake 4.2.1, Ninja, with the isolated checkout and ../wasm build
directory. Configuration explicitly sets WEBENGINE_SOURCE_DIR, Release,
WEBENGINE_BUILD_RPG, WEBENGINE_BUILD_RPG_TESTS and WEBENGINE_BUILD_MODULE_TESTS.
The initial configure omitted WEBENGINE_SOURCE_DIR and correctly failed before
compilation; the corrected configure supplies the isolated checkout path.
An initial fixture compile used EntityId::value as a function; that test typo was
corrected. The final build and complete local Wasm CTest suite passed 7/7 in
92.39 seconds, including 5012 R3 checks, 288 R2 checks, 52 R1 checks, 71 module
checks, both consumers and the six-case configuration matrix. The R3 test itself
took 1.45 seconds. Local commands used `cmake --build ../wasm --parallel 2` and
`ctest --test-dir ../wasm --output-on-failure` from the isolated checkout.

## CI evidence

Implementation commit `401cad1ab28442ce3d9a0c5b8c17b493b3461f79` passed all
three [RPG library CI jobs](https://github.com/johdav999/WebEngine/actions/runs/40194249572):
Ubuntu 24.04 native, Windows Server 2022/MSVC Release and pinned Emscripten/Node
Release. Each job passed the seven CTest cases, including the same two fixed R3
state fixtures. The existing [Portable module SDK workflow](https://github.com/johdav999/WebEngine/actions/runs/40194249551)
also passed all Linux, Windows and Wasm jobs. These are focused portable-library
results, not a claim about the full engine renderer or rendered sample hosts.

Changed contract links/code fences and the three available skill supplements
were checked locally. Project-specific skill context was preserved.

## Skill synchronization

Updated the available existing WebEngine, RPGGame and CafeGame engine-skill
SKILL.md and bundled references, plus WebEngine's canonical engine.md, preserving
project-context and concurrent simulator guidance. These local skill files are
separate from the isolated RPG publication; unrelated pending skill work was not
staged into this commit. The required legacy Johan installed/CatGame locations,
EJOHDAV installed location and EJOHDAV CatGame mirror are absent after discovery.
No replacement packages were created. The R3 contracts provide the handoff for
those unavailable copies.

No rendered browser, GPU, native-window or physical-device check is claimed.
Those hosts remain R9. RPGGame's engine submodule pin is unchanged.
R4 receives the existing shared transaction plus item ownership/modifier contracts;
abilities, statuses and RNG are not implemented here.
