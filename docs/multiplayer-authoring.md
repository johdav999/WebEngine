# Multiplayer milestone 6: editor authoring and diagnostics

[Current M8–M10 contracts](multiplayer-runtime.md) add runtime prefabs and
gameplay RPCs/events, [server physics](multiplayer-physics.md), and
[guest lobbies/recovery](multiplayer-lobbies.md). The current scene schema is MP09
with WES4 component/invocation manifests; compatible WES2/WES3 remain readable.
Earlier stage-only restrictions/schema names below are historical. See
[current browser/Windows evidence](multiplayer-runtime-validation.md).

Both editors can now save ownership and relevance settings on root mesh actors.
Scene sessions use the selected player template plus optional server-owned mesh
actors, with the same validation, replication and diagnostics on browser and
native Windows. Runtime simulation and client presentation remain isolated from
the editor source and history. This is authoring for the supported kinematic
scene profile, not arbitrary gameplay-component replication or collaborative editing.

## Author in either editor

Select an enabled root mesh actor and choose **Add replication settings**.
Select its new **Replication settings** component to edit the Multiplayer fields:

| Field | Values / default | Meaning |
|---|---|---|
| Ownership | Player template (default), Server actor | A per-peer visual instance, or one persistent server-owned actor |
| Relevance | Distance (default), Always visible, Owner only | Server-side selection policy; own player always remains present |
| Enter radius | 4 units | Distance at which a remote actor first appears |
| Exit radius | 5 units | Distance through which an already visible actor remains |

Radii must be finite, at least .1, enter <= exit, and exit <= 10000.
Owner only is valid for player templates, not server actors. Settings apply to
the whole actor hierarchy. There is one settings component per configured actor.
Edits, creation and removal use ordinary editor undo/redo, dirty tracking,
save/load, recovery, cloning and duplication. Invalid edits preserve valid settings.

The selected **Host selected actor** template must have Player ownership. A player
without settings uses the historical defaults. Other configured actors must use
Server ownership. Plain unconfigured actors remain static bootstrap content.
Configured actors must be enabled root mesh actors containing mesh visuals and
inert settings only; camera/light children and unsupported components reject.
There can be at most 48 server actors, reserving space for the 16 session players
within the 64-record limit. The static content/profile limits remain unchanged.

Use the existing [scene server/build commands](multiplayer-scenes.md), then
**Host selected actor** or **Join authored scene** and **Join / Reconnect** in the
isolated client. A selected settings component resolves to its owning actor when
hosting. Changing source settings after publication does not change the running
server; restart it to publish different content. Later publications must still
byte-match the accepted package.

## Portable configuration and persistence

`engine/network_authoring.hpp` defines `ReplicationSettings`,
`ReplicaOwnership::{Player,Server}` and
`ReplicaRelevance::{Distance,Always,OwnerOnly}`. It has no dependency on the
network protocol or adapters. It uses existing Actor/Component types and stores
declarative configuration in an ordinary, unticked Component's namespaced tag.
This deliberately reuses the existing archive, subtree-copy and editor-history
representations; no scene archive or WES2 envelope version changes.

`makeReplicationMetadata(settings={})` returns a detached Component. Adopt it on
the actor on the idle scene owner. `replicationSettings(component)` validates and
returns settings; the Actor overload returns an optional and rejects duplicates.
`setReplicationSettings(component,settings)` validates and stages a complete
ComponentSnapshot before publication, preserving unrelated tags and common state.
`isReplicationMetadata(node)` recognizes the reserved annotation on an exact
ordinary Component. It is a marker check; use the typed getter to validate it.

The reserved tag is `WebEngine.Replication.v1:ownership:relevance:enterBits:exitBits`.
Fields are decimal uint32 values; radii store IEEE754 bits for exact round trips.
Unknown annotation versions, duplicate annotations, malformed fields and invalid
settings reject network publication. Invalid annotations remain inert in ordinary
scene archives; inspectors omit their typed fields, so the component can still be
removed. The network profile additionally requires enabled, childless, unticked
metadata at identity transform directly beneath the root mesh actor.

`EditorSession::addComponent("NetworkReplication")` creates the annotation through
existing structural history. The built-in property registry exposes
`network.ownership`, `network.relevance`, `network.enter`, `network.exit` on valid
metadata. Both editor frontends consume the same registry. Engine core and
standard-only `engine_network` acquire no networking/scene dependency from these
optional authoring helpers. The editor uses header-only annotation helpers;
the scene package/runtime layer interprets the settings.

## Runtime catalogue, authority and lifetime

`NetworkScene::replicated` contains validated `ReplicatedActor` definitions with
saved identity, settings, record kind and optional server entity ID. The selected
player has kind 1 and entity 0, meaning a per-peer instance. Server definitions
are sorted by saved ID, receive kinds starting at 2 and entity IDs starting at
`serverEntityBase` (65536), and use owner 0. Clients validate these identities and
ownership against the content catalogue before committing records or histories.
These IDs are session/content mappings, never local runtime handles.
The scene server admits at most 65535 peer identities over its process lifetime;
it rejects further admissions until restart, preserving the reserved server-entity
range and existing peers. IDs are not reused after disconnect.

The scene server publishes actual transforms for the retained server actors;
their records persist when players disconnect. Client commands cannot target
them because ordinary global ownership checks remain in force. The example adds
no automatic motion/behavior in normal use. Its explicit correction test mode
also nudges server actors after 60 fixed ticks to prove authoritative replication.

`TransformReplicas(scene, map<kind,const Actor*>)` snapshots a detached prototype
for each kind. Existing single-player-template construction still works. Inputs
validate kind, payload and stable entity/kind identity before pose/spawn/despawn
publication. Re-entry starts a fresh handle after removal. Scene owns live actors;
the adapter owns prototypes and handles and borrows the scene on its idle owner
thread. The scene must outlive apply/find calls. Prototype pointers are borrowed
only during construction. Original template actors are hidden in client scenes
after prototypes are staged, preventing duplicate static/dynamic visuals.

`relevantTo(settings,owned,wasVisible,distance)` implements selection with caller-
supplied finite nonnegative local XZ distance. The server uses each entity's
settings, independently of full global ownership and simulation. Own players are
always included. Relevance remains an application policy, not visibility-based
authorization or a secrecy boundary. M3 replay, M4 smoothing and M5 deltas/recovery
continue to consume complete reconstructed records.

M6 introduced **MP06 (`0x4d503036`)**; the current profile uses MP09. Rebuild server and clients together.
Legacy packages without annotations still load with the old player defaults.
Annotated archives retain the same ordinary format; older multiplayer profiles
may reject the annotations. M1 MP01 remains separate. No new scheduler phases,
renderer logic, transport protocol fields or archive migrations are introduced.

## Diagnostics and verification

In browser clients expand **Session diagnostics**. Windows clients use the
**Diagnostics** button. They show authoritative and predicted X, correction size,
pending inputs, delta/full counts, recovered baselines and accepted snapshot bytes.
Browser diagnostics additionally show displayed X, acknowledgement, equivalent full
bytes and each entity's kind/owner/pose. These read-only views inspect the isolated
session; they never modify source or server authority. Counts/ranges refer to
visible records, not a server-wide player directory. Leave/reconnect reset state.

Build native `network_scene_tests`, `multiplayer_scene_server`, `webengine_editor`
and browser `network_scene_tests`, `web_assets` with the repository SDK. Run CTest
`network_scene`, `network`, `multiplayer_arena`, `desktop_editor`, `editor_content`,
`editor_structure`, Node/Wasm tests and `scripts/multiplayer-scene-smoke.cjs` against
fresh dist. The smoke exercises browser authoring controls and file round trips,
native editor authoring/file round trips, both publishers, server-owned visuals,
unauthorized command rejection, diagnostics and M3–M5 lifecycle/recovery checks.
See [recorded evidence](multiplayer-authoring-validation.md).

Arbitrary component fields, client authority/ownership transfer, custom spawn
factories, physics replication/rollback, editable live sessions, internet services
and physical-mobile acceptance remain separate work.
