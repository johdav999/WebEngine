# Hierarchical AI validation — 2026-10-19

## Native contracts

Freshly built `state_machine_tests`, `ai_controller_tests`, `ai_tests`,
`editor_tests`, `editor_structure_tests`, `application_tests` and
`scene_archive_tests` passed: seven suites, 4.96 seconds.

```powershell
cmake --build build/native --target state_machine_tests ai_controller_tests ai_tests editor_tests editor_structure_tests application_tests scene_archive_tests -j4
ctest --test-dir build/native -R '^(state_machine|ai_controller|ai|editor|editor_structure|application|scene_archive)$' --output-on-failure
```

The new assertions cover initial-child descent, nested explicit restart,
outer-to-inner updates, inner-to-outer cancellation, parent priority over an
earlier child edge, sibling retention, ancestor timers across child transitions,
bounded parent self-transitions and cleanup after condition exhaustion/partial
entry failure. Invalid roots, parents, cycles, child entries and excessive depth
reject before graph/history replacement. WEAI3 roundtrips hierarchy and WEAI2
loads flat; existing tests retain WEAI1/AIC1 compatibility coverage.

Controller assertions check no spurious records for ancestor entry, exact edge
and declared source/target diagnostics, parent interruption canceling child motor
commands before its tick, complete-path lifecycle cleanup and scene/isolated Play
reconstruction with fresh runtime paths/default inputs.

## Browser editor and Play

Built with repository-local Emscripten 6.0.11 and tested in Chrome 155.0.8059.80.
The acceptance site is `build/ai-hierarchy-acceptance-site`, served on
`http://localhost:8142`. Matching host/worker hashes are recorded in
`build/evidence/ai-hierarchy-acceptance-artifacts.json`, independently of later
concurrent `dist` publications. The final host adds correct empty-path highlighting
between restart and the next tick without changing the Wasm binary.

`scripts/editor-ai-hierarchy-smoke.cjs` exercises both WebGPU and WebGL2 at DPR1
and DPR2. Final reports, exported graph/scene files and screenshots are in
`build/evidence/ai-hierarchy-dpr1-final/` and
`build/evidence/ai-hierarchy-dpr2-final/`. The expanded-group and active-path WebGPU
screenshots from the same graph layout were visually inspected.

The test uses real controls for nested template creation, collapse/expand,
breadcrumbs, parent/default-child editing, nested child creation and group deletion.
It checks portable rejection of a cycle without history changes, one-command
subtree dragging/deletion and Undo/Redo, hierarchical preview paths, parent
interrupts, WEAI3 export/import and scene persistence. During Play it verifies
active chain/edge highlighting, leaf transition records, live parent interruption
stopping movement, no active highlight immediately after restart, source byte/
history preservation on Stop, and fresh hierarchy/defaults after reload.

Regression suites also passed on both backends:

- `scripts/editor-ai-smoke.cjs`: flat graph editing/gestures/files/preview and
  supported application adoption.
- `scripts/editor-ai-controller-smoke.cjs`: constant bindings, scene history,
  persistence, movement and live debugging.
- `scripts/editor-ai-blackboard-smoke.cjs`: typed fields/sources, live edits,
  rejection, timer preservation, source isolation and reload defaults.
- `scripts/ai-smoke.cjs`: patrol/perception/memory/removal, bounded recovery,
  pause/lifecycle and presentation pixels.
- `scripts/navmesh-smoke.cjs`: manual navigation, topology invalidation/rebuild,
  retained goals, overlays and control handoff.

Reports are in `build/evidence/ai-hierarchy-<suite>-regression/`. Those regressions
ran before the final empty-path highlight correction; the full hierarchical
workflow was rerun afterward on both backends/DPRs.

This establishes desktop Chrome and high-DPI behavior, not physical Android/iOS
or sustained mobile performance. Concurrent FX/GUI changes remain outside this
feature's acceptance scope. Queued gameplay events, parallel regions, history
states, internal transitions and runtime AI checkpoints are not delivered here.

See [hierarchy contracts](ai-hierarchy.md).
