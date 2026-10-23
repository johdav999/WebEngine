# Direct D3D12 GPU timings and profiling

Hardware timestamps on the direct queue sample at most every 100 ms and retain
120 completed samples. Profiling defaults on and changes no simulation, culling,
animation, quality or rendering decisions.

`DesktopHost::diagnostics().performance` contains:

- `gpu`: the shared `PerformanceMonitor` schema, with state, sample counts, live
  age, mean/p95/max `layersMs`, `shadowMs` and `totalMs`. The total sums disjoint
  layer work and shadow maps, excluding sky, reflections, post and GUI.
- `gpuProfile`: full command-list `frameMs`, per-name inclusive and self statistics,
  dropped/invalid counters, queue `frequencyHz`, bounded readback storage and the
  latest completed frame's nested events.
- Each event has a name, parent index, view, layer, frame-relative `startMs`,
  inclusive `durationMs`, `selfMs`, draw calls and mesh/FX triangles. Shadow events
  use `layer` as the map slot; `-1` means unassigned. Draws include auxiliary
  fullscreen/sky/GUI work; triangle counts exclude fullscreen, sky and GUI geometry.
- Latest metadata identifies submitted frame serial, extent, effective MSAA, HDR
  and dropped spans. It describes that sampled submission, which can precede
  current CPU counters. Durations use milliseconds; unavailable values are `null`.

Scopes cover frame/view/layer parents, object/FX copies, each shadow map,
reflection, sky, opaque geometry, water snapshots/surfaces, transparency/FX,
underwater, MSAA resolve, scene copy, bloom, composition, GUI and framebuffer copy.
Optional operations appear when executed; empty layers may measure zero ticks.
Lazy resource uploads inside a drawing scope belong to that scope.

Inclusive parents overlap children: do not sum every row. Self time subtracts
direct children. Per-name statistics sum matching events within each sampled frame
before computing mean, nearest-rank p95 and maximum over the window; absent names
contribute zero. `frameMs` covers the command stream through final image work,
excluding profiling resolve/readback, CPU preparation, pre-submission queue wait,
presentation and scanout. It is not GPU utilization or a shader-stage counter.

## Export and inspect

At an idle owner-thread boundary, write the summary and trace:

```cpp
std::ofstream("gpu-summary.json") << host.diagnostics().performance;
std::ofstream("gpu-trace.json") << host.gpuProfileTrace();
```

Open the trace in Perfetto or Chrome's trace viewer. Complete `X` events use
microsecond `ts`/`dur` on one GPU queue, with frame/epoch/view/layer and draw/triangle
arguments. The first retained GPU tick defines the origin; CPU clocks are not
correlated. Export allocates on demand and never waits for unfinished samples.
Dawn and unbound/unavailable graphics return an empty detailed trace; Dawn retains
its existing GPU summaries. The independent [CPU timeline](profiling-timeline.md)
also identifies the owner-thread Direct D3D12 render call.

Launch with `$env:WEBENGINE_GPU_TIMING='0'` for an instrumentation comparison.
Both native backends read it at device creation; remove it or set `1` to enable.
Disabled direct profiling allocates no query/readback buffers and issues no
timestamp writes/resolves. `capabilities().timestampQuery` describes queue support
independently of requested mode. Browser WebGPU keeps `?gpuTiming=off`; WebGL2
keeps its unavailable GPU timing state.

## Lifetime and limits

Three independent slots each own a 2048-entry timestamp heap and 16 KiB readback
buffer. A sample permits 1024 scopes including parents; overflow omits spans and
reports `droppedSpans`. Only written start/end pairs resolve. Completed submission
fences gate mapping and reuse. Slot exhaustion skips a sample and increments
`droppedSamples`; profiling adds no waits. Normal render resource reuse retains
its existing fences.

Ticks convert with the queue frequency. Backward or out-of-parent timestamps
reject the complete sample and increment `invalidSamples`. Optional query
allocation/non-device readback failure reports `readback-failed` while rendering
continues; device failure follows bounded recovery. See Microsoft's
[D3D12 timestamp contract](https://learn.microsoft.com/en-us/windows/win32/direct3d12/timing).

Scene/view-list, quality/shadow and attachment changes clear history and advance
the measurement epoch. Pending old epochs stay fenced and cannot publish stale
results. Identical per-frame view refreshes retain the window. Device recovery
destroys old heaps/readbacks/history; exports contain only the current device.
Shutdown/unbinding clears history. Pair exports with renderState's `generation`
when comparing devices. No callback borrows scenes, cameras or application objects.

## Verification

```powershell
./scripts/build-windows.ps1
./scripts/windows-gpu-profile-smoke.ps1
ctest --test-dir build/windows -C Release -R '^(performance|performance_timeline|desktop_recovery|desktop_fallback|desktop_api|desktop_api_d3d12)$' --output-on-failure
# Also use a separately configured Dawn-free build:
./scripts/windows-gpu-profile-smoke.ps1 -BuildDir build/windows-direct -Backend D3D12 -EvidenceDir build/evidence/gpu-profile-direct
```

The smoke verifies real timestamps/nesting, split views, shadows, water/reflections,
FX, HDR/MSAA, bloom opt-out, scene/GUI/capture copies, reset, selected-device removal,
recovery, rolling eviction and shutdown. Enabled/disabled captures match exactly.
Browser suites are `performance-smoke.cjs`, `performance-timeline-smoke.cjs` and
`render-fallback-smoke.cjs` against fresh pinned Emscripten output. No comparative
throughput or sustained performance claim follows. See
[validation evidence](windows-gpu-profiling-validation.md).
