# Multiplayer milestone 6 validation

Verified 2026-10-23 using native Release x64, desktop Chrome 155.0.8059.85 and the
repository Emscripten 6.0.11 SDK. Fresh dist and actual Windows client/editor
executables were used. Captures of authoring fields, client diagnostics and native
rendering were inspected.

Six native CTests passed: network_scene, network, multiplayer_arena,
desktop_editor, editor_content, editor_structure. Node/Wasm network_scene_tests
passed with the new authoring cases, and network_tests passed separately.
The added cases exercise structural/property undo/redo, creation/removal, exact
save/load and duplicate round trips, invalid cross-field/ownership preservation,
unknown/duplicate annotation rejection, deterministic server actor catalogue,
Distance/Always/OwnerOnly relevance and hysteresis, multiple visual templates,
stable entity/kind identity, despawn/re-entry and legacy unannotated WES2 loading.

The rendered smoke uses the browser's actual Add replication settings button and
typed fields, sets entry/exit radii to 6/7, checks undo/redo, saves bytes and reopens
the file before hosting/joining. The Windows editor smoke drives EditorSession
authoring APIs, writes authored.wescene, reopens it and verifies the values. It
then launches the real client through its Host/Join GUI button. Both native
editor.json reports confirm authoringRoundtrip/documentPreserved/rendered. Native
authoring field pointer/keyboard interaction is not separately established by
these programmatic authoring steps.

The fixture contains one explicitly server-owned, always-visible orange landmark.
The test server nudges it by one X unit after 60 fixed ticks; every client receives
owner 0, kind 2, entity 65536 and the authoritative X=1 pose. Attempts to command
another player's entity and the server actor both reject. Browser screenshots show
the server actor remains while distance filtering removes the other player.

| Browser | Windows | Publisher | Browser authoring/save-load | Native authoring/save-load | Server actor + diagnostics | Movement pixels |
|---|---|---|---|---|---|---:|
| webgpu | dawn | browser editor | PASS | PASS | PASS | 1845 |
| webgl2 | d3d12 | windows editor | PASS | PASS | PASS | 1833 |

Both runtime pairs also passed M3 prediction/reconciliation, M4 interpolation and
correction decay, M5 deltas/traffic reduction/baseline recovery, distance exit and
re-entry, source bytes/revision/history preservation, late join, content mismatch,
reconnect, despawn and shutdown. Faults were 120 ms per direction, 40 ms ordered
jitter, one correction and one unavailable delta baseline per socket. No browser
page errors were recorded. Browser movement used keyboard input; native client
movement used scripted portable input. Native Diagnostics is visible in captures.

After this matrix, a final server-only admission guard was added to reserve entity
IDs >=65536. A direct bridge boundary test admitted/disconnected 65535 peers,
rejected identities 65536 and 65537, and exited normally. This verifies the final
server admission boundary; the four-renderer matrix was not rerun after that guard.
Client/rendering behavior was unchanged by the guard.

Evidence: build/evidence/multiplayer-m6/report.json, identity-boundary.json,
artifact-hashes.json, *-authoring.png, *-client.png, *-editor.png, interest exit/
reentry captures, native/server logs, dawn/ and d3d12/ authored.wescene,
editor.json, report.json and client.ppm/png. The hash manifest records final files,
including the server with the subsequently tested identity guard.

These checks establish the bounded static mesh/camera/light scene profile with
declarative settings on desktop Chrome WebGPU/WebGL2 and Windows Dawn/direct D3D12.
They do not establish arbitrary gameplay-component replication, client authority
transfer, physics rollback, physical mobile or Safari/Firefox acceptance, internet
hosting or large-session performance. M1 portable protocol/arena regressions
passed; no new live M1 arena run is claimed.

See [authoring contracts and commands](multiplayer-authoring.md).
