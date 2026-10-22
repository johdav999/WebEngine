# Detailed CPU timeline validation — 2026-10-20

Verified against the same locally built/staged engine as the
[prefab editor](prefab-editor-validation.md). Capture is optional and disabled by
default; validation does not claim an engine speed improvement.

## Portable checks

`performance_timeline_tests` passed natively and as
`performance_timeline_wasm_tests` in Node **24.13.1**, built with repository-local
Emscripten **6.0.11**. Deterministic clock tests cover:

- Exact nested inclusive/self times, parent/depth identities, standalone tasks,
  frame intervals and exception unwinding.
- Disabled capture making no clock calls; one borrowed session per thread.
- Event/depth limits, dropped counters and chronological 120-record eviction.
- Stop/truncation, reset generation invalidation and invalid/backward clocks.
- Owned bounded text, escaped JSON and microsecond Chrome/Perfetto trace events.

The 23 native regression suites listed in the prefab validation also passed,
including existing aggregate performance, physics, animation, AI, assets,
importers, application and archive contracts. Instrumentation retains the normal
simulation/input clock and step paths.

## Browser acceptance

Ran `scripts/performance-timeline-smoke.cjs` serially at **DPR1 and DPR2** in Chrome
**155.0.8059.85**, testing actual **WebGPU** and forced **WebGL2** on the selected
`dist` served at `http://localhost:8137`.

- Real skeletal animation, physics/Jolt, AI and FBX import produce categorized
  spans. Hierarchy bounds and self/inclusive timing relationships are checked.
- A synchronous import outside RAF becomes a separate loading task.
- Recording pause/resume, slow budget, category/search filtering, zoom, pinned
  selection beyond ring eviction, following, clear and compact controls work.
- Trace downloads were physically saved and parsed as JSON complete events.
- Existing performance JSON export includes the optional timeline field.
- Final page/console error lists are empty on both backends and both DPR runs.

Evidence is in `build/evidence/performance-timeline{,-dpr2}/results.json`, trace
files and screenshots. The existing animation editor regression passed on both
backends. The legacy aggregate performance smoke stopped at an outdated default
scene draw-count expectation; see the prefab validation for the precise boundary.
It is not reported as a passing suite.

These are owner-thread CPU elapsed-time measurements. No worker internals,
network wait or GPU execution is inferred from CPU spans. Existing GPU timestamps
remain independent. Browser scheduling gaps, dropped spans, capture overhead,
one-thread trace scope and bounded storage are explicit in the
[feature contracts](profiling-timeline.md). Desktop browser/DPR validation does
not establish physical mobile behavior or sustained performance.
