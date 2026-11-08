# RPG library L7 validation

L7 implements equipment requirements, multi-slot occupancy and selectable
progression/respec. See [contracts](rpg-builds.md). Validated 2026-10-26.

| Gate | Local result |
| --- | --- |
| Windows x64 MSVC Release library and tests | Build passed |
| Windows focused L7 suite | Final 92 checks passed |
| Windows complete regression | 22/22 passed |
| Emscripten standalone suite / Node | Build passed; 22/22 checks passed, including 92 L7 checks |
| Shared host-controller / Node | 41 checks passed |
| Browser sample | rpg_demo build passed |
| Windows adapter and shared host test | MSVC C++20 syntax checks passed |
| Rendered Windows/browser execution | Not run for L7 |
| Fresh Linux native, physical GPU and mobile acceptance | Not run for L7 |

Coverage includes point overspending, cyclic/unknown prerequisites, exclusive
choices, duplicate selections, automatic-vs-selected unlock ownership, full-bag
rollback with multiple displaced items, one modifier application per multi-slot
item, base-only equipment eligibility, explicit safe respec, canonical catalogue
and runtime extensions, legacy formats, incompatible catalogue rejection, forged
choice/slot state and restore invalidation of prepared commands. Coordinator tests
cover combined choice save/load and respec. Shared host tests exercise the actual
mastery/greatblade/respec actions and combined load path used by both hosts.

The first focused test incorrectly expected identical live save bytes after
restore. Runtime intentionally rebases revision/generation. The test now checks
stale-candidate rejection and selected state after restore, while checking exact
canonical bytes through decode/encode independently. The final Windows focused
executable was explicitly rebuilt to include two late test cases after the full
regression run; library source was unchanged. Both targets passed all 92 cases.

Standalone builds use the existing `../l4-windows` and `../l4-wasm` directories,
relative to the isolated engine checkout. Both configure `examples/rpg` with RPG,
RPG tests, Modules tests, Quests, Quests tests and RPGQuests enabled. Build with
`cmake --build ../l4-windows --config Release --parallel 4` or
`cmake --build ../l4-wasm --parallel 4`; run CTest with `--output-on-failure
--parallel 3` (`-C Release` for Windows). Wasm uses the installed Emscripten SDK
and Node 24.19.0 with the bundled Python directory on PATH. The cached root Wasm
build supplies rpg_demo and rpg_host_tests.

Only generated local CTest files extend the configuration/bridge-off build checks
to 1200 seconds; source/CI timeouts are unchanged. Logs remain in RPGGame's ignored
`build/rpg-l7/evidence`. Configuration/bridge-off checks passed in 39/286 seconds
for Windows and 56/111 seconds for Wasm. Earlier renderer results are not claimed
as L7 acceptance.

The required installed engine-skill copies and engine guide were updated and
verified. RPGGame's pin and concurrent integration work are preserved. L4-L7 are
local pending publication approval: an earlier automatic approval review rejected
a direct push to shared main without explicit publication authorization. No fresh
remote CI is claimed. Game integration remains the separate G11 milestone.
