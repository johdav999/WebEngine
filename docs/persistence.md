# Persistence and browser storage

`engine_core` adds versioned application snapshots over the existing
[scene archives](scene-archives.md). `ApplicationArchive` is an opt-in serializer:
applications select scenes, payloads, checkpoints and when to publish a restored
document. Browser adapters in `web/storage.js` only store bytes. The playground
offers **Scenes & assets → Browser saves → Save progress / Open progress** for
one explicit IndexedDB checkpoint, with deletion. It never autosaves or restores
without a request; the existing file workflow remains available.

## Stable saved identities

Every `Node` receives a generated `savedId()`, independent of its display name and
scene-scoped `ActorId`/`ComponentId`. A random 64-bit process prefix and monotonic
64-bit counter produce defaults; they are identifiers, not authentication tokens.
`setSavedId("level.player")` assigns an application identity at an idle boundary.
IDs contain 1–128 ASCII letters, digits, `.`, `_`, `-` or `:`. Archives reject
duplicates anywhere in a scene, including disabled nodes and required implementation
children. Adoption/reparenting preserves IDs; applications importing two copies
of the same archived hierarchy must assign distinct IDs before saving them together.
Prefab-created instances get new IDs through normal construction.

`scene.findSavedNode(id)` resolves a borrowed node, including disabled nodes. Missing,
ambiguous or pending-removal identities return null. After loading, resolve the node
and use its fresh runtime handle for delayed work. Never serialize a runtime handle
or retain a node pointer across scene replacement. IDs are scoped to the saved scene;
cross-scene references use `(scene.savedId(), node.savedId())` under application policy.
Versions 1–3 receive fresh identities once during loading; saving as version 4 retains
them. Changing an identity requires the application to update its saved references.

## Custom component codecs

An application-owned `ComponentCodecs` registry maps a stable format key and schema
version to an exact C++ component type. Pass it as the final argument of scene/file
save/load and application save/load. Built-in codecs cannot be replaced. Duplicate
keys/types, missing functions and zero versions are rejected without modifying the
registry. Actor/Scene subclasses still require a separate application construction
policy and are rejected by scene archives.

```cpp
engine::ComponentCodecs codecs;
codecs.add({
    "myapp.counter", typeid(CounterComponent), 2,
    [](const engine::Component& component) {
        return encodeCounter(dynamic_cast<const CounterComponent&>(component));
    },
    [](std::uint32_t version, std::span<const std::byte> payload) {
        // Validate bounds and upgrade supported older data explicitly.
        return decodeCounter(version, payload); // unique_ptr<CounterComponent>
    },
    [](engine::Component& component, engine::Scene& scene) {
        auto& counter = dynamic_cast<CounterComponent&>(component);
        auto* actor = dynamic_cast<engine::Actor*>(scene.findSavedNode(counter.targetSavedId));
        if (!actor) throw std::invalid_argument("Missing saved target");
        counter.target = actor->id();
    }
});
auto bytes = engine::SceneArchive::save(scene, views, {}, {}, {}, codecs);
auto candidate = engine::SceneArchive::load(bytes, {}, codecs);
```

The example encode/decode functions belong to the application. Payloads should use
explicit portable field encodings, not struct padding, pointers or `size_t` layouts.
Decoders receive a non-owning byte span valid only during the callback. They return
the registered detached, childless component type; archive common settings and
ordered children are restored afterward. Schema versions above the registered version
are rejected before decoding. The decoder must reject older versions it cannot upgrade.

Resolvers run after the complete scene hierarchy and views are decoded, before
`Scene::synchronize()`. They can resolve forward references and rebuild callbacks,
but must not mutate the hierarchy, saved IDs or unrelated live state. A resolver
failure discards the staged scene. Loading initializes ownership/resources without
executing variable or fixed ticks. Callbacks are copied for each synchronous archive
operation; captures must remain alive through it. No global executable registry is
loaded from a file. Unknown types never silently become base components.

Custom resource payloads are owned/validated by their codec; shared built-in resource
tables are not exposed to codecs. Opaque byte vectors count against archive byte and
allocation budgets. Codec-internal allocations and callback side effects cannot be
budgeted or rolled back by the engine; keep callbacks bounded and confined to the
candidate. Runtime audio emitters/listeners and other unsupported built-in types
remain unsupported unless the application supplies an appropriate custom codec.

## Versioned application state

```cpp
#include "engine/persistence.hpp"

engine::ApplicationStateCodec schema;
schema.application = "myapp";
schema.version = 2;
schema.validate = validateCurrentState; // Throws on invalid version-2 payload.
schema.migrate = migrateOneVersion;     // (N, bytes) -> version N+1 bytes.

const engine::SavedScene saved[] = {
    {&world, worldViews, &retainedAssets, perSceneMetadata}, {&menu}
};
auto bytes = engine::ApplicationArchive::save(
    schema, applicationBytes, saved, world.savedId(), paused, {}, codecs);
auto document = engine::ApplicationArchive::load(bytes, schema, {}, codecs);
// Stage application-specific bindings/behaviours and publish at an idle boundary.
// document.scenes owns scenes, views/assets and each scene's applicationData.
```

The application document records application identity, schema version, payload,
the saved active scene's root identity, explicit pause policy and selected scenes.
An empty active identity allows state-only or inactive-scene snapshots. Active
identities must name a saved scene, and saved scene root identities must be unique.
Each nested scene has its own shared-resource table; sharing across scenes is not
deduplicated. Input, clocks, pending transitions, suspension, renderer state and
application lifecycle hooks are reconstructed deliberately rather than checkpointed.
The serializer does not inspect or mutate a running `Application`.

Loading validates application identity and rejects future schemas. Older payloads
migrate one version at a time (at most 64 steps), then current-version validation
runs. Missing/throwing migrations, invalid state, invalid nested scenes or trailing
bytes discard the entire document. Save validates the current payload before
encoding. Byte and tracked allocation budgets cover the envelope, nested scene
buffers and decoded payloads; node/resource budgets apply across all selected scenes.
Migration outputs count against allocation budgets. Existing archive accounting
excludes object/allocator overhead, callback allocations and GPU/Jolt internals.
CRC32 detects accidental corruption; it does not authenticate data.

Application envelopes use `WESCENE\0` format version 4, content kind 3, and the same
length/CRC header as scenes/assets. Application schema version is separate from
engine format version. Scene and asset readers reject application content and vice
versa. Store the returned bytes through any host byte adapter; native applications
can supply their own storage policy.

## Browser adapters

```js
const storage = new EngineStorage.IndexedDBStorage({namespace: 'myapp'});
const abort = new AbortController();
await storage.put('checkpoint', applicationBytes, {signal: abort.signal});
const bytes = await storage.get('checkpoint'); // Uint8Array, or null if absent.
const keys = await storage.list();
await storage.remove('checkpoint');
storage.dispose();

// Explicit alternative for small payloads; there is no silent fallback.
const small = new EngineStorage.LocalStorageStorage({namespace: 'myapp-settings'});
```

Both adapters expose promise-based `put/get/list/remove` and `dispose`. Namespace
and slot keys follow saved-ID syntax. IndexedDB stores structured byte records in
an application-namespaced database and reports write success only on transaction
completion. localStorage uses one versioned length/base64 record per key; it is
synchronous internally and replaces with one `setItem`. Default per-record limits
are 512 MiB for IndexedDB and 2 MiB of raw bytes for localStorage; browser quota can
be lower and base64 requires extra space. Up to 10,000 keys can be listed.

Input bytes are copied immediately before queued work, so a caller can release Wasm
memory. Reads return owned bytes. Operations on one adapter execute in invocation
order; separate instances/tabs follow browser transaction ordering, with last
committed writer winning. Multi-tab merge/conflict policy remains application-owned.
Storage never validates application/scene semantics: decode and validate a staged
candidate before publication. A malformed stored record does not remove valid data.

An `AbortSignal` cancels queued work and aborts active IndexedDB transactions;
replacement rolls back before commit. Disposal rejects queued work, interrupts
opening operations, aborts active transactions and closes the owned connection.
Cancelled writes that already committed cannot be undone. localStorage cancellation
is checked before its synchronous replacement. Version changes close the connection;
blocked opens reject visibly and late connections are closed. Failures use
`StorageError.code`: `invalid`, `unavailable`, `quota`, `blocked`, `cancelled`,
`closed`, `corrupt`, or `io`. Rejected operations do not poison the adapter queue.

`EngineStorage.capabilities()` reports IndexedDB availability, a storage estimate
and persistent-storage status when supported. `requestPersistence()` is an explicit
application action. These are browser reports rather than guaranteed disk capacity.
Storage remains origin-bound and can be cleared or evicted. See the browser's
[IndexedDB transaction guidance](https://developer.mozilla.org/en-US/docs/Web/API/IndexedDB_API/Using_IndexedDB)
and [quota/eviction rules](https://developer.mozilla.org/en-US/docs/Web/API/Storage_API/Storage_quotas_and_eviction_criteria).

## Verification

`persistence_tests` covers stable/recreated identities and handles, forward references,
custom schema upgrades and failure recovery, exact reserialization, no load-time
ticks, application migration/validation and aggregate budgets. Existing scene archive
tests cover built-in assets, effects, physics and version-1 compatibility.
`node scripts/persistence-smoke.cjs` serves its selected `SITE_DIR` on an owned
loopback port and checks real IndexedDB/localStorage, cancellation rollback, quota
recovery, namespace isolation, page reloads, malformed checkpoints and exact WebGPU
pixels. Supply `NODE_PATH` and `BROWSER_EXECUTABLE` as for other browser smoke scripts.
Evidence is saved under `build/evidence/persistence/`; mobile viewport checks are
Chromium emulation and do not establish physical Android/iOS acceptance.
