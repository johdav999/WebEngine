# Simulator v0.1 release gates

Status on 2026-10-25: release candidate implementation complete; release acceptance pending native CI and hardware checks. Nothing is tagged, published or pushed by stage 6. Consumer renderer pins are unchanged.

## Evidence from this task

| Check | Result |
| --- | --- |
| Clean standalone Emscripten 6.0.11, CMake 4.2.1/Ninja Release consumer | Passed in build/simulator-release-wasm; no graphics/core targets or dependency downloads. |
| CTest under SDK Node 24 | 4/4 passed: service example, whole-step parity scenario, partitioned parity scenario, contract suite. |
| Exact elapsed-time partition comparison | Passed: complete snapshots and ordered event records match byte-for-byte, excluding transient handle epochs. Trace in build/simulator-release-wasm/parity.txt. |
| Browser contracts | Edge 155.0.4283.62: 764 checks passed, zero page/console errors. |
| CafeGame bundled sources | Six source/build hashes verified; canonical LF hashes work across checkout line endings. |
| Native Windows | Not run: vswhere found no VC.Tools.x86.x64 installation; no cl, clang++ or g++ on PATH. |
| Native Linux and cross-platform comparison | CI configured, not executed/observed in this local task. |
| CafeGame rendering | Stage 5 evidence: forced WebGL2 and automatic software fallback, save/load/page reload passed. Not rerun for these release-only additions. |

The parity fixture uses one worker, one slot, two customers and two finite ingredients. It saves while assigned and running, rejects a truncated save without mutation, restores fresh handles, checks completed output ownership, retries a pending payment, cancels running work, and checks no leaked slots/claims or extra output/reward. Script output contains full canonical bytes, not only a digest. The workflow uploads native Windows, Linux and Wasm traces and compares all three in a dependent job; missing or unequal traces fail that job. Each recording also compares whole versus partitioned advances.

## Reproduce and approve release

Build the standalone consumer as documented in simulator.md with tests and example enabled. Record a native trace:

```sh
python scripts/simulator-parity.py record build/native.txt build/simulator-native/simulator_parity
```

For Visual Studio use build/simulator-native/Release/simulator_parity.exe. For Wasm:

```sh
python scripts/simulator-parity.py record build/wasm.txt node build/simulator-wasm/simulator_parity.js
python scripts/simulator-parity.py compare build/native.txt build/wasm.txt
```

Before tagging v0.1, obtain passing native Windows/Linux jobs and the cross-platform parity job for the exact proposed revision. Run the CafeGame native session host and full Linux cloud check for the consumer revision. Hardware WebGPU, physical mobile and sustained performance remain separate validation gates; browser software rendering does not satisfy them.

## Known limits

Single owner thread; full bounded-state candidate copies favor small simulators. No pathfinding, seating, recursive supply chains, restocking, spoilage, staff shifts, progression, multiplayer, offline catch-up or provider calls. Prices, wallet and durable storage remain game policy. Only snapshot version 1 and identical catalogs restore; no migration or authenticity guarantee. No stable binary ABI is promised. Snapshot and event determinism covers the discrete simulator, not rendering or physics.

Installed engine-skill at C:/Users/EJOHDAV and the WebEngine/CafeGame repository copies are updated. Historical C:/Users/Johan installed/CatGame locations are unavailable on this host.

Stage 10 source extension: portable atomic inventory transfer and optional supply ordering/reorder helper, adopted by CafeGame schema 4. See [supply API and actual validation](simulator-supply.md). Native and physical GPU/mobile gates remain open.

Stage 11 source extension: portable Ledger adopted by CafeGame for costs, refunds and wages. See [economy API and actual evidence](simulator-economy.md). Native and hardware GPU/mobile release gates still need verification.

Stage 12 source extension: quality/condition and disposal mechanisms, adopted by CafeGame for timed freshness and staffed maintenance. See [quality contracts and evidence](simulator-quality.md). Native and hardware GPU/mobile gates require separate evidence.


Stage 13 adds bounded grid layout validation and enabled stable occupancy indices; CafeGame envelope 7 validates furniture, claims and ledger together. See simulator-layout.md for contracts and evidence.


Stage 14 adds portable demand values and CafeGame envelope-8 guest/reputation integration. See simulator-demand.md for contracts and dated evidence.

## Stage 15 diagnostics validation — 2026-10-25

Optional diagnostics.hpp and shared simulator_diagnostics_cases.hpp add detached
catalog validation and read-only job inspection. Canonical and Cafe-bundled sources
were compared; all 15 bundled source hashes passed. The API owns no platform state.

Standalone MSVC 19.44 native Windows consumer/parity/contracts built in
build/simulator-stage15-native and CTest passed 4/4. The final standalone Emscripten
6.0.11 suite passed 4/4 in build/simulator-release-wasm; its browser contract page
passed in Edge 156.0.4314.42 without page/console errors. The new helper checks run
alongside the historical counter reported by that page. Cafe's final native and
Wasm suites each passed 3/3. Four Cafe presets with seeds 123/456 over 120 seconds
produced identical report JSON after Windows newline normalization. Final Cafe
scenario/inspection/export browser acceptance passed on forced WebGL 2 and
automatic fallback; the full existing rendered regression also passed both paths.
All three engine-skill copies document the delivered API and ownership boundaries.

These results cover portable Windows/Node/browser mechanisms and the Cafe browser
consumer. They do not validate a native Cafe 3D renderer, Linux/cloud execution,
hardware WebGPU, physical mobile, long stress or content migrations. The renderer
pin, core schema 1 and Cafe envelope 8 stay unchanged. See simulator-authoring.md.

## Stage 16 compatibility/scale evidence — 2026-10-25

ContentHash/ContentStamp/checkpoint helpers and Runtime::metrics are optional
portable interfaces. Clock-only advance and no-queued-job assignment avoid state
copies; transactional due transitions retain existing ordering/work/outbox limits.
Core wire schema stays 1. See simulator-compatibility.md for the exact boundary.

Final MSVC 19.44 Release standalone CTest passed 4/4; final Emscripten 6.0.11
Release CTest passed 4/4 and the shared contract page passed in Edge 156.0.4314.42
without page/console errors. Shared new cases cover hash encoding, clock/expiry
boundaries, full outboxes, saturation, stale handles, restore and conservation.
The parity runner compared exact native/Wasm snapshots and ordered events after
verifying whole/partitioned stepping in each host; traces are in
build/evidence/stage16-{native,wasm}.trace.

Cafe's native/Wasm suites each passed 4/4. Its shared scale helper passed through
1024 jobs in two 24-hour-per-seed native/Wasm checkpoint/replay comparisons, seeds
123/456, with 868 checkpoints each and exact deterministic results. The core
1024-job checkpoint was 117,556 bytes; Cafe save peaks were 20,124 bytes with four
live guests/jobs and 128 recent ledger entries. Detailed performance samples live
in CafeGame/build/evidence/stage16-stress-parity.json; timing samples include host-load
noise and are not acceptance thresholds. Source hashes/parity and all three
engine-skill copies were verified. Cafe's focused migration browser smoke passed
forced WebGL 2 and automatic fallback; hosts own its envelope-9 migrations.

These are portable Windows/Node/browser checks, not native Cafe 3D rendering,
Linux/cloud acceptance, hardware WebGPU, physical mobile or arbitrary-content
migration evidence. No renderer, platform dependency or clock ownership was added.

Final Cafe full game, demand and authoring rendered regressions passed both forced WebGL 2 and automatic fallback against this module, in addition to the focused compatibility smoke.
