# Quest kernel M2 validation

This records the M2 baseline. M3 supersedes the rendered sample UI/commands; use
[the current host reproduction](quests-host-validation.md) for host tests. The
standalone M2 kernel tests and snapshot format remain unchanged.

Verified locally on 2026-10-22 in the adjacent WebEngine development checkout,
based on `f4cae84a2ebef51a2e2449409653f6e2901eb211` plus existing and M0/M1/M2
working changes. This is not a published revision or a Paws pin update.
Contracts: [quests.md](quests.md). Consumer handoff: CatGame
`docs/quest-module-m2.md`.

## Results

| Path | Evidence |
| --- | --- |
| Standalone MSVC 19.51 x64 Release | 242 checks pass |
| Standalone Emscripten 6.0.11 / Node Wasm | The same 242 checks pass |
| Native to Wasm, then Wasm to native | Byte-identical final 344-byte snapshots and full intermediate state/effect/diagnostic transcripts; snapshot FNV-1a `f840213075084f6a` |
| Chrome 155.0.8059.85, forced WebGPU | Generated `quest-site` flow passes; 5375 changed canvas pixels; no page/host errors |
| Same Chrome, forced WebGL2 | Same flow passes; 5375 changed canvas pixels; no page/host errors |
| Windows DesktopHost, Dawn/D3D12 | Rendered quest flow, rejected candidate, snapshot/retry and shutdown pass; 4269 changed scene pixels outside GUI |
| Windows DesktopHost, direct D3D12 | Same flow passes; 4269 changed scene pixels outside GUI |
| Native and Wasm with quests/modules disabled | Standalone consumer configures, builds and runs without either library |
| Existing native regressions | `engine_core`, `scene_archive`, `modules`, `quests`: 4/4 pass |
| Existing Paws baseline | 101 frozen artifacts still match their M0 manifest |

The contract suite covers catalogue validation, custom handler schemas and modes,
missing bindings, graph cycles/dangling/unreachable nodes, aggregate bounds,
corrupt/truncated wire data, All/Any objectives, filtered events/current facts,
ordered branches, prerequisite unlock, explicit timers, stable receipts, replay,
cancel/fail/retry, independent runtimes, module-provider lifetime, scoped and
duplicate input, bounded queue/source limits, partial FIFO processing, budget
atomicity, callback exceptions/reentry, stale/foreign candidates, invalid restore,
generation/time exhaustion and restored receipt durability.

Rendered samples compose `webengine.quests` and `example.delivery` through M1.
They activate a quest, deliver two events (including the custom weight objective),
choose a branch, reject a prepared reward, advance 500 simulation milliseconds,
complete for 20 coins, round-trip the snapshot and retry to completion with the
wallet still at 20. Browser automation clicks actual controls. Native automation
uses the same controller callbacks as the retained GUI and captures real frames;
it is not physical input acceptance. Images were inspected; buttons are visible
and the scene reflects committed state.

Ignored evidence is under `build/evidence/quests`: browser `report.json`, paired
PNGs, native before/after PPM/PNG and pixel report, native/Wasm `.qsave` and `.trace`
files, plus `identity.json` with artifact hashes. Frame counts vary by machine.
The full transition transcript is compared exactly; the displayed snapshot hash
alone is not the intermediate-trace proof.

## Reproduce portable contracts

From WebEngine, use PowerShell and the repository-pinned SDK. The standalone
example defaults to its containing engine checkout; override `WEBENGINE_SOURCE_DIR`
when testing another checkout. On Windows the CMake generator must match the
installed Visual Studio version.

```powershell
cmake -S examples/quests -B build/quests-native -G 'Visual Studio 18 2026' -A x64
cmake --build build/quests-native --config Release --parallel 4
ctest --test-dir build/quests-native -C Release --output-on-failure

$env:TEMP = Join-Path (Get-Location) 'build/quests-emcc-temp'
$env:TMP = $env:TEMP
New-Item -ItemType Directory -Path $env:TEMP -Force | Out-Null
& .tools/emsdk/upstream/emscripten/emcmake.exe cmake -S examples/quests -B build/quests-wasm -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/quests-wasm --parallel 4
ctest --test-dir build/quests-wasm --output-on-failure

New-Item -ItemType Directory -Path build/evidence/quests -Force | Out-Null
& build/quests-native/engine-quests/Release/quest_tests.exe --write build/evidence/quests/native.qsave
node build/quests-wasm/engine-quests/quest_tests.js --verify build/evidence/quests/native.qsave
node build/quests-wasm/engine-quests/quest_tests.js --write build/evidence/quests/wasm.qsave
& build/quests-native/engine-quests/Release/quest_tests.exe --verify build/evidence/quests/wasm.qsave
```

`--write` creates a snapshot and adjacent `.trace`; `--verify` checks both against
the current deterministic run and decodes the foreign snapshot. Wasm tests use
Node raw filesystem access solely for this test harness, never in the kernel.
Repeat standalone configure/build with `WEBENGINE_BUILD_QUESTS=OFF` and
`WEBENGINE_BUILD_MODULES=OFF` in separate native/Wasm directories; run the resulting
`quest_disabled` executable or JS entry point.

## Reproduce real hosts

Configure the existing Windows build as described in [desktop setup](windows-desktop.md),
including Dawn and direct D3D12, then enable the quest options:

```powershell
cmake -S . -B build/windows -DWEBENGINE_BUILD_QUESTS=ON -DWEBENGINE_BUILD_QUEST_TESTS=ON -DWEBENGINE_BUILD_QUEST_DEMO=ON
cmake --build build/windows --config Release --target quest_demo_windows quest_tests engine_tests scene_archive_tests --parallel 4
ctest --test-dir build/windows -C Release -R '^(quests|modules|scene_archive|engine_core)$' --output-on-failure
& build/windows/Release/quest_demo_windows.exe --smoke build/evidence/quests/dawn
& build/windows/Release/quest_demo_windows.exe --smoke build/evidence/quests/d3d12 --d3d12
python tools/verify_quest_pixels.py build/evidence/quests

& .tools/emsdk/upstream/emscripten/emcmake.exe cmake -S . -B build/web-release -G Ninja -DWEBENGINE_BUILD_QUESTS=ON -DWEBENGINE_BUILD_QUEST_DEMO=ON
cmake --build build/web-release --target quest_demo --parallel 4
```

Serve `build/web-release/quest-site` on an unused loopback port. Run
`scripts/quests-smoke.cjs` with Node resolving Playwright and pngjs, `QUEST_URL`
pointing to that server, and `BROWSER_EXECUTABLE` selecting Chrome if necessary.
The script defaults to port 8147 and checks both explicit renderer selections.
For an interactive Windows sample omit `--smoke`; `--d3d12` selects direct D3D12.

## Acceptance boundary

M2 is complete locally. These samples verify the kernel in both real host paths;
they do not complete M3's full host lifecycle/persistence vertical slice. Timers
use explicit deterministic deltas; snapshot round trips are in memory in the
rendered samples. Filesystem interchange is tested by the native/Wasm suite.
Scene travel, automatic pause-clock wiring, process/browser reload persistence
and the shared save coordinator remain M3 work. Paws runtime/quests/saves/pin are
unchanged. No macOS/Linux desktop, physical mobile, external service transaction,
untrusted code loading or sustained-performance claim follows from this evidence.
