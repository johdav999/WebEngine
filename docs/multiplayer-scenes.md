# Authored multiplayer scenes (milestone 2)

[Current M8–M10 contracts](multiplayer-runtime.md) add runtime prefabs and
gameplay RPCs/events, [server physics](multiplayer-physics.md), and
[guest lobbies/recovery](multiplayer-lobbies.md). The current scene schema is MP09
with WES4 component/invocation manifests; compatible WES2/WES3 remain readable.
Earlier stage-only restrictions/schema names below are historical. See
[current browser/Windows evidence](multiplayer-runtime-validation.md).

[Milestone 6](multiplayer-authoring.md) adds saved ownership/relevance authoring,
server-owned mesh actor replication and session diagnostics. M6 used MP06; the MP05 descriptions below refer to M5. M7 introduced the registered-component package extension; the current profile is linked above.


[Milestone 3](multiplayer-prediction.md) now adds prediction/reconciliation to
these clients. [Milestone 5](multiplayer-replication.md) adds deltas/interest and
upgrades the current session schema to MP05. Saved packages remain compatible.

Milestone 2 adds an isolated authored-scene session to both editors. The author
selects a root mesh actor as the player template and publishes the scene to the
development server. Each admitted player receives an instance of that actor,
including its mesh hierarchy and materials. The server owns movement, spawning,
ownership and despawning. Late joiners receive the complete scene before session
admission, then the current transform snapshot. Source documents and edit history
remain in their editor; closing or leaving a client does not apply runtime changes
back to the source. This is multiplayer Play in a separate client window/tab,
not collaborative editing.

## Supported scene profile

The explicit first authored-scene profile admits ordinary Scene, Node, Actor,
MeshComponent, CameraComponent and LightComponent nodes. It requires one active
camera view and one enabled root mesh actor selected by saved identity. The player
template can contain mesh hierarchies but cannot contain cameras or lights.
Unsupported components, custom codecs and ticking actors reject before publication,
including unsupported components in disabled subtrees. No behavior is silently
stripped. All assets are embedded in the existing scene archive.

Limits are 4 MiB for the entire scene package, 32 MiB of archive-accounted
allocations, 2048 nodes, 256 resources, depth 32 and 262144 elements per archive
collection. The existing 16-peer/session and 64-entity/snapshot limits remain.
This profile is suitable for small authored visual test scenes. Physics/collision,
skeletal/animation gameplay, terrain components, arbitrary component replication,
runtime level editing and general prefab/schema registration are future work.

## Run

Enable `WEBENGINE_BUILD_MULTIPLAYER_DEMO=ON` using the [shared build setup](multiplayer.md).

```powershell
cmake --build build/windows --config Release --target network_scene_tests multiplayer_scene_server webengine_editor --parallel 6
cmake --build build/web-assets-release --target network_scene_tests web_assets --parallel 6
npm.cmd ci --prefix examples/multiplayer --ignore-scripts
node examples/multiplayer/server.cjs build/windows/Release/multiplayer_scene_server.exe 8177
```

Serve the freshly generated `dist`. In either editor, open a supported scene,
select its player mesh actor, enter `ws://127.0.0.1:8177`, and choose **Host selected
actor**. In the opened client choose **Join / Reconnect** to publish and join.
Other editor instances use **Join authored scene**; standalone browser clients use
`multiplayer/scene.html`, and Windows clients use `multiplayer_scene_windows.exe
--server ws://127.0.0.1:8177` (optionally `--d3d12`). WASD/arrows move the player.
Browser clients also have pointer buttons. Leave removes the player's instance;
reconnect receives fresh content and a new peer identity.

Start with a plain mesh/camera/light scene, rather than a demo that contains game
components. A reproducible fixture can be produced with
`build/windows/Release/network_scene_tests.exe build/evidence/scene-fixture`;
open its `courtyard.wescene` and select **Player template**.

The first valid publication freezes server content for that process lifetime.
Further publishers must provide byte-identical content. Use **Join authored
scene** to join an existing server without publishing local edits. Restart the
server to test a different scene. A rejected publication leaves the existing
session intact. A join attempted before publication waits up to 30 seconds.
The M1 fixed arena and its **Multiplayer test** action remain available separately.

## Engine contracts and lifetimes

`engine_network_scene` / `WebEngine::NetworkScene` is an optional layer above
`engine_network` and `engine_core`. Include `engine/network_scene.hpp`.
`saveNetworkScene(scene, views, assets, playerSavedId)` serializes a versioned
profile envelope over the existing archive, then validates it through the same
loader used by server and clients. `loadNetworkScene(bytes)` returns a fully
validated separate `NetworkScene` (`SceneDocument` plus player-template saved ID).
No archive version or generic component codecs change. Runtime handles are never
serialized as network identity.

`encodeTransform` / `decodeTransform` encode nine finite little-endian IEEE754
float32 values (position, Euler rotation, scale), each bounded to absolute 1e6.
`TransformReplicas(scene, actorTemplate)` copies a detached template and resolves
network entity IDs through generation-checked ActorId handles. `apply(records)`
validates the entire snapshot and prepares new instances before publishing poses,
spawns and removals. Kind 1 denotes a transform. The caller owns authority and
owner validation. All methods run on the idle scene owner, which must outlive
apply/find calls. Scene owns instances; `apply({})` removes them. Destruction
releases only the template/handles. Renderers never own authoritative state.

`engine/network_content.hpp` adds an ordered bootstrap stream to `engine_network`.
WEC2 messages have little-endian magic, total byte count and offset followed by
up to 16384 bytes. A zero-total/zero-offset packet requests content. Other packets
must contain exactly the expected next chunk. `ContentReceiver` stages at most
4 MiB; completion only makes bytes available, and callers still validate their
content type before publication. Duplicate, out-of-order, oversized and incomplete
streams cannot install a scene. Hosts own cancellation and deadlines.

The current scene example uses MP09 (M2 used MP02; M3/M4 used MP03; M5 used MP05; M6 used MP06; M7 used MP07; M8 used MP08). Bootstrap completes before Hello;
the server sends two content chunks per service wakeup, while publishers send at
most 60 chunks per second independently of rendering frequency. The gateway retains its packet/rate/buffering limits; pending
admission expires after 30 seconds. The native C++ server owns monotonic simulation
time and runs the existing Application fixed traversal. Example movement is
kinematic at 3 units/second within +/-32 units of the template's local starting
position, with normalized diagonal input and one acknowledged command per 60 Hz step. These are
example rules, not engine policy. Clients send intent, never authoritative poses.
Local player presentation uses the [M3 prediction/reconciliation contract](multiplayer-prediction.md); remote players use [M4 interpolation](multiplayer-presentation.md).

Client scene installation happens at the idle host boundary: stage/validate the
document, bind new views, transition Application, and release the old scene after
renderer borrows have moved. The authored camera fills the standalone viewport.
Leave cancels transport and pending transfer and clears player replicas; the last
static scene remains visible. Browser blur neutralizes input, and hiding/leaving
the page disconnects. Native focus loss neutralizes input.

DesktopEditor exposes optional `setMultiplayerSceneLauncher` and
`openMultiplayerScene(endpoint, publish)`. The synchronous callback borrows source,
views, assets and selected identity; an empty identity means Join. The host owns
packaging and process policy. Play, pending document changes and active transform
gestures gate publication. The sample writes a temporary package consumed by the
child; it does not mark the document saved. Browser export likewise does not mark
saved or cancel edits. An expiring, single-use IndexedDB handoff transfers the
package to a same-origin, opener-isolated client tab.

## Validation and limits

Run CTest `network_scene`, `network`, `multiplayer_arena`, `desktop_editor`, the
Node/Wasm `network_scene_tests.js`, and `scripts/multiplayer-scene-smoke.cjs` with
Playwright/pngjs available in NODE_PATH. The smoke serves `dist`, publishes from
both editor hosts, joins the opposite runtime, verifies actual rendered authored
meshes/movement, source preservation, late join, ownership/content rejection,
reconnect, despawn and shutdown. See [recorded validation](multiplayer-scenes-validation.md).

The gateway remains loopback-only and unauthenticated development infrastructure.
TLS termination, accounts, internet hosting, general physics
rollback and physical Android/iOS acceptance are not added by this milestone.


Multiplayer [milestone 4](multiplayer-presentation.md) now interpolates remote players and smooths
local reconciliation corrections automatically in authored-scene Host/Join clients.
