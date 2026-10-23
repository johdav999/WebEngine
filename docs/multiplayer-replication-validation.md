# Multiplayer milestone 5 validation

Verified 2026-10-23 using native Release x64, Chrome 155.0.8059.85 and repository
Emscripten 6.0.11. The smoke served fresh dist and launched actual native clients
through the Windows editor, with browser editor Host/Join controls on both
renderers. The first run published from the browser; the second from Windows.

Native CTest network, network_scene, multiplayer_arena and desktop_editor passed
4/4. Node/Wasm network_tests, network_scene_tests and multiplayer_arena_tests passed.
The final extended network_tests also passed natively and under Node/Wasm.
New protocol cases cover sparse patches (including offset 255), independent peer
baselines, automatic own-entity inclusion, removal/re-entry, keyframe cadence,
size fallback, metadata replacement, explicit force, recovery, command-sequence
independence, hidden-record ownership rejection, invalid selection retry, duplicate
operations, missing targets, out-of-range offsets, all truncations, reconstructed
capacity and fresh-peer baseline reset. Rejections preserve valid client state.

Live tests injected 120 ms per direction, 40 ms ordered jitter, one server
correction and one unavailable delta baseline per socket. Every browser/native
client requested one resync and accepted one full recovery. The browser left the
native player's relevance range and returned; its own player remained present.
Both clients recorded remote removals/re-entries while prediction and presentation
continued. Existing movement visuals, source archive bytes/revision/history,
late join, ownership/content rejection, reconnect, despawn and shutdown passed.

| Runtime | Applied deltas | Full packets | Resync recoveries | Accepted bytes / equivalent full | Reduction |
|---|---:|---:|---:|---:|---:|
| webgpu | 1384 | 74 | 1 | 82536 / 132056 | 37.5% |
| Windows dawn | 307 | 18 | 1 | 22236 / 45288 | 50.9% |
| webgl2 | 73 | 5 | 1 | 5574 / 10224 | 45.5% |
| Windows d3d12 | 301 | 17 | 1 | 21700 / 44616 | 51.4% |

Savings compare accepted snapshot packets with full packets for those same
filtered record sets. Bootstrap, commands, controls, discarded recovery deltas
and TCP/WebSocket framing are excluded. This is a small-scene traffic measurement,
not a total network bandwidth or large-session performance claim. Both browsers
recorded two remote entries and one exit before reconnect; native clients recorded
four entries and three exits across distance changes, the invalid ownership peer
and browser reconnect. No browser page errors were recorded.

Browser movement used keyboard events; native smoke used scripted portable input.
Browser and native captures were inspected. Evidence is in
build/evidence/multiplayer-m5/report.json, artifact-hashes.json, *-client.png,
*-editor.png, *-interest-exit.png / *-interest-reentry.png, dawn/ and d3d12/ native
reports/captures and native/server logs. Native editor.json reports confirm
documentPreserved and rendered for both backends.

This establishes the delivered static-scene kinematic profile on desktop Chrome
WebGPU/WebGL2 and native Windows Dawn/direct D3D12. It does not establish physical
mobile, Safari/Firefox, large-peer throughput, internet deployment, unreliable
transports, arbitrary component replication or physics rollback. M1's portable
protocol/arena regression checks passed; no new live M1 arena run is claimed.

See [contracts, limits and commands](multiplayer-replication.md).
