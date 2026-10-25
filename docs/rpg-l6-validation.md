# RPG library L6 validation

L6 adds weighted loot, atomic reward bundles and coordinated quest loot.
See [contracts](rpg-rewards.md). Validation date: 2026-10-26.

| Gate | Local result |
| --- | --- |
| Windows x64 MSVC Release library and tests | Build passed |
| Windows focused L6 reward suite | 153 checks passed |
| Windows quest coordination suite | 155 checks passed |
| Windows complete regression | 21/21 passed |
| Emscripten standalone suite / Node | Build passed; 21/21 checks passed, including 153 L6 reward checks |
| Shared host-controller / Node | 38 checks passed |
| Browser sample build | rpg_demo build passed |
| Native Windows adapter and shared host tests | MSVC C++20 syntax check passed |
| Rendered Windows/browser execution | Not run for L6 |
| Fresh Linux native, physical GPU and mobile acceptance | Not run for L6 |

Coverage includes deterministic weighted choices and explicit no-drop entries;
table/weight/roll bounds; invalid selected grants; shared draw and expanded
operation budgets; preview isolation; full-bag rollback and retry; duplicate,
suppressed and repeated-in-one-command receipts; stale/foreign/restore candidates;
quest RNG rollback with queue retention; encounter/quest namespace separation;
combined save/load and repeated interaction. Shared host tests exercise collection
after encounter exit and suppression after travel/save/load.

Standalone builds use the cached `../l4-windows` and `../l4-wasm` directories
relative to the isolated engine checkout, configured from `examples/rpg` with
RPG, RPG tests, Modules tests, Quests, Quests tests and RPGQuests enabled.
Commands: `cmake --build ../l4-windows --config Release --parallel 4`,
`cmake --build ../l4-wasm --parallel 4`, then `ctest --output-on-failure
--parallel 3` in each build directory (`-C Release` for Windows).
The cached root `../wasm` supplies rpg_host_tests and rpg_demo. Emscripten uses
the installed repository SDK and Node 24.19.0; its launcher requires the bundled
Python directory on PATH. The initial Wasm configure failed to launch Python;
setting PATH fixed configuration. Builds needed access to installed SDK caches.

Only generated local CTest files extend isolated configuration/bridge-off tests
to 1200 seconds; repository and CI timeouts are unchanged. Windows configuration/bridge-off checks passed in 108/283 seconds; Wasm checks
passed in 65/116 seconds. Local logs are retained
in RPGGame's ignored `build/rpg-l6/evidence` directory. Earlier renderer acceptance
is not presented as fresh L6 GPU evidence.

Installed engine-skill copies and the engine guide were updated and verified.
RPGGame's engine pin and concurrent integration work are preserved. L4-L6 remain
local: an earlier automatic approval review rejected a direct push to shared
main, and publication approval has not been supplied. No new remote CI is claimed.
Game campaign reward integration remains G8.
