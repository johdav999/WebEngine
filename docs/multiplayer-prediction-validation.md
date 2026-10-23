# Multiplayer milestone 3 validation

Verified 2026-10-23 on Chrome 155.0.8059.85, native Release x64 and repository-local
Emscripten 6.0.11. Final artifacts served from freshly built dist.

Native CTest network_scene, network, multiplayer_arena and desktop_editor passed
4/4. Node/Wasm network_scene_tests passed. New checks cover correction/replay,
future acknowledgement and duplicate sequence rejection, replay failure retaining
valid history, backlog limits, reset, and server commands producing no movement
until fixed ticks consume them. Ten ticks apply exactly ten steps and acknowledge
sequence ten. Existing source/transfer/invalid-transform/lifetime checks passed.

The actual browser and native editor Host/Join controls launched sessions under
120 ms injected session-message latency in each direction. Both browser clients
predicted before delayed authority, then converged after input stopped. Native
reports likewise show predicted movement ahead of authority, advancing applied
acknowledgements, and equal final predicted/authoritative positions.

| Browser | Windows | Publisher | Native maximum prediction lead | Movement pixels changed | Result |
|---|---|---|---:|---:|---|
| webgpu | dawn | browser editor | 0.800 units | 1333 | PASS |
| webgl2 | d3d12 | windows editor | 0.950 units | 1585 | PASS |

Both runs also verified authored visuals, late join, ownership/content rejection,
Leave/Reconnect with a fresh peer, remote despawn, shutdown and source archive byte
preservation in both editors. No browser page errors were recorded. Browser input
uses keyboard events; native smoke uses scripted portable movement intents.
This is desktop-browser/native-Windows evidence, not physical mobile, production
network conditions, arbitrary gameplay or general physics rollback acceptance.

Evidence: build/evidence/multiplayer-m3/report.json, artifact-hashes.json,
native-*.log, dawn-server.log, d3d12-server.log, browser *-client.png/*-editor.png,
and native dawn/ and d3d12/ client.ppm, editor.ppm and report.json.
The M1 arena was unchanged; its native protocol/arena tests passed in this run.
Earlier live M1 evidence remains historical rather than a new live M1 run.

See [contracts and commands](multiplayer-prediction.md).
