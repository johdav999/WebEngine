# Multiplayer M8–M10 validation

Verified in the local shared Windows checkout on 2026-10-23. This covers
[runtime prefabs/RPCs](multiplayer-runtime.md), [authoritative bodies/collisions](multiplayer-physics.md)
and [guest lobbies/recovery](multiplayer-lobbies.md). Unrelated dirty/concurrent
checkout changes were retained; this is local evidence, not a clean upstream build.

## Contract and service checks

Release native CTests passed **10/10**: network_lobby, network_gameplay,
network_components, network_scene, network, multiplayer_arena, desktop_editor,
editor_content, editor_structure and persistence. The five corresponding networking
Wasm executables also passed under the repository-local Emscripten 6.0.11 Node
24.19.0 runtime: lobby, gameplay, components, scene and network.

The gameplay suite exercises prefab registration/IDs/capacity, initializer failure
without publication, idle traversal, registered PrefabAsset reuse, ownership,
independent invocation/movement sequences, queue bounds, payload truncation,
manifest mismatch, permissions/history/duplication, private events and late join.
It also runs actual server falling/settling/contact delivery, owner-checked impulses,
physics codec corruption/NaN rejection and passive clients without local drift.

`tests/network-lobby-tests.cjs` uses real WebSockets and separate C++ room processes.
It passed invitation/session authentication, pre-auth gameplay rejection, host-only
publication, room/membership capacity, roster/roles, room isolation, retained
player position and owned prefab, rotated-token/active-login rejection, old-epoch
command rejection, transient-event non-replay, grace expiry, explicit leave and
room/process cleanup. Core tests independently cover bounded codecs, handshake,
roster corruption, suspend/resume and old snapshot/RPC/event rejection. These are
functional bounds/security assertions, not public-service penetration/load testing.

## Rendered gameplay and editor matrix

Fresh `dist` was served from an isolated local HTTP server with Chrome
155.0.8059.85. `scripts/multiplayer-scene-smoke.cjs` passed:

| Browser renderer | Native Windows renderer | Content publisher | Result |
| --- | --- | --- | --- |
| WebGPU | Dawn/WebGPU | Browser editor | Passed |
| WebGL2 | Direct D3D12 | Native editor | Passed |

Both pairs exercised real authored prefab ownership/permissions, registered fields,
physics settings, undo/redo, save/reopen and isolated Host/Join. They proved actual
spawn/despawn and component values on both clients, an already-existing prefab on
late join, forbidden foreign-target/despawn and unknown-prefab rejection, private
pulse events with actual handlers, server collision events, authoritative owner
impulses and **zero client solver bodies**. Earlier prediction, interpolation,
delta/interest, injected baseline fault/resync, content rejection, remote despawn
and shutdown assertions also passed with 120 ms gateway delay and 40 ms FIFO jitter.
Pixel changes establish visible movement/authored visuals, alongside state assertions.

Evidence is under `build/evidence/multiplayer-final`: `report.json`, browser PNGs,
native `dawn`/`d3d12` reports and PPM captures, source round trips and
`artifact-hashes.json`. Earlier `multiplayer-m8`/`multiplayer-m9` evidence is retained
as historical stage evidence, not substituted for this final matrix.

## Authenticated lobby and recovery matrix

`scripts/multiplayer-lobby-smoke.cjs` passed both WebGPU/Dawn and WebGL2/D3D12 pairs.
Each run created a room from the browser editor, handed an invitation to another
browser client through the editor's memory channel, joined the native client via
the restricted inherited pipe, and checked public host/guest roster roles.

Both rendered transports were deliberately interrupted while players owned live
physics prefabs. Browser and Windows clients automatically recovered the same guest,
peer, room, authoritative gameplay state and owned object. They rebuilt current
replicas with zero local physics bodies and did not replay pulse events. A fresh
owner impulse worked after recovery. Explicit native and browser Leave removed
memberships/owned state and stopped the empty room. Source bytes/history were retained.
Reports/logs were checked not to contain invitation capabilities; screenshots keep
invitation fields masked or collapsed. Native and browser diagnostic JSON excludes
session/invitation tokens by construction.

`scripts/multiplayer-lobby-host-smoke.cjs` separately passed **Dawn and direct D3D12
authenticated creator** launches from the native editor, host-only scene publication,
forced-loss recovery, source preservation and explicit cleanup. Credentials travelled
only through the anonymous pipe and runtime memory. No invitation file/clipboard
destination was used by either workflow.

Evidence: `build/evidence/multiplayer-m10/lobby-report.json`, editor/recovered PNGs,
native reports/captures; `build/evidence/multiplayer-m10-host/host-report.json` and
native creator captures. The final manifest records hashes of the exact tested
executables, Wasm/site assets and multiplayer adapters/scripts.

## Reproduce

Use the README Windows/local-SDK setup with WEBENGINE_BUILD_MULTIPLAYER_DEMO=ON.
The repository's Emscripten pin is authoritative; do not substitute a global SDK.

```powershell
cmake --build build/windows --config Release --target network_lobby_tests network_gameplay_tests network_component_tests network_scene_tests network_tests multiplayer_scene_server webengine_editor --parallel 6
$env:TEMP=(Resolve-Path build/multiplayer-tmp).Path
$env:TMP=$env:TEMP
cmake --build build/web-assets-release --target network_lobby_tests network_gameplay_tests network_component_tests network_scene_tests network_tests web_assets --parallel 6
ctest --test-dir build/windows -C Release --output-on-failure -R '^(network_lobby|network_gameplay|network_components|network_scene|network|multiplayer_arena|desktop_editor|editor_content|editor_structure|persistence)$'
foreach ($suite in @('network_lobby_tests','network_gameplay_tests','network_component_tests','network_scene_tests','network_tests')) {
  & .tools/emsdk/node/24.19.0_64bit/node.exe ('build/web-assets-release/'+$suite+'.js')
  if ($LASTEXITCODE -ne 0) { throw $suite }
}
build/windows/Release/network_scene_tests.exe build/evidence/multiplayer-m10/fixtures
node tests/network-lobby-tests.cjs
$env:EVIDENCE_DIR='build/evidence/multiplayer-final'
node scripts/multiplayer-scene-smoke.cjs
$env:EVIDENCE_DIR='build/evidence/multiplayer-m10'
node scripts/multiplayer-lobby-smoke.cjs
$env:EVIDENCE_DIR='build/evidence/multiplayer-m10-host'
node scripts/multiplayer-lobby-host-smoke.cjs
```

Create the task-local TEMP directory first if absent. Provide Playwright/pngjs
through NODE_PATH and install the pinned example ws dependency as described in
[setup](multiplayer.md). Scripts bind isolated local servers, launch actual native
executables and clean up their owned processes. The creator smoke consumes fixtures
from the lobby run.

## Acceptance boundaries

Browser authoring uses real DOM controls. Native authoring uses shared EditorSession
APIs plus saved-file reopen and actual native launches/rendered UI; individual native
authoring fields were not separately driven with pointer/keyboard. No physical mobile,
Safari/Firefox, public internet, TLS deployment, service-load or long-duration
performance acceptance is claimed. Recovery is guest/process-lifetime state, not
account login or crash-durable persistence. The example player remains kinematic;
client physics prediction/rollback, character-body collision integration, joints,
vehicles and ownership transfer remain outside this profile.
