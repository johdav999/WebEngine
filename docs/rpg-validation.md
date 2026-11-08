# RPG R1 validation

Scope: optional module foundation, metadata catalogue, identity allocation,
ownership and standalone consumer. The [API contract](rpg.md) lists the delivered
surface. Character/transaction rules, RNG, quest bridge, wire snapshots and
rendered hosts remain later milestones.

## Reproduction

Use the commands in [the external consumer](../examples/rpg/README.md). Enable
both `WEBENGINE_BUILD_RPG_TESTS` and `WEBENGINE_BUILD_MODULE_TESTS` with RPG enabled.
CTest runs `rpg_consumer`, `rpg_cmake_contract`, `modules`, and `rpg_foundation`.
The configuration test builds/runs default-off and all-off consumers and checks
expected configure failures for missing Modules, unsupported demo/bridge and tests
enabled without RPG. Each build uses its own directory and the same toolchain.

The foundation tests use explicit checks that remain active in Release. Coverage:

- ID syntax/length, explicit types, zero versions/namespaces and uint64 exhaustion.
- Duplicate modules/handlers/definitions, missing or incompatible dependencies,
  missing validators/handlers, mismatched handler versions and hard bounds.
- Failed/reentrant installation leaves the previous snapshot intact.
- Validator exceptions cannot replace existing catalogue/bindings.
- Canonical definition order, lookup and independent session identity cursors.
- Catalogue/session retention after composition destruction and validator-before-
  provider teardown when the last owner releases the snapshot.

The standalone project also rejects unexpected core/quest/physics/GUI targets.
Configuration tests reject `_deps` population. Public RPG code includes only
standard-library and Modules/RPG headers; there are no fetch/find-package commands
in `rpg/` or its standalone consumer. The root integration is opt-in and uses the
same target; existing engine builds retain their own dependencies.

## Evidence for this task

Local Windows-hosted Emscripten 6.0.11 (repository-local SDK), Ninja, CMake 4.2.1:

```bat
call .tools\emsdk\emsdk_env.bat
emcmake cmake -S examples/rpg -B build/rpg-r1-wasm -G Ninja -DCMAKE_BUILD_TYPE=Release -DWEBENGINE_SOURCE_DIR=C:/Users/EJOHDAV/source/repos/WebEngine -DWEBENGINE_BUILD_RPG=ON -DWEBENGINE_BUILD_RPG_TESTS=ON -DWEBENGINE_BUILD_MODULE_TESTS=ON
cmake --build build/rpg-r1-wasm --parallel 2
ctest --test-dir build/rpg-r1-wasm --output-on-failure
```

Build and all four CTest cases passed locally (77.88 seconds total), including
the six configuration cases, 52 RPG foundation checks and 71 module checks.
The toolchain warns about CMake 4.2.1 shared-library support; this
consumer uses static libraries only.

`.github/workflows/rpg.yml` runs Ubuntu 24.04, Windows 2022/MSVC and pinned
Emscripten/Node builds with the same consumer and omission/failure matrix. The
existing module SDK workflow remains unchanged for Modules/Quests regressions.

Published implementation: `824de01b730805d7b74b3c164e77c331430fd7f5`.

| Platform/check | Result | Evidence |
| --- | --- | --- |
| Ubuntu 24.04 native, standalone/configuration matrix | Passed, 4/4 CTest cases | [RPG workflow](https://github.com/johdav999/WebEngine/actions/runs/40191701796) |
| Windows 2022/MSVC Release, standalone/configuration matrix | Passed, 4/4 CTest cases | [RPG workflow](https://github.com/johdav999/WebEngine/actions/runs/40191701796) |
| Clean Emscripten 6.0.11/Node, standalone/configuration matrix | Passed, 4/4 CTest cases | [RPG workflow](https://github.com/johdav999/WebEngine/actions/runs/40191701796) |
| Existing Modules/Quests consumers and omission checks | Passed on Linux, Windows and Wasm | [SDK workflow](https://github.com/johdav999/WebEngine/actions/runs/40191701819) |
| Documentation | Local links, code fences and diff whitespace passed | Local check on 2026-10-25 |

The broader core suite already failed `vehicle` and `spotlight` (94/96 passed)
at the starting architecture commit `c1736b9`; see the
[baseline run](https://github.com/johdav999/WebEngine/actions/runs/40187821879).
The [general build on R1](https://github.com/johdav999/WebEngine/actions/runs/40191701931)
was still running when this evidence was recorded; the focused successes above
do not claim a green full-engine suite. This milestone changes no vehicle or
spotlight implementation and does not attempt unrelated repairs.

No graphics, input, physics, browser DOM or native window integration is introduced
in R1. No rendered/GPU/mobile acceptance is claimed; rendered samples are R9.

AGENTS.md's installed engine-skill and CatGame mirror paths are unavailable on this
machine (including their EJOHDAV equivalents). They were not updated. The current
contract and R2 handoff are in rpg.md; merge those into existing skill copies when
available, preserving game-specific guidance.
