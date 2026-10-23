# Multiplayer milestone 3: prediction and reconciliation

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


[Milestone 4](multiplayer-presentation.md) now adds remote interpolation and local
correction smoothing to these same MP03 clients.

Authored-scene clients now predict their own player movement at 60 Hz. Each input
has a sequence number. The server consumes at most one queued input per existing
60 Hz fixed tick, then snapshots the authoritative transform with the last applied
sequence. Clients discard acknowledged inputs and replay the remaining inputs
from that transform. Remote actors continue to display authoritative snapshots.
The editor Host/Join actions automatically use this behavior on browser and Windows.

The selected static scene/template profile from [milestone 2](multiplayer-scenes.md)
is retained. Prediction affects client presentation only; it never writes the
authoring document or changes the authoritative session records. Physics, arbitrary
component replication and general physics rollback remain extensions. M4 adds
remote interpolation and positional visual correction smoothing. This is deterministic replay of the example's kinematic movement,
not a general physics rollback system.

## Portable API

`engine/network_prediction.hpp` adds header-only `engine::net::PredictionHistory`
to `WebEngine::Network`. It holds opaque state bytes, acknowledged/issued sequence
numbers and up to 128 pending input payloads. State and input each have the existing
256-byte payload bound. It has no engine_core, scene, transport or clock dependency.

Initialize with `reconcile(0, authoritativeState, step)`. `push(sequence, input,
step)` requires consecutive increasing sequences. `reconcile(ack, state, step)`
validates the acknowledgement, removes acknowledged inputs and replays retained
inputs into a candidate state before publishing. Regressing/future acknowledgements,
invalid sequence/budget and replay failures preserve the last valid state/history.
Repeated acknowledgements may carry new authoritative corrections. `reset()` clears
the complete lifetime; the next session starts again at zero.

The application supplies a pure deterministic `Bytes(const Bytes&,const Bytes&)`
step callback. It is borrowed only during each synchronous call. All calls belong
to the owner thread. External side effects in callbacks cannot be rolled back by
the history container. Applications retain authority, validation and scene mapping.

## Authored-scene integration

The example now uses session schema **MP03**, intentionally incompatible with MP02
clients. Existing saved scene packages remain valid. The M1 arena retains MP01.
Scene snapshot payloads are the existing 36-byte local transform followed by an
explicit little-endian uint32 applied-input acknowledgement. The example removes
that acknowledgement before calling the unchanged TransformReplicas API.

Client and server share `moveStep` with normalized direction, .05 units per
1/60-second step and the existing +/-32-unit template-relative bounds. The server
accepts only owned movement requests, validates consecutive input sequences and
caps each player's queue at 128. Receiving packets does not advance simulation.
Disconnect removes the player and its queued inputs. A command represents exactly
one step; it does not leave held velocity running without further commands.

Clients sample using accumulated host time, capped to 50 ms per update/catch-up.
They stop issuing commands when prediction history is full; long stalls slow local
progress rather than creating an unbounded backlog. Snapshot acknowledgements are
validated against issued inputs before session/history publication. Leave,
reconnect, shutdown and invalid traffic cancel pending prediction. Existing focus
cancellation supplies neutral input; already transmitted commands remain subject
to server authority. Local transforms are projected from replayed state while
session records remain authoritative and unchanged.

## Run and verify

Use the existing [scene build and run commands](multiplayer-scenes.md). No editor
configuration is needed to enable prediction. For development latency injection:

```powershell
$env:MULTIPLAYER_DELAY_MS='120'
node examples/multiplayer/server.cjs build/windows/Release/multiplayer_scene_server.exe 8177
```

The gateway delays session traffic by that many milliseconds **in each direction**,
clamped to 0–500; bootstrap remains immediate. Default is zero. Each socket has at
most 64 pending deliveries and one FIFO timer per direction; close cancels them. This is an optional local fault simulator.

Run native and Node/Wasm `network_scene_tests`, then
`scripts/multiplayer-scene-smoke.cjs` with Playwright/pngjs in NODE_PATH. The smoke
defaults to 120 ms per direction and tests both editor publishers and both runtime
pairs. It checks local prediction leading delayed authority, eventual stationary
convergence, native acknowledgement/prediction telemetry, rendered authored meshes,
late join, ownership/content rejection, reconnect, despawn and shutdown.
See [validation](multiplayer-prediction-validation.md) for actual evidence.
