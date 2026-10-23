# Multiplayer milestone 2 validation

Verified on 2026-10-22 from this working checkout with the repository-local
Emscripten 6.0.11 SDK, native Release x64 builds and Chrome 155.0.8059.85.
The final smoke served fresh `dist` files. Source and artifact hashes are in
`build/evidence/multiplayer-scene-final/artifact-hashes.json`.

## Portable contracts

- Native CTest: `network_scene`, `network`, `multiplayer_arena` and
  `desktop_editor`: **4/4 passed**.
- The same scene-package/replica tests passed in Node/Wasm.
- Focused checks cover source-byte preservation, saved-ID selection, embedded
  archive round-trip, ordered multi-chunk transfer, duplicate/out-of-order
  rejection, malformed/unsupported content, stale-handle replacement and despawn.
- Full-snapshot rejection includes NaN and singular scale in a later record;
  existing transforms and uncreated instances remain unchanged.
- A simulated 1000 Hz client-update loop verifies bootstrap publication remains
  capped at 60 chunks/second. Disabled unsupported components also reject.
- Native scene-server import inspection showed only Windows/C++ runtime libraries;
  it has no SDL, Dawn or D3D12 dependency.

## Live runtime and editor acceptance

`scripts/multiplayer-scene-smoke.cjs` starts a real headless C++ scene server behind
the local Node/WebSocket gateway. It opens the actual browser editor, loads the
authored courtyard archive and activates Host/Join. The native editor opens the
same source and activates its retained GUI Host/Join control, launching a real
native client. The authored pink scaled-sphere player template, orange landmark
and blue platform are rendered in every client.

| Browser | Windows | Scene publisher | Changed movement pixels | Authored player pixels | Native frames | Result |
|---|---|---|---:|---:|---:|---|
| webgpu | dawn | browser editor | 1475 | 962 | 493 | PASS |
| webgl2 | d3d12 | windows editor | 1200 | 952 | 504 | PASS |

Both runs verified 246927-byte scene transfer, correct scene/template identities,
late join into the existing authoritative state, movement observed across runtimes,
ownership-violation rejection, differing-publication rejection without damaging
the accepted scene, manual Leave/Reconnect with fresh peer IDs, remote despawn and
server-shutdown cleanup. No browser page errors were recorded. Browser movement
uses actual keyboard events; native smoke movement uses scripted portable intents.
Native physical keyboard/controller injection is not established by that smoke.

Browser source archive bytes, revision and undo history remained unchanged. Native
editor source archive bytes, identity and revision remained unchanged. Normal
editor Play state remained separate. Native/browser captures were inspected; the
native scene editor displays the selected source actor and the new Host/Join
controls, while both clients display the authored replicas.

Evidence: `build/evidence/multiplayer-scene-final/report.json`, `native-*.log`,
`dawn-server.log`, `d3d12-server.log`, browser `*-client.png` / `*-editor.png`, and
native `dawn/` / `d3d12/` `client.png`, `editor.png`, `report.json`, `editor.json`.
Earlier probe outputs in `build/evidence/multiplayer-scene` include the corrected
fixture tint assertion and are not final acceptance evidence.

## Scope

This verifies the bounded static scene profile and replicated player-template
transforms on desktop Chrome WebGPU/WebGL2 and native Windows Dawn/direct D3D12.
It does not establish arbitrary gameplay/component replication, collision,
prediction/reconciliation, mobile hardware, Safari/Firefox, hostile internet
deployment, authentication, saturation performance or production hosting.
See [delivered contracts and commands](multiplayer-scenes.md).

## M1 regression

The original `scripts/multiplayer-smoke.cjs` also passed on both runtime pairs
against the final `dist`, including native transport lifecycle tests and existing
editor launchers. Evidence: `build/evidence/multiplayer-m2-regression/report.json`.
