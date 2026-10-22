# AI blackboard validation — 2026-10-19

The feature adds typed graph schemas, independent controller values, validated
atomic live patches, parameter sources, transition guard labels, graph field
authoring and a Play-only live inspector. The application patrol example now
feeds perception and target positions into its actor controller.

## Native checks

Build `ai_controller_tests`, `state_machine_tests`, `ai_tests`, `editor_tests`,
`editor_structure_tests`, `application_tests` and `scene_archive_tests`, then run:

```powershell
ctest --test-dir build/native -R '^(ai_controller|state_machine|ai|editor|editor_structure|application|scene_archive)$' --output-on-failure
```

All seven suites passed after a fresh build (5.17 seconds). Coverage includes
Boolean/Number/Vector/Actor fields, independent instances,
state/timer/history preservation, atomic invalid-patch rejection, source type/range
validation, live movement, distance guards, removed targets, collision-aware Y
constraints, source/default persistence, transient-value exclusion and reading
version-1 graphs/controller payloads. Existing AI tests cover patrol, sight,
investigation, return, obstruction/retry, pause, lifecycle and scene replacement.

## Browser checks

Built with repository-local Emscripten 6.0.11 and tested in Chrome 155.0.8059.80.
The fixed acceptance site is `build/ai-blackboard-acceptance-site`, served on
`http://localhost:8141`. Its manifest is
`build/evidence/ai-blackboard-acceptance-artifacts.json`. It contains the matching
host and asset decoder worker; later concurrent `dist` publications do not change
this acceptance site.

`scripts/editor-ai-blackboard-smoke.cjs` passed on WebGPU and WebGL2 at DPR 1 and 2.
Evidence: `build/evidence/ai-blackboard/` and
`build/evidence/ai-blackboard-dpr2/`, including results, exported `.weai`/`.wescene`
files and live debugger screenshots. The WebGPU live debugger screenshot was visually inspected.

The test uses real field forms for all four types, graph schema Undo/Redo and
WEAI2 export, compatible source selectors and disabled constant inputs. During
Play it edits typed values, checks timer preservation and range rejection, watches
transition guard labels, and confirms Stop preserves authoring bytes/history.
Scene reload and another Play session restore defaults rather than live edits.

Existing browser suites also passed on both backends:

- `scripts/editor-ai-controller-smoke.cjs`: assignment, constants, scene history,
  saved files, isolated Play, movement and debugger.
- `scripts/editor-ai-smoke.cjs`: graphical authoring, gestures/cancellation,
  graph files, isolated preview, validation and application adoption.
- `scripts/ai-smoke.cjs`: patrol, perception/memory/removal, bounded detours/retry,
  pause, lifecycle and presentation pixels.
- `scripts/navmesh-smoke.cjs`: click-to-move, topology invalidation/publication,
  original-goal recovery, overlays and control handoff.

Results are under `build/evidence/ai-blackboard-<suite>-regression/`.

The field schema and live-input layer do not add queued events, hierarchical
states, automatic perception/navigation discovery or runtime checkpoint files.
Standalone graph preview still uses manual guards. Desktop Chrome and DPR
emulation establish no physical Android/iOS or sustained mobile-performance
acceptance. Concurrent FX changes are outside this feature's acceptance scope.
