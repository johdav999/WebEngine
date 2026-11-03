# RPG library L5 validation

L5 implements bounded combat planning, deterministic selection, cancellation and
normal-pipeline execution. See [contracts](rpg-combat-ai.md).

Implementation: local commit `47e0177`, based on L4 commit `f3ae237`.

| Gate | Local result |
| --- | --- |
| Windows x64 MSVC Release library and tests | Build passed; 20/20 regression checks passed |
| Final Windows AI suite | 72 checks passed after the final fallback-cancellation correction |
| Emscripten Release standalone suite / Node | Final source: 20/20 passed, including 72 AI checks |
| Shared host-controller / Node | Final source: 34 checks passed, including AI strike, recovery and fallback |
| Browser sample | Final `rpg_demo` build passed |
| Native Windows adapter and shared host test | MSVC C++20 syntax checks passed |
| Rendered Windows/browser execution | Not run for L5 |
| Fresh Linux native, physical GPU and mobile acceptance | Not run for L5 |

The complete native regression run preceded the final one-line correction that
reports Cancelled when fallback validation both requests cancellation and throws.
The final native AI executable was rebuilt and its expanded 72-check suite passed.
The final WebAssembly build, full regression suite and shared host test include
that correction. No earlier rendering evidence is presented as L5 acceptance.

Coverage includes stable ID tie-breaking and input-order independence; positive
and negative game utility; declined legal actions; invalid movement and missing
items; cooldown filtering; actor/duplicate-ID validation; evaluation/action budgets;
explicit truncated search and rejected fallback; no live state/RNG/watermark
mutation during preview; normal costs and publication; stale world, foreign
runtime and restore invalidation; cancellation before/during planning and after
preparation; reentrant callback rejection; coordinator ledger restrictions and
unchanged quest state. The shared sample exercises AI through normal presentation
reservation and recovery on both platform source paths.

Tests used the existing local build directories `../l4-windows` and `../l4-wasm`
relative to the isolated checkout (their names are historical). Standalone
configuration is `examples/rpg` with WEBENGINE_SOURCE_DIR pointing to this checkout
and WEBENGINE_BUILD_RPG, WEBENGINE_BUILD_RPG_TESTS, WEBENGINE_BUILD_MODULE_TESTS,
WEBENGINE_BUILD_QUESTS, WEBENGINE_BUILD_QUEST_TESTS and WEBENGINE_BUILD_RPG_QUESTS ON.
Build Release, then run `ctest --output-on-failure --parallel 3` in the build
directory, adding `-C Release` for Visual Studio. The Wasm build uses the installed
Emscripten toolchain and Node 24.19.0. The cached root `../wasm` build supplies
`rpg_host_tests` and `rpg_demo`.

As in L4, only generated local CTest files give the two isolated build checks
1200-second timeouts. Repository/CI timeouts are unchanged. Windows build checks
passed in approximately 93/290 seconds; final Wasm checks took 93/139 seconds.
Local logs are retained in RPGGame's ignored `build/rpg-l5/evidence` directory.

Installed engine-skill copies and the engine guide were updated and verified.
RPGGame's engine submodule and concurrent integration work were preserved.
The code is committed locally. The earlier automatic approval review rejected a
direct push to shared `main`; publication approval has not been supplied, and no
new CI or rendered acceptance is claimed. Game-owned enemy policies, navigation
and automatic scheduling remain the separate G7 integration task.
