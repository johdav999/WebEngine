# RPG library L8 validation

L8 delivers trading, crafting and bounded detached replay. See
[contracts](rpg-economy.md). Local validation date: 2026-10-26.

| Gate | Local result |
| --- | --- |
| Windows x64 MSVC Release library and tests | Build passed |
| Windows complete regression | 23/23 passed; L8 143 checks, quest bridge 167 checks |
| Emscripten standalone suite / Node | Build passed; 23/23 tests passed |
| Shared host-controller / Node | 46 checks passed |
| Browser sample | rpg_demo build passed |
| Windows adapter and shared host test | MSVC C++20 syntax checks passed before the test-only storage reset |
| Browser host.js syntax | Node check passed |
| Rendered Windows/browser and fresh Linux native | Not run for L8 |

Windows configuration and bridge-off checks passed in 82 and 389 seconds.
Wasm equivalents passed in 77 and 160 seconds.
The first host market scenario inherited the previous scenario's saved full bag
through the controller's intentional startup auto-load. The test now clears its
fake storage before starting the independent market scenario; production code
correctly rejected that full-bag purchase. No economy rule was relaxed.

Coverage includes purchase/sale conservation, partial stock, fresh destination
identity, exact payments, insufficient funds, recipient headroom, unit/scale
mismatch, wrong/equipped owners, full bags, crafting consumption before output,
duplicate inputs, operation budgets, stale/foreign candidates, duplicate and
suppressed receipts, post-transaction reload and deterministic detached replay.
Failures compare complete canonical live saves to detect partial changes to
resources, receipts, allocation cursors, RNG and producer watermarks.

Standalone builds use `../l4-windows` (MSVC x64 Release) and `../l4-wasm`
(Emscripten/Node), relative to the isolated engine checkout. CTest runs include
the optional quest bridge and configuration/dependency tests. The root Wasm build
provides the shared controller tests and browser sample. Local generated CTest
configuration-build timeouts may be extended to 1200 seconds; source/CI settings
are unchanged. Logs are retained in RPGGame's ignored `build/rpg-l8/evidence`.

Fresh rendered Windows/browser, physical GPU/mobile, Linux-native and remote CI
checks are not claimed. The installed engine-skill copies were updated and verified.
RPGGame integration remains G12; its pin and concurrent work are preserved.
The L4-L8 stack remains local: an earlier automatic approval review rejected a
direct push to shared main without explicit publication authorization. No push
was retried for this implementation request.
