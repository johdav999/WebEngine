# Gameplay-component replication (milestone 7)

[Current M8–M10 contracts](multiplayer-runtime.md) add runtime prefabs and
gameplay RPCs/events, [server physics](multiplayer-physics.md), and
[guest lobbies/recovery](multiplayer-lobbies.md). The current scene schema is MP09
with WES4 component/invocation manifests; compatible WES2/WES3 remain readable.
Earlier stage-only restrictions/schema names below are historical. See
[current browser/Windows evidence](multiplayer-runtime-validation.md).

Games can register their own Component subclasses for authoritative state
replication in browser and native Windows sessions. Both editors support registered
creation, typed configuration, replication audience, undo/redo, duplication and
save/load. The example provides Vitals and Progress components; the engine has no
health, inventory, score or interaction rules. An independent inventory adapter in
the tests exercises the same public API.

This extends the [authored-scene profile](multiplayer-scenes.md) and
[ownership/relevance authoring](multiplayer-authoring.md). It does not copy arbitrary
C++ memory, executable code, callbacks or unregistered component types.

## Register a component

Link `WebEngine::NetworkScene` and include `engine/network_components.hpp`.
Create a `ComponentReplicationRegistry` and add a `ReplicatedComponentType` for
each supported class. Each registration contains:

| Member | Contract |
|---|---|
| `archive` | Existing ComponentCodec: unique stable key, concrete type, archive version, data encoder and detached factory. No resolve callback in this profile. |
| `version` | Nonzero network-state schema version; change it when the wire interpretation changes. |
| `maxStateBytes` | Declared worst-case state size, 1–256 bytes. The combined actor budget is stricter. |
| `capture` | Read authoritative component state into bounded portable bytes. Never use object layout, pointers or runtime handles as the encoding. |
| `validate` | Reject all malformed, oversized, nonfinite or semantically invalid state. No live mutation. |
| `prepare` | Validate/decode and allocate a PreparedComponentUpdate without modifying the target. Returning null rejects. |
| `replica` | Configure a detached replica to suppress authoritative behavior, subscriptions and effects. |

`PreparedComponentUpdate::commit() noexcept` applies already prepared state without
allocation, callbacks, hierarchy changes or exceptions. For allocated state, stage
ownership in the plan and swap during commit. All updates for a snapshot are
prepared before any replica poses, spawns or component values are published.
Throwing validation or preparation preserves existing valid replicas and fields.
These are trusted host-code contracts, not a sandbox for untrusted factories.
Archive loading performs ordinary lifecycle synchronization, including during
package validation. Constructors and load-time callbacks must therefore be safe
before a host assigns authority; gate side effects/subscriptions on an explicit
application role. The replica hook configures detached live replica clones, not
the earlier archive-validation document. Component replication does not grant
authority merely because a component exists in a loaded scene.

The registry owns its registrations and callback values. Captures must remain valid
for each registry copy's lifetime. At most 32 unique registrations are accepted;
built-in archive types cannot be replaced. `codecs()` provides the corresponding
ComponentCodecs for scene persistence, Play and subtree copies. See
`examples/multiplayer/gameplay.hpp` for complete bounded scalar/string adapters,
archive factories, staged commits and editor properties.

## Author and persist

`ComponentReplication::{Off,Everyone,OwnerOnly}` selects each component's runtime
state audience. `setComponentReplication(component, mode)` stages a common
ComponentSnapshot, preserving unrelated tags and fields. The getter validates the
reserved `WebEngine.ComponentReplication.v1:1` or `:2` tag; no tag means Off.
Unknown, duplicate or malformed annotations reject network publication. Ordinary
archives retain them; invalid typed fields are omitted so removal remains possible.
There is no scene archive version bump.

Pass `EditorComponentSupport{codecs, factories}` as the final EditorSession
constructor argument. Each `EditorComponentFactory` supplies a stable key, label
and detached childless Component factory matching a registered codec. Creation
uses ordinary `addComponent(key)` and history. Session state owns copies of these
values. `componentReplicationProperties(registry, baseRegistry)` returns an
extended property registry exposing `network.component` (State replication), while
preserving existing fields. Games add their own typed properties through the
existing EditorPropertyRegistry. This optional header depends on editor/scene
integration; engine_core and standard-only engine_network do not depend on editor.

DesktopEditor accepts optional property registry and factory arguments after its
existing codecs argument. Browser application setup configures the equivalent
support before binding its editor; the browser adapter passes it through file
reopen and ordinary Play. Both frontends display registered Add-component choices
and the same shared properties. Registered archive bytes participate in structural
history fingerprints, preventing undo from silently discarding external changes.

In the opt-in multiplayer editor example, add **Vitals** or **Progress** to the
selected player root actor. Vitals exposes health, charges and invulnerability;
Progress exposes score and stage. Defaults are Everyone and Owner only,
respectively. Save/reopen and Host selected actor. **Activate gameplay** / Space
in the browser, or **Activate** in Windows, sends intent; the server consumes a
charge and changes authoritative health/progress. These are example game rules.
Diagnostics show values read back from actual client components.

`SceneArchive::duplicateSubtree(node, limits, codecs)` and `cloneSubtree` now
support registered detached components. Duplication regenerates saved identities;
cloneSubtree retains them for adoption into a different root. Used codecs with resolve
callbacks reject subtree copying because their saved-reference fixup requires a
complete scene. Full-scene load/clone retains its existing resolve support.

## Runtime identity, authority and application

Pass the same registry to `saveNetworkScene(..., registry)` and
`loadNetworkScene(bytes, registry)`. NetworkScene retains a registry copy and each
ReplicatedActor lists canonical ComponentBindings (slot, type key, child index and
audience). Slots are content-scoped indices, never local handles. Types can appear
more than once on an actor. The profile admits registered childless components
directly beneath enabled replicated root mesh actors, including the selected
player template and configured server actors. Other unsupported components still
reject, including in disabled subtrees.

`bindComponents` derives descriptors for a fixed authored topology.
`bindComponentHandles` binds descriptors to generation-checked ComponentIds on an
attached actor. Retain those bindings for that instance; removal/replacement rejects
even if another component of the same type occupies the same child index.
`captureComponents` reads/validates authoritative state. `encodeComponentStates`
and `decodeComponentStates` bound counts, ordered unique slots, versions, enabled
flags and byte lengths. `validateComponentStates` requires the exact expected
audience and schema. `prepareComponents` stages state and enabled flags.

Clients call `prepareComponentReplicas` on detached clones before adoption. The
host replica callback runs once there, and engine variable/fixed ticking is
disabled. Off and hidden owner-only components remain passive authored defaults;
they do not independently simulate. Any lifecycle callbacks must respect the
host's passive-replica setup. Scene ownership, deferred destruction and idle-owner
execution are unchanged. Prepared plans borrow target components only for the
immediate prepare/commit transaction; do not retain them across hierarchy edits.

TransformReplicas accepts custom codecs and an optional synchronous Stage hook.
With it, the first 36 payload bytes remain the pose; the hook prepares the rest for
each existing or staged actor. All hooks succeed before publication. Without a
hook, the existing strict 36-byte transform API remains unchanged. The scene must
outlive the adapter and its apply/find calls; prototypes and codec values are owned
by the adapter. Application callbacks are not retained by apply.

The example server captures complete state on the existing fixed simulation owner.
`selectComponentStates` filters per-component audiences before encoding a peer's
records. `ServerSession::snapshotForRecords(peer, records, forceFull)` validates
projected IDs, owners and kinds against the complete authoritative registry, then
uses the existing delta/keyframe/resync mechanism. Missing owned records are
included automatically; hosts must explicitly project owned payloads when needed.
Only trusted hosts choose payload projections. Filtering never grants command
authority. Clients send bounded action intent and cannot write replicated fields.
Prediction/smoothing alters presentation poses only; gameplay state stays server
authoritative and is not predicted or interpolated automatically.

Owner only filters runtime updates, including deltas, keyframes and resync.
Authored initial values and component schemas are present in the public bootstrap
package. Do not store secrets there. Server actors have owner 0, so owner-only
component updates are visible to no client.

## Versions and limits

M7 registered packages introduced **WES3**, containing the player saved identity, a bounded
canonical manifest and the existing scene archive. Manifest compatibility compares
the exact sorted type keys, archive versions, state versions and declared bounds
before invoking archive factories. Packages contain data only. Old unregistered
WES2 packages still load; registered gameplay components require matching manifests (current packages use WES4).

M7 introduced **MP07 (`0x4d503037`)**; the current scene example uses MP09. Rebuild server and clients
together. Each record contains transform (36 bytes), movement acknowledgement
(4 bytes), component count (4 bytes), then each slot/version/enabled/length header
(16 bytes) and its state. Publication rejects a template whose declared total
exceeds **256 bytes**, including private state. At most **8 registered components
per actor**, including Off components, are allowed. The manifest is at most 8192
bytes. Existing 4 MiB package, 16 peers and 64 entities remain. WEN1 version 1,
transport adapters, renderers, scheduler and M1 MP01 remain unchanged.

Runtime addition/removal/reordering of components, ownership transfer, physics
rollback, custom component prediction and public internet deployment remain separate
work. M8–M10 add the prefab/invocation, server-body and guest-service contracts linked above. Restart a session
to publish changed content/registrations. This is isolated multiplayer Play,
not collaborative live editing.

## Build and verify

Enable `WEBENGINE_BUILD_MULTIPLAYER_DEMO=ON` using the existing
[build setup](multiplayer.md) and pinned `.emscripten-version` SDK (6.0.11).

```powershell
cmake --build build/windows --config Release --target network_component_tests network_scene_tests multiplayer_scene_server webengine_editor --parallel 6
cmake --build build/web-assets-release --target network_component_tests network_scene_tests network_tests web_assets --parallel 6
ctest --test-dir build/windows -C Release -R '^(network_components|network_scene|network|multiplayer_arena|desktop_editor|editor_content|editor_structure|persistence)$' --output-on-failure
.tools/emsdk/node/24.19.0_64bit/node.exe build/web-assets-release/network_component_tests.js
.tools/emsdk/node/24.19.0_64bit/node.exe build/web-assets-release/network_scene_tests.js
$env:EVIDENCE_DIR='build/evidence/multiplayer-m7'
node scripts/multiplayer-scene-smoke.cjs
```

Use Playwright/pngjs in NODE_PATH, the example's installed ws dependency and a fresh
dist. `network_scene_tests.exe <output-folder>` writes both legacy courtyard and
registered gameplay fixtures. The rendered smoke tests browser WebGPU/WebGL2 and
Windows Dawn/direct D3D12, both editor publishers, applied component values,
owner-only isolation and the earlier authority/lifecycle/recovery flows. Native
authoring checks use EditorSession APIs and file round trips; native field
pointer/keyboard acceptance is a separate check. See [recorded evidence](multiplayer-components-validation.md).
