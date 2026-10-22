# Adaptive graphics quality

Open **Rendering quality → Adaptive graphics quality**, enable **Maintain a
frame-time target**, and choose a target from **4–100 ms** (default **16.67 ms**,
about 60 frames/s). `?scene=terrain&adaptive=1&targetMs=16.67` demonstrates it with
existing terrain LOD on either renderer. Adaptation is off by default.

The currently selected profile/custom settings become a retained quality ceiling.
The controller gradually reduces resolution and projected mesh-detail size, then
reduces shadow resolution, filtering and map count at discrete thresholds. It
never raises quality above that ceiling. Disable adaptation to restore it.
Successful manual quality, MSAA, lighting, shadow or mesh-detail edits stop
adaptation and apply against the retained ceiling; rejected edits leave it intact.
Changing the target while enabled preserves the current reduction and restarts
timing evidence. There is no saved preference or scene archive format change.

## Timing and gradual changes

The browser host samples completed, visible RAF intervals, including while the
simulation is explicitly paused. It does not replace simulation clocks or steps.
A one-second warmup precedes measurement. The exponential moving average has a
0.5-second time constant, independent of frame rate.

- Sustained smoothed intervals above **112%** of target for **1 second** reduce
  quality pressure by one adjustment: pressure rises **0.05** toward its floor.
- Intervals at or below **103%** of target for **5 seconds** reduce pressure by
  **0.05**, probing a gradual recovery toward the ceiling. This allows recovery
  at a display's refresh interval; it does not prove unused GPU capacity.
- Each adjustment has a **1-second settling period**. Overload/recovery evidence
  is consecutive, and resets when its condition is no longer true.
- Suspension, unavailable renderer, a changed CSS viewport/DPR, zero initial
  interval, or a gap over **250 ms** discard timing evidence, retaining quality.
  Hidden/recovery time is not accumulated as render-load evidence.

Pressure is a bounded 0–1 policy value. At 1, default floors are half the ceiling
resolution scale, half its LOD pixel/texel scale, 256 shadow texels, one comparison
sample and at most one shadow map. Existing lower budgets remain lower: the
absolute scale/detail minimum is 0.25 and an authored zero shadow-map budget stays
zero. At 0 the exact ceiling settings return. Shadow resolution drops by successive
halvings at pressure 1/3, 2/3 and 1; nine-tap filtering becomes one-tap at 0.6;
map count decreases during the upper half of the pressure range.

All existing DPR, dimension, pixel and device limits still apply. If the backing
canvas is already capped by another limit, changing the scale may initially have
no effect. No target is guaranteed: CPU work, content without LODs, refresh-rate
limits, browser scheduling and minimum budgets can make it unattainable. The panel
reports its smoothed interval, pressure, adjustment count and floor state. These
are feedback observations, not measured FPS gains or GPU bottleneck attribution.
Existing CPU/GPU metrics and the [profiling timeline](profiling-timeline.md) remain
separate diagnostic evidence.

## Portable policy and renderer integration

```cpp
#include "engine/adaptive_quality.hpp"

engine::AdaptiveQuality adaptive; // Application-owned, no retained scene/backend.
auto settings = engine::AdaptiveQualitySettings{};
settings.targetMs = 1000.0 / 60.0;
adaptive.start(renderer.quality(), settings);

// After each eligible completed frame on the renderer's owner thread:
if (adaptive.observe(intervalMs))
    renderer.setQuality(adaptive.effective());

// On hidden/device/scene/resize boundaries:
adaptive.resetEvidence();
// To disable and restore the selected presentation ceiling:
adaptive.stop();
renderer.setQuality(adaptive.effective());
```

`AdaptiveQuality` / `AdaptiveQualitySettings` live in `engine_core`. The controller
owns only validated values, bounded counters/timers and an exponential average.
No allocations, clocks, renderer types, callbacks, subscriptions or scene handles
are retained. `observe(intervalMs, eligible=true)` returns whether effective
quality fields changed. Invalid/nonpositive samples are ignored; `eligible=false`
discards timing evidence. `start` validates ceiling/settings before replacing
state. `configure` validates new settings, retains pressure and recomputes the
effective policy; callers must apply `effective()` after configuration. `stop`
does not call a renderer. Applications own sampling eligibility, lifetime and
publication at idle presentation boundaries.

The ceiling's MSAA, post-processing, compression, pixel/DPR caps, cascade count,
shadow distance and LOD enable flags remain unchanged. `RenderQuality::detailScale`
is a new independently validated 0.25–1 multiplier (default 1). Both renderers
apply it to existing per-camera/per-shadow projected LOD dimensions. It preserves
viewports, projection/frustum, shared source geometry, authored fixed LOD levels,
skinned/morph full-detail rules, physics and authoritative transforms. Available
static LOD chains are reused; no geometry is generated by adaptation.

The host owns one optional controller, samples after rendering and applies changes
before the next frame through existing `Renderer::setQuality`. Ordinary attachment
recreation, capability fallback and device/context recovery remain renderer-owned.
`EngineQuality.snapshot().adaptive` and performance JSON expose its transient
state and ceiling; quality snapshots include effective `detailScale`.

For deterministic acceptance only, `?adaptiveTest=1` replaces automatic sampling
with explicit `_engine_adaptive_sample(ms,count)` calls (1–2000 samples/call).
It uses the same controller/publication path and labels the source **test replay**.
The export rejects calls without that query. Normal runs report **frame intervals**.
This fixture is separate from measured real-frame browser acceptance.

See [validation](adaptive-quality-validation.md), [rendering quality](rendering-quality.md)
and [architecture](../architecture.md).
