# Typed AI event validation — 2026-10-19

## Native

Fresh builds of `state_machine_tests`, `ai_controller_tests`, `ai_tests`,
`editor_tests`, `editor_structure_tests`, `application_tests` and
`scene_archive_tests` passed: seven suites, 5.71 seconds.

```powershell
cmake --build build/native --target state_machine_tests ai_controller_tests ai_tests editor_tests editor_structure_tests application_tests scene_archive_tests -j4
ctest --test-dir build/native -R '^(state_machine|ai_controller|ai|editor|editor_structure|application|scene_archive)$' --output-on-failure
```

New assertions cover exact typed schemas, FIFO consumption under transition
budgets, one-time delivery, event quotas, reject-newest overflow, unmatched/early
event discard, callback deferral, parent event priority, state-exit cancellation,
foreign/reentered activation rejection, stop rejection and bounded traces. Graph
tests cover WEAI4 roundtrips, WEAI3 compatibility, duplicate definitions and
undeclared triggers; previous version 1/2 and hierarchy checks remain.

Controller tests read triggering payloads in a registered behavior, copy data into
a declared blackboard field, capture entry-time completion tokens, correlate
transition/event sequences, reject missing actor payload references before queue
mutation, cancel queued inputs on disable/configuration handoff, and reconstruct
empty independent Play queues. Callback configuration reentry rejects without
deleting an executing runtime. Existing simulation, hierarchy, motor, scene and
editor tests pass alongside these additions.

## Browser

Built with repository-local Emscripten 6.0.11 and tested in Chrome 155.0.8059.80.
The fixed site is `build/ai-events-acceptance-site`, served on
`http://localhost:8143`. Matching host/worker/editor hashes are in
`build/evidence/ai-events-acceptance-artifacts.json`; subsequent concurrent `dist`
publications do not change this acceptance site.

`scripts/editor-ai-events-smoke.cjs` passed on WebGPU and WebGL2 at DPR1 and DPR2.
Reports, exported WEAI4/scene files and trace screenshots are in
`build/evidence/ai-events/` and `build/evidence/ai-events-dpr2/`. The WebGPU event
authoring view and live payload trace were visually inspected.

The script uses real forms for event schema/parameter authoring, Undo/Redo,
transition trigger editing and atomic duplicate-schema rejection. Preview checks
typed queued input before stepping, FIFO/unmatched tracing, parent interruption,
actor payloads and stale captured tokens. Live Play checks typed vector/number/
boolean/actor input paths, blackboard guards, invalid-range preservation, scoped
animation completion and late-post rejection. WEAI4 and scene roundtrips preserve
definitions/triggers; Stop preserves source bytes/history, and fresh Play starts
with an empty queue/trace.

Existing suites also passed on both renderers:

- `scripts/editor-ai-smoke.cjs`
- `scripts/editor-ai-controller-smoke.cjs`
- `scripts/editor-ai-blackboard-smoke.cjs`
- `scripts/editor-ai-hierarchy-smoke.cjs`
- `scripts/ai-smoke.cjs`
- `scripts/navmesh-smoke.cjs`

Their reports are under `build/evidence/ai-events-<suite>-regression/` and cover
flat graph authoring, constant/blackboard bindings, hierarchical cancellation and
debugging, source isolation, patrol/perception/recovery and navmesh lifecycle.

These runs establish desktop Chrome and high-DPI behavior. They do not establish
physical Android/iOS acceptance or sustained mobile performance. Event trace
allocation failure is contained by design, not an injected OOM acceptance run.
Automatic game animation/perception subscriptions, durable event delivery and task
execution remain application integrations. Concurrent GUI/FX changes are outside
this feature's acceptance scope. See [event contracts](ai-events.md).
