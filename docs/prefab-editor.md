# Prefab assets, variants and instance updates

**Scene editor → Prefab assets** captures a selected built-in actor as a reusable
definition, saves `.weprefab` files, creates variants, places linked instances and
updates them while preserving local edits. Try `?scene=prefabs&editor=1` for two
linked lanterns. The example policy lives in `src/prefab_example.hpp`.

## Authoring workflow

1. Select an actor and choose **Create from selected actor**, or **Open prefab
   editor → Open .weprefab**. Capturing attaches an inert instance link in scene
   history; existing nodes and resources remain in place.
2. Select a definition node. Edit its name, TRS and typed inspector properties;
   add cube/light components or remove ordinary components. Definition edits use
   a private scene and independent **Undo definition / Redo definition** history.
   The live scene changes only when placing or updating instances.
3. **Save .weprefab** exports the definition and embedded resources. **Place
   instance** creates independent nodes sharing immutable resources.
4. **Create variant** makes a new asset identity with an immutable base revision.
   Edit and save/place it like a base prefab. Variants can derive from variants.
5. **Update linked instances & variants** stages the current definition, rebases
   known descendant variants and updates matching instances in one scene command.
   A failed merge keeps the scene and published asset catalog unchanged.
6. Select a linked instance in the main scene to inspect local differences.
   **Revert instance overrides** restores the current session definition, retaining
   root placement. **Unpack instance** removes its link while keeping the objects.

Close retains pending definition edits in the session asset catalog. Scene/editor
rebinding clears that catalog; save asset files to keep definitions that have no
placed instance. Scene files retain applied definitions, variant bases and links.
They do not retain unpublished asset documents or their history. Files remain local.
Play and other previews gate prefab authoring. Async file reads check binding
generation, editor state and workbench lifetime before adopting a candidate.

## Merge and override rules

Definition-local saved IDs are stable keys; display names can change. Each instance
maps keys to its own saved identities and holds the immutable definition revision
it last received. Update performs a three-way comparison between that previous
definition, the current instance and the incoming definition. Returning a field
to its previous definition value removes the inferred override.

Local typed inspector values, individual TRS axes, full tag lists, mesh/texture
bindings and per-slot material values override incoming values. Root placement
always belongs to the instance, even when it happened to equal the old definition.
Ordinary actor-level added/deleted children, reparenting and child order are merged;
new upstream children appear where no local override conflicts. Unmapped/new
instance children retain independent identities.

An upstream deletion of a locally edited subtree is a conflict. Type changes,
missing parents, key collisions, required-child changes, conflicting nested revisions
and unsupported configuration overrides reject before publication. Configuration
outside the property inspector is checked through the existing archive codecs;
unexposed edits are rejected rather than silently replaced. Component-owned child
structure overrides require unpacking; protected skeletal/FX/route children remain
managed by their owner. Nested linked prefabs survive capture, instantiation and
archive round trips. The definition workbench can nest another immutable source;
dependency refresh preserves supported local overrides and rejects cycles before
publication. See [content iteration](content-iteration.md) and the
[shared native/browser frontend](editor-authoring.md).

Publication exchanges complete staged actor subtrees on the idle scene owner.
Saved identities survive; runtime handles are recreated through normal registration.
Resolve borrowed instance nodes again after updates. A grouped undo restores all
prior ownership, state and links, with stale-state/parent guards. Updates containing
cameras reject to protect borrowed renderer view lifetimes; unpack those instances
before editing them in place. Capturing, placing and saving camera definitions is
supported. No updates run automatically during simulation or Play.

## Portable APIs and ownership

```cpp
#include "engine/prefab_asset.hpp"
#include "engine/editor_prefab.hpp" // Optional engine_editor operations.

auto asset = engine::PrefabAsset::capture(sourceActor, "Lantern");
scene.adoptChild(asset->instantiate());
auto bytes = engine::PrefabAssetArchive::save(asset);
auto loaded = engine::PrefabAssetArchive::load(bytes);

engine::PrefabAssetEditor document(asset);
document.session().select(asset->definition().savedId());
document.session().setProperty("node.name", std::string("Tall lantern"));
auto next = document.snapshot("Tall lantern");
const std::array updates{next};
sceneEditor.updatePrefabs(updates); // One scene undo command.

auto variant = engine::PrefabAsset::capture(asset->definition(), "Red lantern",
                                           {}, 1, asset);
auto rebased = engine::rebasePrefab(variant, next,
                                  *engine::EditorPropertyRegistry::builtins());
```

`PrefabAsset` and `PrefabInstanceComponent` are in `engine_core`. Assets own a
detached immutable actor definition and optional shared immutable base asset.
`instantiate(optionalOldBindings)` returns detached ownership with a fresh link;
normal scene attachment owns lifecycle/registration. Definitions contain existing
mesh, material, physics, animation and other built-in resource representations.
No renderer types, browser APIs, scene pointers, callbacks or global registry enter
the asset. Instances own their state; links retain no executable factory callbacks.

Optional `engine_editor` adds `PrefabAssetEditor`, `mergePrefab`, `rebasePrefab`,
`prefabOverrides` and `EditorSession::{linkPrefab,placePrefab,updatePrefabs,
selectedPrefabRoot,unpackPrefab}`. Workbench history uses an ordinary private
`EditorSession` (32 entries); scene history retains replaced ownership until eviction.
Browser session discovery/catalog publication is explicit and holds at most 64
assets. No filesystem watcher, remote asset service or automatic live update exists.

The existing [`Prefab` factory API](prefabs.md) remains the configurable C++
construction path. `.weprefab` persists supported built-in scene data; it does not
serialize arbitrary `std::any`, callbacks, game subclasses or component factories.

## Format and bounds

At most 512 definition nodes, depth 64 and 512 identity bindings; variant ancestry
has at most eight bases and rejects cycles/repeated asset IDs. Asset IDs use the
existing saved-ID rules; names are 1–256 bytes without newline/NUL; revisions are
positive uint32 values and editor revision overflow rejects. Files are at most
64 MiB, with a 128 MiB archive allocation budget and existing resource limits.
Invalid files leave the current document/scene intact.

`WEPREFAB1` wraps a checksummed scene archive carrying one asset resource. Scene
archive **v34** adds the inert instance node kind and shared prefab asset records;
readers retain v1–33 compatibility. Template-local node identities have their own
scope while resources share the ordinary archive table across definitions, bases
and instances. Duplication remaps link bindings along with normal saved identities.
No factory registry or editor history is persisted.

See [validation](prefab-editor-validation.md) and [scene archives](scene-archives.md).


## Authoritative network instances (M8)

Optional WebEngine::NetworkScene adds PrefabSpawner over existing Actor prototypes
or PrefabAsset canonical definitions; it does not create a second asset format.
The scene profile uses Runtime prefab ownership for its frozen whitelist. Scene
owns instances; the spawner owns detached templates/handle maps and explicit clear
requests deferred destruction. See [lifetime, limits and RPC authoring](multiplayer-runtime.md).
