# AI state machines and graphical editor validation — 2026-10-18

Contracts: [runtime and graphical editor](ai-state-machines.md).

## Native verification

Built `state_machine_tests`, `ai_tests`, `navmesh_tests`, `editor_tests`,
`editor_content_tests` and `editor_structure_tests`, then ran:

```powershell
ctest --test-dir build/native -R '^(state_machine|ai|navmesh|editor|editor_content|editor_structure)$' --output-on-failure
```

All six suites passed. New tests cover enter/update/exit ordering, simulation
delay gates and timer reset, deterministic priority, bounded cycles, condition
work failure, exception cleanup and reentry rejection, explicit stop/restart,
invalid replacement preservation, immutable graph edits, saved identity,
undo/redo, bounded history, exact `.weai` roundtrip, malformed/oversized/trailing
file rejection, orphan endpoints, duplicate IDs and nonfinite positions.

The autonomous demo was converted to a real `StateMachineHost` with named game
bindings. Existing AI tests retained patrol/perception/memory, navigation recovery,
target removal, pause, deferred teardown and scene replacement coverage. A native
teardown failure exposed an exit callback resolving a pending-removal motor;
guarded component lookup repairs the lifecycle boundary, and the final suite passes.
Existing navigation and scene-editor contract suites also pass.

## Browser build and evidence

Built `web_assets` from `build/web-navmesh-release` using repository-local
Emscripten **6.0.11**, publishing matching main/worker JS/Wasm and current host
files. Served `dist/` at `http://localhost:8137`. Final SHA-256:

| Artifact | SHA-256 |
| --- | --- |
| `webengine.wasm` | `9066FC6EA59A901DA6A1D8A584FADE83A5FE4219EDDF85ED6FA541DAF57F5F9A` |
| `asset_decode.wasm` | `2E652C2586686EA8C8E3E2C32BDA1BDEBCCD2285BD0561E3F592A3F938E0FEA6` |

Chrome **155.0.8059.80** independently selected actual WebGPU and WebGL2.
`scripts/editor-ai-smoke.cjs` passed at DPR1 and DPR2 with zero captured page or
console errors. Final evidence:

- `build/evidence/editor-ai-final/results.json` and per-backend PNGs / `.weai` files.
- `build/evidence/editor-ai-final-dpr2/results.json` and per-backend artifacts.

The script uses real controls and pointer gestures to create/rename states,
reject invalid names, drag/cancel nodes, connect states, edit transition delays,
change priority, set entry, undo/redo, delete with incident connections and restore.
It runs the actual C++ preview using condition toggles, verifies minimum state time,
run/pause/stop and live active-node highlighting, then saves and reloads a graph.
Malformed imports retain valid content. Source scene bytes are hashed and scene
history revisions checked before/after authoring and preview; they remain identical.
Graph framing/zoom do not change document history. Resize and close/reopen preserve
the graph. A custom authored entry with the `blocked` behavior is applied and
verified in the running AI demo, proving application consumption of edited data.

Integration checks caught two browser ownership issues: scene-sidebar CSS hid the
modal inspector, and scene-level Escape capture consumed graph cancellation. The
graph inspector now has its own scoped section, and the modal owns keyboard input
while open. Final real drag/Escape and both-backend tests pass.

## Regression checks on the same final build

- `scripts/ai-smoke.cjs`, evidence `build/evidence/ai-state-machine`: both backends
  passed patrol/follow/investigate/return, actual motion/pixels, perception,
  target removal, obstacle detours/retry/deadlines, pause, saves after return,
  restart, resize, scene switching and reload with no captured errors.
- `scripts/navmesh-smoke.cjs`, evidence `build/evidence/navmesh-state-machine`:
  both backends passed click-to-move, static detours, navigation revisions,
  removed-floor stop, explicit rebuild, original-goal recovery and lifecycle checks.
- `scripts/editor-smoke.cjs`, evidence
  `build/evidence/editor-state-machine-regression`: existing scene editor acceptance
  passed on WebGPU and WebGL2, including its scene/history/file and Play/Stop flows.

Updated architecture/build boundaries, README, AI and scene-editor contracts, plus
installed and CatGame engine-skill copies and affected references. Documentation
links, scoped whitespace and both skill copies are verified separately.

## Acceptance boundary

Flat finite-state execution and standalone graphical AI asset authoring are
delivered. Behavior/condition implementations remain application code. Preview
conditions are controllable test inputs and do not execute game behavior actions.
The live demo recognizes its documented binding set; other games must supply hosts.
`.weai` files save definitions/layout only, separately from scene archives. There
is no automatic actor attachment, game callback discovery, live-agent debugger,
hierarchical/concurrent states, visual action scripting or AI graph autosave.
Desktop browser/DPR2 acceptance does not establish physical Android/iOS behavior
or sustained mobile performance, nor accept unrelated shared-checkout changes.
