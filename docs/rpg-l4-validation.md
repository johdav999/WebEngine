# RPG library L4 validation

L4 adds optional damage, defeat and revival to API 1.10.0. See
[contracts](rpg-vitality.md). Tested source: local commit `1b44c0d`.

| Gate | Local result |
| --- | --- |
| Windows x64 MSVC Release build | Passed |
| Windows standalone tests | 17 runtime/consumer tests passed; both build-contract checks passed on timeout-adjusted retry (19 total) |
| Emscripten Release build and Node execution | 19/19 passed |
| New vitality suite | 130 checks passed on each target |
| Shared rendered-host controller syntax | Emscripten C++20 syntax check passed |
| Rendered Windows/browser host execution | Not run for L4 |
| Fresh Linux native, physical GPU and mobile acceptance | Not run for L4 |

The new suite checks capped healing, overkill, explicit defeat and resurrection,
unit/magnitude rejection, raw-resource reconciliation, incapacitated turn and
ability exclusion, revival AP/resource costs, stale-world publication, effect
budget rollback including RNG, game-selected team outcomes, periodic effects,
canonical v1/v2/v3 saves and malformed life-state rejection.

Windows initially passed all 17 runtime/consumer tests, but the isolated CMake
contract and bridge-disabled builds exceeded their 180/300-second timeouts.
Only generated local CTest files were adjusted to 1200 seconds. Both failed
checks then passed (215 and 53 seconds respectively). WebAssembly used the same
local allowance; its two build checks passed in 259 and 246 seconds. Repository
and CI timeouts are unchanged. These are local results, not fresh CI evidence.

Build directories are `../l4-windows` and `../l4-wasm` relative to this isolated
checkout. Configure `examples/rpg` with WEBENGINE_SOURCE_DIR pointing here and
RPG, RPG_TESTS, MODULE_TESTS, QUESTS, QUEST_TESTS and RPG_QUESTS enabled (each option
has the WEBENGINE_BUILD_ prefix). Build Release, then run CTest with
`--output-on-failure --parallel 2`; use `-C Release` for Visual Studio. The Wasm
build uses the installed Emscripten toolchain and Node 24.19.0 emulator.
Local evidence is retained in RPGGame's ignored `build/rpg-l4/evidence` folder.

Core and host-test C++20 syntax checks also passed. Both rendered sample hosts
share vitality setup and typed strike effects, but compilation of their complete
hosts and fresh rendered smoke checks remain pending. Prior L3 rendering evidence
does not establish L4 acceptance.

Automatic approval review rejected publishing directly to WebEngine `main`
because this task did not explicitly authorize that shared-history mutation.
The implementation is committed locally; publication and CI require user
approval. Installed engine-skill copies were updated and verified, with that
local-only status recorded. RPGGame's engine submodule was not advanced, and its
concurrent game integration work was preserved.
