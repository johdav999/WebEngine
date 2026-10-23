# Quest host M3 validation

Verified 2026-10-22 in the local WebEngine working checkout. This is local M3
delivery, not a published engine revision or a Paws engine-pin update. Exact
artifact hashes and base revision are recorded in ignored
`build/evidence/quest-host/identity.json`. See [host contracts](quests-host.md).

## Verified results

| Target | Result |
| --- | --- |
| Native MSVC x64 Release shared host suite | 49 checks pass |
| Emscripten 6.0.11 / Node shared host suite | Same 49 checks pass |
| M2 kernel suite | 242 checks pass on native and Wasm |
| Existing engine regressions | Application, core, scene archive, modules, quests and quest host: 6/6 pass |
| Chrome 155.0.8059.85, forced WebGPU | Complete browser host/reload/file-exchange flow passes; 5375 changed canvas pixels, no page/host errors |
| Same Chrome, forced WebGL2 | Same complete flow passes; 5375 changed canvas pixels, no page/host errors |
| Windows Dawn/D3D12 | Separate seed/restart/import process flows pass |
| Windows direct D3D12 | Same three process flows pass |
| Paws frozen baseline | All 101 M0 artifacts still match |

The portable host suite tests the complete envelope, truncated/corrupt data,
recomputed-checksum wallet/clock inconsistencies, fractional delta accumulation,
Application pause/resume/suspension, no catch-up, stale camera/host-generation
rejection, discarded old-scene input, failed scene binding/load rollback, failed
save preservation, successful restoration, both branches, once-only rewards,
40 repeated travels with one loaded scene and shutdown destruction.

Native **seed** activates, delivers one apple, pauses, travels to Orchard and
persists. The executable exits. A separate **resume** process restores the partial
objective and explicit pause. It rejects stale/corrupt loads, travels, resumes,
finishes collection, enters the timer branch, pauses/saves/loads halfway, resumes
without catch-up, completes, retries the alternative branch and saves with exactly
20 coins. **Import** starts another process from an actual browser-downloaded
mid-timer file, resumes it, completes/retries without another award, and exports
the resulting live state. These phases run on both native renderers, capture real
frames and verify shutdown unloads scenes.

Browser automation clicks the generated site's controls and exercises partial
objective persistence across page reload, timer pause through longer wall waits,
travel while paused, mid-timer save/reload, delayed import after travel, injected
storage-write failure retaining the previous value, corrupt-load preservation,
explicit host suspension, completion/practice, and completed-save reload.
It uploads an actual native `.qhost`; its own File download is passed to the native
Import phase and the resulting native file is uploaded back through the file
input. Both sides finish with 20 coins, valid receipts, restored pause and one
loaded scene. This is rendered-host file interchange, beyond the M2 Node-only
codec comparison.

`report.json` records six native process phases and two browser runs. Screenshots,
PPM captures, saved envelopes and `identity.json` retain scoped evidence. The
browser and native screenshots were visually inspected. Native pixel checks
exclude the retained GUI. Browser frame counts/timer values vary with scheduling;
the script checks invariants rather than claiming identical wall-time traces.

## Build and run

Use the repository's configured Windows dependencies and pinned Emscripten SDK.
From WebEngine in PowerShell:

```powershell
cmake -S . -B build/windows -DWEBENGINE_BUILD_MODULES=ON -DWEBENGINE_BUILD_QUESTS=ON -DWEBENGINE_BUILD_QUEST_DEMO=ON -DWEBENGINE_BUILD_QUEST_TESTS=ON
cmake --build build/windows --config Release --target quest_demo_windows quest_host_tests quest_tests application_tests engine_tests scene_archive_tests --parallel 4
ctest --test-dir build/windows -C Release -R '^(application|engine_core|scene_archive|modules|quests|quest_host)$' --output-on-failure

$env:TEMP = Join-Path (Get-Location) 'build/quests-emcc-temp'
$env:TMP = $env:TEMP
New-Item -ItemType Directory -Path $env:TEMP -Force | Out-Null
& .tools/emsdk/upstream/emscripten/emcmake.exe cmake -S . -B build/web-release -G Ninja -DWEBENGINE_BUILD_MODULES=ON -DWEBENGINE_BUILD_QUESTS=ON -DWEBENGINE_BUILD_QUEST_DEMO=ON -DWEBENGINE_BUILD_QUEST_TESTS=ON
cmake --build build/web-release --target quest_demo quest_host_tests quest_tests --parallel 4
node build/web-release/quest_host_tests.js
node build/web-release/quests/quest_tests.js
```

Serve `build/web-release/quest-site` on an unused loopback port. Install/resolve
Playwright and pngjs for Node, and Pillow for the image checks. Then:

```powershell
$env:QUEST_URL = 'http://127.0.0.1:8147'
$env:BROWSER_EXECUTABLE = 'C:/Program Files/Google/Chrome/Application/chrome.exe'
node scripts/quests-host-smoke.cjs
python tools/verify_quest_host.py build/evidence/quest-host
```

The runner uses `build/windows/Release/quest_demo_windows.exe` unless overridden
by `QUEST_NATIVE`, and writes evidence under `EVIDENCE_DIR` (default
`build/evidence/quest-host`). It creates new per-run native test profiles and fresh
browser contexts; it does not delete or use a player's default saves. Native
processes are hidden and have a timeout. The native smoke exercises the real
DesktopHost scheduler/renderers with supplied monotonic timestamps; browser time
comes from RAF. This is automated controller/render acceptance, not physical
mouse/touch or OS power-loss acceptance.

For browser-only checks use `scripts/quests-smoke.cjs` (the legacy entry point now
targets the M3 UI), or set `QUEST_BROWSER_ONLY=1` for the full runner. That skips
native file interchange and cannot establish desktop acceptance. The standalone
M2 external-consumer and disabled-module targets remain unchanged.

## Scope and limits

M3 delivers the host vertical slice, including explicit persistence, pause and
scene travel. It does not add autosave, cloud synchronization, concurrent save
writers, save-history recovery, authentication, arbitrary executable plugins or
Paws migration. Browser persistence is origin-scoped localStorage for this small
sample. Native persistence uses existing atomic desktop-file replacement; storage
failure injection does not simulate a physical power loss. Browser suspension is
tested through its exported visibility adapter, not physical mobile backgrounding.
Only Windows x64/this machine's renderers and desktop Chrome were tested; no
macOS/Linux desktop, physical mobile or sustained-performance claim follows.

M4 still publishes a deliberately selected engine revision, validates and advances
the consumer pin, and establishes Paws' shared host seam before quest/save migration.
