# Multiplayer milestone 5: deltas and interest filtering

[Current M8–M10 contracts](multiplayer-runtime.md) add runtime prefabs and
gameplay RPCs/events, [server physics](multiplayer-physics.md), and
[guest lobbies/recovery](multiplayer-lobbies.md). The current scene schema is MP09
with WES4 component/invocation manifests; compatible WES2/WES3 remain readable.
Earlier stage-only restrictions/schema names below are historical. See
[current browser/Windows evidence](multiplayer-runtime-validation.md).

[Milestone 6](multiplayer-authoring.md) adds saved ownership/relevance authoring,
server-owned mesh actor replication and session diagnostics. M6 used MP06; the MP05 descriptions below refer to M5. M7 introduced the registered-component package extension; the current profile is linked above.


Authored-scene sessions now send each peer only relevant dynamic players and
encode small changes against that peer's last snapshot. Browser and native
Windows clients reconstruct complete validated records before M3 prediction and
M4 presentation consume them. Both editors' existing **Host selected actor** and
**Join authored scene** controls use this automatically. Source documents and
history remain isolated.

The scene example uses schema **MP05 (`0x4d503035`)**. Rebuild server and clients
together; older MP03 scene clients reject the mismatch. Saved WES2 packages and
scene archives are unchanged. The separate M1 MP01 fixed arena still sends full
snapshots and remains compatible.

## Portable API and ownership

Include `engine/network.hpp`, link `WebEngine::Network`. On the single server
owner thread, call `ServerSession::snapshot(allRecords)` once per publication to
validate and replace the **complete** authoritative ownership registry. Its full
packet remains available to existing consumers. Then call
`snapshotFor(peer, relevantEntityIds, forceFull=false)` at most once for each
ready peer at that publication. Relevant IDs must exist and be unique. All
records owned by that peer are included even if omitted from the selector.
Selection is application policy; the portable module owns no scene, spatial
index, position, clock, transport or callback.

Each peer owns an independent bounded baseline, discarded on disconnect. The
first packet, an explicit force, a resync request or 20 publications since the
last full packet causes a full keyframe. Otherwise the server builds a delta:
new entities and owner/kind/payload-size changes are complete upserts; absent
entities are removals; equal-size payloads use sparse changed-byte patches.
Unchanged entities cost no entity data. A delta is used only when its encoded
size is smaller than the corresponding full snapshot. Records reconstruct in ID
order. Never interpret relevance as authorization: command target ownership is
checked against `allRecords`, independently of what any client can see.

`ClientSession::receive(packet)` validates and reconstructs a candidate record
set before replacing records/sequence. Invalid lengths, IDs, duplicate/overlapping
operations, missing removal/patch targets, patch offsets, capacity, schema,
session or sequence reject without partial publication. `BaselineMismatch`
(a `std::invalid_argument`) specifically identifies a missing/wrong baseline.
`requestSnapshot()` creates a Resync control message without advancing gameplay
command sequence. The server validates the ready peer and session, then coalesces
requests into one force-full flag for the next `snapshotFor` call.

The scene client requests a keyframe once on mismatch, retains valid records and
prediction, and skips deltas until a validated full packet arrives. Reconciliation
and remote timeline changes are staged with the reconstructed records. Removed
replicas and their interpolation histories are discarded; re-entry starts a new
visual instance/history with the same network ID. Leave/error/reconnect reset
baselines, prediction, presentation and replication diagnostics together.

## Transport and bounds

This is a **reliable ordered WebSocket** contract. After generating a peer packet,
send it in order or close that peer on failure. The server advances its baseline
when the packet is produced, not when the client acknowledges receipt. There is
no UDP/unreliable-channel support. Baseline checks, explicit recovery and periodic
full packets bound recovery after a missing baseline; this is not a general
acknowledged transport or congestion-control protocol.

WEN1 envelope version 1 adds types Delta=5 and Resync=6. After its seven uint32
header fields, Delta contains baseline sequence, upsert count and ordinary
records, removal count and IDs, then patch count and patches. A patch contains
entity ID, change count, then (uint8 offset, uint8 value) pairs in strictly
increasing offset order. Counts/IDs are uint32 little-endian. Baseline must be
nonzero and less than the packet sequence. Resync carries the current session
and peer in the header and sequence zero, without a payload.

Existing limits remain 16 peers, 64 reconstructed records, 256 bytes per payload,
32768 bytes per packet and 64 queued transport messages. Each operation list has
at most 64 entries; each patch has 1–256 changes. Temporary candidates are bounded
and validated before publication. No archive, scene-lifecycle, scheduler, renderer
or dependency boundary changes are required.

## Example relevance and editor behavior

The scene server selects remote players by distance in its authoritative local
XZ plane: enter within **4 units**, retain through **5 units**, remove beyond
5. This hysteresis avoids rapid toggles around a boundary. The client's own
player always remains present. Selection runs with snapshot publication, about
20 Hz under the existing gateway wakeups; server fixed ticks remain 60 Hz.
Filtering only affects dynamic replication. The complete static authored scene
is still transferred at bootstrap. It is not asset streaming or a secrecy boundary.

These radii are example gameplay policy in `scene_server.cpp`, not engine API
defaults or editor scene settings. Both editor launch paths share this server
policy and the same portable client reconstruction. No edits apply back to the
source, and ordinary editor Play remains separate.

Diagnostics (`Client::json`, browser `multiplayerState`, native smoke reports)
include delta/full counts, resync requests/recoveries, interest entry/exit counts,
accepted snapshot bytes and equivalent full-packet bytes. `wireSnapshotBytes`
counts accepted snapshot packets, excluding bootstrap, control, commands and
discarded recovery deltas; savings describe that traffic only.

## Build and verification

Use the pinned local SDK from `.emscripten-version` and existing
[scene setup/run commands](multiplayer-scenes.md). Build `network_tests`,
`network_scene_tests`, `multiplayer_scene_server`, `multiplayer_scene_windows`
and `webengine_editor` on Windows; browser `network_tests`, `network_scene_tests`
and `web_assets`. Run native CTest network/network_scene/multiplayer_arena/
desktop_editor, Node/Wasm protocol and scene tests, then
`scripts/multiplayer-scene-smoke.cjs` against fresh `dist`.

The smoke adds `MULTIPLAYER_TEST_BASELINE=1` to the existing latency/jitter and
correction test modes. The gateway changes one Delta baseline per socket to a
valid but unavailable earlier sequence, so both clients must request and accept
a full recovery. The switch defaults off and does not reorder/drop the reliable
transport. The smoke also moves the browser outside the native player's interest
range and back, checks both clients' removal/re-entry diagnostics, byte savings,
prediction, smoothing, rendered movement and both editor publishers. Native scene
smoke runs 20 seconds; M1 still runs 10. See [evidence](multiplayer-replication-validation.md).

General component replication, quantization, compression, spatial indexing,
adaptive bandwidth budgets, acknowledged/unreliable transports, internet identity
and deployment, physics rollback and physical mobile acceptance remain future work.
