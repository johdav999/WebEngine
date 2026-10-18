# Actor AI controller validation — 2026-10-18

Contracts: [actor controllers, typed bindings and live debugging](ai-controllers.md).

## Native verification

Built `ai_controller_tests`, `state_machine_tests`, `ai_tests`, `editor_tests`,
`editor_structure_tests`, `application_tests`, `movement_tests`, `physics_tests`
and `scene_archive_tests`, then ran:

```powershell
ctest --test-dir build/native -R '^(ai_controller|state_machine|ai|editor|editor_structure|application|movement|physics|scene_archive)$' --output-on-failure
```

All nine suites passed. New controller tests cover:

- Controller commands reaching an earlier-inserted free motor in the same fixed
  step, independent of hierarchy insertion order.
- Enter/exit command ownership, disable/deferred removal cancellation, explicit
  restart, independent per-agent state and a bounded 32-entry transition history.
- Wrong parameter types and unknown behavior rejection without interrupting a
  valid runtime, missing-motor assignment preserving scene/history, exact controller
  payload roundtrips and malformed suffix rejection.
- Scene configuration roundtrip, unknown-codec rejection, saved actor reference
  resolution in a reconstructed scene and missing target rejection.
- Codec-aware Play cloning with fresh handles/runtime, source Idle state and
  byte-for-byte source preservation after Play/Stop.
- Undo/redo of component assignment and typed configuration changes.

Existing state-machine, AI, editor, application, archive, physics and movement
contract suites also passed. These focused tests do not accept all unrelated work
in the shared checkout.

## Fixed browser artifact site

Built with repository-local Emscripten **6.0.11** in `build/web-navmesh-release`.
Because concurrent work also publishes `dist/`, final browser acceptance uses a
separate `build/ai-controller-acceptance-site` served at `http://localhost:8138`.
It contains the selected main/worker JS/Wasm, current host adapters and required
fixtures. The frozen artifact manifest is
`build/evidence/ai-controller-acceptance-artifacts.json`.

| Artifact | SHA-256 |
| --- | --- |
| `webengine.wasm` | `017CF85511766A5865B91E7B1CC50AE3FE6E09B7C7B703EDBF984E01E5CCD55B` |
| `asset_decode.wasm` | `BE6970FFAEF1AE07E212483C1E54AA6477E1942E4A23B92143F7C6D27ED884FA` |

Chrome **155.0.8059.80** independently selected actual WebGPU and WebGL2.
`scripts/editor-ai-controller-smoke.cjs` passed at DPR1 and DPR2. Evidence:

- `build/evidence/ai-controller-fixed/results.json` and per-backend scene files / PNGs.
- `build/evidence/ai-controller-fixed-dpr2/results.json` and per-backend artifacts.

Real UI controls place an actor, report missing graph bindings, reject assignment
without a motor, add/configure a motor, assign the authored movement graph and
edit vector parameters through registered schemas. The script remaps a graph
condition to `flag`, edits its boolean, then remaps it to `near`, selects a saved
actor target and edits its numeric distance. Assignment/configuration undo/redo
is checked separately from graph history.

The scene is downloaded as `.wescene`, run in isolated Play, and inspected through
the live graph. Tests verify real X movement, active-node highlighting, elapsed
state time and transition records. Authoring controls remain disabled during Play.
Stop preserves source scene bytes and history. The saved file is reopened and its
actor identity, controller, graph, parameter values and subsequent Play behavior
are reconstructed. No captured page or console errors remain in the final runs.

## Regression checks on the fixed site

The same frozen main/worker/host set passed:

- `scripts/editor-smoke.cjs` on WebGPU and WebGL2, evidence
  `build/evidence/editor-ai-controller-fixed-regression`, retaining existing scene
  editing/history/file and Play/Stop acceptance.
- `scripts/editor-ai-smoke.cjs` on both backends, evidence
  `build/evidence/graph-ai-controller-fixed-regression`, retaining graphical graph
  authoring, drag/Escape, separate history/files, isolated preview and demo binding.
- `scripts/ai-smoke.cjs` on both backends, evidence
  `build/evidence/patrol-ai-controller-fixed-regression`, retaining patrol,
  sight/memory, target removal, predictive clearance/detours, bounded retry,
  pause, saves after return, resize, switching and reload.

One earlier concurrent-browser regression run hit Chromium `ERR_NO_BUFFER_SPACE`.
A serial retry passed; the fixed-site serial regressions also passed with no
captured page/console errors.

## Integration checks and limitations

The optional AI codec uses existing custom-component archive machinery; the core
archive schema is not extended for AI and `engine_core` does not link AI.
SceneArchive clone and EditorPlaySession accept explicit registries. Controller
configuration persistence resets runtime state on load/Play; it is not a checkpoint.

The shared checkout gained route implementation changes during this work. Missing
Actor/Scene includes and comma-containing JavaScript object literals inside route
`EM_ASM` probes blocked builds; the includes and probe construction were repaired
without changing route behavior. Final controller acceptance uses fixed artifacts
to avoid unrelated republishing races.

Documentation, architecture, README and both installed/CatGame engine-skill copies
are updated. Callback code/closures are application-owned and never serialized.
The supplied browser registry demonstrates idle/move and always/flag/near;
navigation/perception/game actions require additional application bindings. Source
actor selection addresses the corresponding Play component by saved identity,
while runtime handles and mutable state remain isolated.

No custom-component subtree duplication, automatic game binding discovery,
hierarchical states, runtime-state checkpointing, physical Android/iOS acceptance
or sustained mobile performance is established by these checks.
