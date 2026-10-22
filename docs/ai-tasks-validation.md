# AI task sequence validation — 2026-10-20

Delivered contracts are in [AI tasks](ai-tasks.md). This records fresh local
acceptance, not physical-device or rendered-animation acceptance.

## Native

Built `ai_task_tests`, `ai_controller_tests`, `state_machine_tests`, `ai_tests`,
`editor_tests`, `editor_structure_tests`, `application_tests` and
`scene_archive_tests` with the configured MinGW compiler. Final CTest run:
**8/8 passed, 5.01 seconds**. Log: `build/ai-tasks-native-results.log`.

```powershell
cmake --build build/native --target ai_task_tests ai_controller_tests state_machine_tests ai_tests editor_tests editor_structure_tests application_tests scene_archive_tests -j4
ctest --test-dir build/native -R '^(ai_tasks|state_machine|ai_controller|ai|editor|editor_structure|application|scene_archive)$' --output-on-failure
```

The task suite covers ordered execution, one start per task, captured blackboard
inputs, queued scoped result deferral, late-event rejection, deadline failure,
equal-duration/deadline floating-point behavior, interruptions, disable/restart,
callback stop during start and tick, cancellation exactly once, queue overflow
and terminal-result retry without rerunning work, ancestor retention and subtree
cancellation, missing navigation integration, actual Navigator movement and dirty
source publication, world facing under a rotated scene, typed schema rejection,
sequence/timeout/result validation, WEAI5 roundtrip/WEAI4 compatibility, fresh Play
instances and unchanged source scene bytes. The MoveTo → FaceTarget →
PlayAnimation → WaitForEvent example uses a controlled application animation
adapter; it proves task lifecycle and completion delivery, not rendered playback.

An intermediate shared-checkout archive run failed while concurrent version-32
animation/archive changes were being rebuilt. The final complete rebuild and all
eight suites above passed; no task-related core archive format change was made.

## Browser

Built `build/web-navmesh-release` target `web_assets` using repository-local
Emscripten **6.0.11** from `.emscripten-version`. Copied the selected binaries,
workers and current host adapters into `build/ai-tasks-acceptance-site` and served
that fixed site at `http://localhost:8147`, independently of concurrent `dist`
publication. Artifact digests: `build/evidence/ai-tasks-acceptance-artifacts.json`.

- `webengine.js`: `fedc1fd25705981b2cd97be7ea7f5bdcde3136b35ee72fee07ff450fb0ba2d11`
- `webengine.wasm`: `4fc5f5dc887fa5ebe6ec19c55b3a120f537bc3fb77c10726899e5ec09d48c2f0`

`scripts/editor-ai-tasks-smoke.cjs` passed in Chrome **155.0.8059.85**, explicitly
asserting WebGPU and WebGL2 selection at a 1500×1050 CSS viewport, DPR 1 and 2,
with no page/console errors. Evidence directories:
`build/evidence/ai-tasks/` and `build/evidence/ai-tasks-dpr2/`.

The browser test uses real task controls for addition/type selection/reordering,
one-edit Apply and Undo/Redo, invalid timeout preservation and Reload recovery.
It exports/reimports WEAI5, verifies the standalone-preview execution label and
disabled forms, assigns an ordinary actor, saves/reloads its scene, observes the
real live sequence, injects AnimationFinished to complete and Damaged to cancel,
checks bounded outcome history/source isolation/fresh Play state, and authors a
short timeout to verify its failure reason and TaskFailed transition. Integrated
authoring and expanded history captures were visually inspected.

Existing browser suites also passed on both renderers: `editor-ai`,
`editor-ai-controller`, `editor-ai-blackboard`, `editor-ai-hierarchy`,
`editor-ai-events`, `ai` and `navmesh`. Their evidence is under
`build/evidence/ai-tasks-<suite>-regression/`.

```powershell
$env:NODE_PATH="$PWD/build/detail-browser-tools/node_modules"
$env:BROWSER_EXECUTABLE='C:\Program Files\Google\Chrome\Application\chrome.exe'
$env:DEMO_URL='http://localhost:8147'
$env:EVIDENCE_DIR='build/evidence/ai-tasks'
node scripts/editor-ai-tasks-smoke.cjs
$env:DEVICE_SCALE_FACTOR='2'
$env:EVIDENCE_DIR='build/evidence/ai-tasks-dpr2'
node scripts/editor-ai-tasks-smoke.cjs
```

Documentation links, JS syntax, scoped whitespace, matching site/host scripts,
artifact hashes and all 20 updated skill files across the installed and CatGame
copies were checked. No physical Android/iOS acceptance, parallel task execution,
durable task checkpointing or automatic animation subscriptions are established.
Move-to requires the host's navigation source and supporting scene/motor setup.
