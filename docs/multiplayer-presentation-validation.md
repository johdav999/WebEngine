# Multiplayer milestone 4 validation

Verified 2026-10-23 with native Release x64, Chrome 155.0.8059.85 and local
Emscripten 6.0.11. Fresh dist files and actual native clients were used.

Native CTest network_scene, network, multiplayer_arena and desktop_editor passed
4/4. Node/Wasm network_scene_tests passed. New cases cover delayed midpoint
interpolation, endpoint holding, shortest angle paths, equal-time coalescing,
capacity, stale/invalid pose preservation, teleports, gaps, scale-sign cuts, reset,
correction continuity, half-life decay, repeat corrections, invalid delta retention,
large-correction snaps and equivalent decay across different frame rates.

Live tests enable 120 ms session delay per direction, 40 ms deterministic ordered
jitter and one real server correction per player after 60 fixed ticks. Both editor
publishers launched their clients through the actual Host/Join controls. Browser
and native diagnostics prove moving remote interpolation, nonzero correction
offsets, followed by convergence to predicted/authoritative positions. Existing
prediction-before-authority, source-byte preservation, rendered authored visuals,
late join, ownership/content rejection, reconnect, despawn and shutdown passed.

| Browser | Windows | Publisher | Native interpolated frames | Native maximum visual correction | Movement pixels | Result |
|---|---|---|---:|---:|---:|---|
| webgpu | dawn | browser editor | 33 | 0.841 units | 1832 | PASS |
| webgl2 | d3d12 | windows editor | 35 | 0.841 units | 1366 | PASS |

No browser page errors were recorded. Browser movement uses keyboard events;
native smoke uses scripted portable intents. Captures were inspected. Evidence:
build/evidence/multiplayer-m4-final/report.json, artifact-hashes.json, native logs,
server logs, browser *-client.png / *-editor.png and native dawn/ and d3d12/ captures
and reports. An earlier probe in multiplayer-m4 exposed timer delivery reordering
in the fault simulator; bounded FIFO delivery replaced those independent timers.
Only multiplayer-m4-final is the completed acceptance run.

This validates the static scene's kinematic presentation profile on desktop Chrome
WebGPU/WebGL2 and native Windows Dawn/direct D3D12. It does not establish physical
mobile, production network conditions, arbitrary component replication, physics
rollback, skeletal interpolation, adaptive clock synchronization or internet hosting.
The M1 native protocol/arena tests passed; no new live M1 run is claimed.

See [contracts and commands](multiplayer-presentation.md).
