# Gameplay-component replication validation (M7)

Verified 2026-10-23 using native Release x64, Chrome 155.0.8059.85 and the pinned
repository Emscripten 6.0.11 SDK. The live smoke served fresh dist and launched real
Windows editors/clients. Browser authoring, browser diagnostics and native rendered
captures were inspected.

Eight focused native CTests passed: network_components, network_scene, network,
multiplayer_arena, desktop_editor, editor_content, editor_structure and persistence.
Node/Wasm network_component_tests and network_scene_tests passed. The component
suite includes an independent inventory class, scalar Vitals and string Progress;
it checks codec/manifest compatibility, malformed/truncated/bounded state, exact
audience, versions, enabled flags, passive replicas, staged updates, late-batch
rejection preserving live values/poses/handles, editor creation/history/duplication,
archive round trips and per-peer privacy through deltas, full keyframes and resync.
After the live matrix, additional native and Wasm cases passed for pending removal,
stale generation handles, declared combined payload budgets and the eight-component
cap. Those are test-only additions; runtime replication behavior is unchanged.

## Live editor and runtime matrix

| Browser | Windows | Publisher | Registered creation/save-load | Applied gameplay state | Owner-only filtering | Movement pixels |
|---|---|---|---|---|---|---:|
| webgpu | dawn | browser editor | PASS | PASS | PASS | 1856 |
| webgl2 | d3d12 | windows editor | PASS | PASS | PASS | 1807 |

The browser smoke uses actual Add-component controls and typed fields, creates
Vitals (health 91, charges 5), switches replication Off/Everyone with undo/redo,
creates owner-only Progress (stage ready), saves and reopens the file, then hosts
or joins. The Windows editor smoke creates/configures the same components through
EditorSession APIs, checks history and file reopen, and launches its client through
the real Host/Join GUI action. Native field pointer/keyboard interaction was not
separately tested. Both editor.json reports confirm authoringRoundtrip and source
preservation. Runtime simulation never changes source bytes/history/revision.

Browser Activate gameplay sends intent. The authoritative server consumes one
charge, decreases health and advances progress; each client's diagnostics checks
values from actual replica Component instances against reconstructed wire state.
Native client activation uses scripted portable input; its final own replica has
four charges and stage activated. Both clients receive public Vitals on other
players and server actors while Progress updates appear only for their own player.
The initial authored bootstrap is public; this is update audience filtering, not
secret-storage acceptance.

Both pairs passed the earlier movement prediction/reconciliation, remote
interpolation, correction decay, delta traffic reduction, baseline recovery,
interest exit/re-entry, server actor replication, unauthorized player/server-actor
commands, mismatched content rejection, late join, reconnect, despawn and shutdown.
Faults were 120 ms per direction, 40 ms ordered jitter, one authoritative correction
and one unavailable delta baseline per socket. No browser page errors were recorded.
Browser movement used keyboard input; native movement used scripted portable input.
The final toolbar hint was subsequently clarified to mention registered components;
that text-only change received syntax/content verification, not another live matrix.

Evidence is under build/evidence/multiplayer-m7: report.json, artifact-hashes.json,
*-gameplay-authoring.png, *-private-authoring.png, client/editor/interest captures,
native/server logs and dawn/ and d3d12/ authored.wescene, editor.json, report.json,
client.ppm/png. Hashes identify tested runtime artifacts and final relevant source.
Tests added after the live run are recorded separately above; concurrent unrelated
terrain/module edits in this shared checkout are not multiplayer acceptance claims.

This establishes registered fixed-topology component state on desktop Chrome
WebGPU/WebGL2 and Windows Dawn/direct D3D12. It does not establish runtime component
composition, arbitrary automatic reflection, RPC/event delivery, physics rollback,
internet deployment, physical mobile/Safari/Firefox or large-session performance.
M1 portable protocol/arena regressions passed; no new live M1 arena run is claimed.

See [contracts, limits and reproduction commands](multiplayer-components.md).
