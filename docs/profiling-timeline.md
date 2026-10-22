# Detailed CPU profiling timeline

Open **Performance → Detailed CPU timeline → Start recording / Open timeline**, or
launch `?scene=physics&timeline=1`. Capture is off by default. Recording is independent
of simulation pause; pausing capture retains its recorded history.

The viewer shows the latest 120 frame callbacks and synchronous tasks, with nested
spans for simulation, physics, animation, AI, loading and rendering. Select a record
to pin it while capture continues, **Follow latest** to resume following, or choose
**Slowest CPU record**. Pinned inspection survives rolling-buffer eviction until
selection/clear changes; trace export contains the current native buffer.

The budget defaults to 16.67 ms and is configurable within 0.1–10000 ms. Red records
exceed either CPU duration or frame interval budget. Category/search filters apply
to the selected record. Zoom changes the time scale with horizontal scrolling.
Select a span to see its subject, offset, inclusive duration and exclusive duration.

## Reading the measurements

- **CPU duration** is elapsed wall-clock time inside the instrumented callback or
  task on its owning thread. Timing resolution follows the platform clock.
- **Frame interval** is the time between recorded frame callback starts. It also
  includes browser scheduling, wait time and tasks between callbacks. A long
  interval with short CPU work is not evidence of expensive engine computation.
- **Inclusive time** contains child spans. **Self time** subtracts direct children.
  Category summaries sum self time, avoiding parent/child double counting.
- An import or other instrumented synchronous operation outside a frame becomes a
  separate **task** record. The next frame identifies captured tasks in its interval;
  their presence is evidence of work between callbacks, not proof that they account
  for every millisecond of scheduling delay.
- The frame callback scope includes host/inspector work. Existing aggregate
  `cpuMs` excludes inspector processing and therefore has a different boundary.
  Existing GPU timestamp metrics remain independent. Queue-submit spans are CPU
  API-call durations, not GPU execution time.

Recording/export and clock calls have overhead. Capture for diagnosis, then compare
normal runs with capture off. No FPS improvement or sustained device-performance
claim follows from a timing capture.

## Instrumented work

| Category | Current spans |
| --- | --- |
| Simulation | Application advance, scene variable and fixed updates |
| Physics | Reconciliation/body synchronization, complete step and Jolt solver call |
| Animation | Per-character update, sampled/blended pose, layers and IK |
| AI | Controllers, states, state-machine evaluation, task sequences/tasks, navigation and navmesh path search |
| Loading | Asset-manager processing/loader start, FBX/GLB import, image decode, scene archive save/load, worker-result hydration and CPU resource upload work |
| Render | WebGPU/WebGL2 render, scene collection and WebGPU queue submission |

Names/subjects identify actors, states or asset IDs where the owning API has one.
An instrumented Jolt call measures the blocking owner-thread duration, including
any solver coordination; it is not a per-worker solver trace. Async network wait,
decode-worker compute and GPU execution are not presented as main-thread CPU
spans. Existing importer measurements and GPU metrics provide separate evidence.
Uninstrumented native/browser work remains inside ancestor self time or interval
gaps. This is not a sampling profiler or automatic C++ call-stack capture.

## Portable API and lifetime

Link `engine_core`; include `engine/performance_timeline.hpp`.

```cpp
auto timeline = std::make_unique<engine::FrameTimeline>();
engine::ProfileSession capture(*timeline); // Borrows on the current owner thread.
timeline->setRecording(true);
{
    engine::ProfileFrame frame;
    engine::ProfileScope scope(engine::ProfileCategory::AI, "Plan next move", "Guard");
    // Existing simulation work; no extra scene ticks or altered clock inputs.
}
timeline->setRecording(false);
auto report = timeline->json();
auto trace = timeline->traceJson();
```

Only one `ProfileSession` may bind a recorder on a thread. End scopes before
releasing the session/recorder. The session owns no scene, node, asset, callback or
worker lifetime. It clears the thread-local binding when destroyed. Subjects are
copied into bounded owned storage; no borrowed node references survive recording.
Construct the recorder on the heap: fixed storage is approximately 5 MiB and the
browser allocates it only on first use. Disable capture to eliminate clock reads
and per-span allocation; the disabled path performs only a thread-local/flag check.

`ProfileFrame` owns a frame record. A `ProfileScope` outside one owns an automatic
task record. C++ unwinding closes ordinary scopes. Stop during active work closes
and marks the record truncated. Reset discards current work/history, invalidates
old scope tokens and starts a new relative time origin. Resume omits the first
frame interval so a capture pause is not treated as engine delay.

There are 256 spans per record and 32 nesting levels. Capacity/depth overflow omits
spans and increments visible dropped counters; parent self time then includes
unattributed work. Labels use 47 bytes and subjects 95 bytes, preserving a complete
UTF-8 prefix when truncated. The recorder uses fixed arrays with no per-span heap
allocation. JSON/trace serialization and browser visualization allocate separately
and run on demand/at most four updates per second. Recording changes no simulation
state, culling policy, physics step, animation clock or AI budget.

## Exports and browser host

**Export trace** downloads Chrome/Perfetto compatible complete (`X`) CPU events with
microsecond timestamps/durations, categories, subjects, record identity and
truncation/drop diagnostics. All events describe one owner thread; there is no
automatic cross-worker or GPU clock correlation.

The existing **Export JSON** / `EnginePerformance.snapshot()` includes an optional
`timeline` field. `EngineTimeline.snapshot()` also exposes recent records, pinned
selection, budget and boundary definitions. The playground owns the optional
recorder/session and `ProfileFrame` around its RAF callback. Browser controls use
`engine_profile_control` (0 pause, 1 record, 2 clear, 3 trace) and
`engine_profile_snapshot`; those adapters are outside portable headers. Timeline
state is transient and does not change scene/application archive formats.

See [validation](profiling-timeline-validation.md) and the existing
[performance metrics](../README.md#performance-measurements).
