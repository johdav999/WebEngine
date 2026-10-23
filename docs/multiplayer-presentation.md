# Multiplayer milestone 4: presentation smoothing

[Current M8–M10 contracts](multiplayer-runtime.md) add runtime prefabs and
gameplay RPCs/events, [server physics](multiplayer-physics.md), and
[guest lobbies/recovery](multiplayer-lobbies.md). The current scene schema is MP09
with WES4 component/invocation manifests; compatible WES2/WES3 remain readable.
Earlier stage-only restrictions/schema names below are historical. See
[current browser/Windows evidence](multiplayer-runtime-validation.md).

[Milestone 5](multiplayer-replication.md) now reconstructs per-peer deltas and filters
remote players by interest before these mechanisms consume complete records.
The current scene schema is MP05; references to MP03 below describe M3/M4 history.
Saved scene packages remain compatible.


Authored-scene clients now interpolate remote actors through a small receipt-time
buffer and smooth local positional reconciliation corrections. Both browser and
Windows editor Host/Join sessions enable these defaults automatically. Local input
continues to predict immediately through the M3 movement model; neither smoother
changes authoritative records, prediction history, server simulation or source
editor documents. The MP03 wire schema and saved scene packages remain compatible.

## Engine APIs

Include `engine/network_presentation.hpp` and link optional
`WebEngine::NetworkScene` (`engine_network_scene`). These portable mechanisms use
existing Transform/Vec3 values and have no platform, renderer, scene or callback
ownership. Call from one owner thread; callers supply clock values in seconds.

`TransformTimeline(delay=.1, maxGap=.5, snapDistance=4)` retains up to 32 samples.
`push(received, transform)` validates the complete pose and receipt timestamp
before publication. Times must be finite, nonnegative and nondecreasing. Equal
times replace the newest pose, so a batch of TCP-delivered snapshots coalesces
without inventing an artificial interval. Invalid/stale inputs retain valid history.
`sample(now)` returns an optional TransformSample at `now-delay`: position and scale
interpolate linearly, and Euler angles take their shortest wrapped paths. It holds
the earliest/latest sample outside the buffer and never extrapolates. The returned
`interpolated` flag identifies samples between two retained timestamps.

First samples, receipt gaps above maxGap, position jumps above snapDistance and
scale-sign changes reset the buffer. This makes spawns, teleports, long outages and
mirroring changes snap rather than sweep through stale or singular geometry.
Settings validate delay in [0,.5], maxGap in (delay,10], and positive finite distance
up to 1e6. `reset()` clears all samples. These are local receipt-time interpolation
contracts, not server-clock synchronization or adaptive jitter-buffer control.

`CorrectionSmoother(halfLife=.08, snapDistance=4)` keeps a positional visual offset.
Call `rebase(oldPrediction, newPrediction)` only when authoritative reconciliation
changes predicted position. It accumulates the compensating offset; ordinary input
steps require no rebase. `present(prediction, deltaSeconds)` returns a separate pose
and exponentially decays the offset with the configured half-life. At zero delta
the correction preserves the previous visual position. Offsets below .0001 units
settle to zero. A correction or accumulated offset above snapDistance, or a frame
gap above .5 seconds, snaps. Position output respects the network transform +/-1e6
range. Rotation and scale remain the predicted values. Invalid inputs preserve
the offset. Half-life is finite in (0,1]; `reset()` clears the lifetime.

## Client and editor integration

The client stages remote timeline updates together with complete validated session
snapshots and prediction reconciliation. It removes histories on despawn. Each
presentation frame samples remote poses and projects its own corrected predicted
pose through the existing TransformReplicas actor-handle mapping. Receipt times
and presentation delta come from the existing host clock; no second scheduler or
physics update is introduced. Leave, reconnect, errors and scene replacement clear
buffers, correction offsets and diagnostics. Scene owns the visual actors.

Existing editor controls need no new configuration. The static mesh/camera/light
profile remains unchanged; the source document/history and ordinary editor Play
remain isolated. Applications using these APIs with gameplay colliders must apply
the results to presentation objects rather than their authoritative collision state.

## Run and verify

Use the existing [scene run commands](multiplayer-scenes.md). Build native
network_scene_tests, multiplayer_scene_server and webengine_editor; build browser
network_scene_tests and web_assets using the pinned local Emscripten SDK.

For development fault simulation, the gateway accepts process-local
`MULTIPLAYER_DELAY_MS` (0–500 per direction, default 0) and
`MULTIPLAYER_JITTER_MS` (0–80, default 0). Jitter varies deterministically and
preserves ordering independently in each direction. Each socket still has at most
64 pending deliveries with at most one FIFO timer per direction; close cancels
timers and queued deliveries. Bootstrap stays immediate.

`MULTIPLAYER_TEST_CORRECTION=1` passes `--test-correction` to the scene server.
Only that explicit test mode nudges each player by one bounded local X unit after
60 fixed ticks, producing a real authoritative correction without client knowledge.
Normal server movement policy does not issue these nudges. No test state is saved.

Run native and Wasm network_scene_tests, then scripts/multiplayer-scene-smoke.cjs
with Playwright/pngjs in NODE_PATH. The smoke enables 120 ms delay per direction,
40 ms jitter and the test correction. It checks moving remote interpolation,
nonzero visual correction offsets followed by convergence, prediction before
authority, actual rendering, both editor publishers, source-byte preservation,
ownership/content rejection, reconnect, despawn and shutdown on both runtime pairs.
See [recorded validation](multiplayer-presentation-validation.md).

General physics rollback, arbitrary component replication, quaternion/skeletal
interpolation, extrapolation, adaptive server-clock synchronization and internet
hosting remain separate work. Desktop browser tests do not establish mobile-device
or Safari/Firefox acceptance.
