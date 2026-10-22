# General region streaming

Driving Stage 8 adds optional portable static scene-content residency in
`engine/region_stream.hpp`. Applications own interest selection, predicted-motion
queries, loading UI, teleports, checkpoint policy and the `AssetManager` registry.
No renderer or browser type crosses the API. Existing terrain streams remain
independent owners; this module does not take their pages or duplicate their graph.

## Authored content and source assets

`RegionManifest::create(RegionManifestData)` validates immutable metadata: one
scope, 1..128 records with unique stable keys, finite strict world AABBs, separate
optional visual/collision logical asset IDs, byte allowances and selected
`requiredOnRestore` collision keys. Scope/key length is 1..24 ASCII letters,
digits, underscore, dash or dot. Bounds lie within +/-1,000,000 m. A record needs
at least one source. Each part allowance is 1 KiB..16 MiB. Asset IDs use the
existing 1024-byte logical-key contract. Restore keys must be unique and refer
to records with collision sources.

An ordinary `RegionManifestComponent(name, manifest)` persists this definition,
without ticking or starting IO. Its hierarchy world transform must stay identity;
bounds and payload transforms are world authored. `configure()` replaces only a
complete valid immutable candidate while the hierarchy is idle. Built-in prefab
factory `RegionManifestComponent` accepts `shared_ptr<const RegionManifest>`.

Register immutable `RegionSource` assets through `AssetManager`. A source holds
one part's built-in SceneArchive bytes, up to 16 MiB. `create(part, bytes)` validates
the transport size/type; full bounded decode and content validation happen before
publication. `capture(part, span<const Actor* const>)` clones 1..128 selected actors
and validates/encodes them. It preserves their local transforms and saved IDs;
prepare top-level world-authored roots. The source scene remains unchanged.

This increment accepts ordinary top-level Actor roots with ordinary Node grouping:

- Visual: exact static MeshComponent (no skin, skeleton or morph targets) and
  LightComponent. Mesh bounds must fit the manifest AABB.
- Collision: exact registered solid static RigidBodyComponent / ColliderComponent.
  Every root needs a static body. Authoritative Jolt body AABBs must fit the region.

All source nodes must be enabled; validation walks disabled branches too and
rejects them. An inactive target Scene suspends publication/eviction and reports
readiness blocked until reactivated. Content cannot tick or contain cameras, views, application data, vehicles, dynamic
bodies, custom behavior or scene references. Derived terrain/route components and
FX are not automatically split or streamed. Prepare explicit static geometry and
collision sources. Collider shapes can reuse compound shapes and physical surfaces.
The descriptor's readiness volume declares required geometry presence; it does not
prove continuous drivable ground inside the entire box.

## Runtime ownership and scheduling

```cpp
RegionStream stream(scene, assets, manifest); // scene and assets outlive stream
const RegionInterest interests[] = {{"road0", 20, true, true}};
assets.update();                              // owner thread, no blocking wait
stream.update(interests);
if (stream.collisionReady(predictedVehicleBounds)) {
    // Application may advance movement/simulation.
}
```

Keep one owner for each scope and keep the target Scene world transform identity.
Construction rejects a transformed target; later inactive/transformed scenes block
publication, eviction and collision readiness. Calls run on the idle scene owner thread. Loaders
can complete immutable resources from workers; they never capture or mutate the
scene. Publication decodes into a separate staging Scene, validates all nodes,
identity conflicts, bounds and budget, then adopts ordinary actors into the target
and synchronizes physics. Failure rolls back that part; an already valid visual
or collision part remains intact. New handles use existing scene registration.

Interest lists contain <=128 known keys with priority -1,000,000..1,000,000 and
independent visual/collision flags; duplicate interests merge by maximum priority.
Higher priorities win admissions. Collision precedes visual at equal priority;
required restoration and body pins have higher internal priority. Unwanted parts
are cancelled/evicted. Lower-priority unpinned parts can be evicted for a new
admission. Loading counts against residency and reservations. A failed part is
reported until `retryFailed()`; collect unused AssetManager cache entries when
retrying rejected immutable content or supply a corrected asset version/manifest.
AssetManager already replaces failed/cancelled IO generations on a new request.

Defaults: 8 resident/loading regions, 4 in-flight parts, 2 new starts and 2 ready
publication attempts per update, 512 nodes/part, 1024 tracked moving bodies,
64 MiB reserved, 2 m body-pin margin. Settings validate bounded ranges. Each active
part reserves **three times its declared allowance** for encoded, decoded and
cooking work; encoded bytes and decoder allocations are independently checked
against that allowance. This is admission accounting, not a hard process-memory,
GPU-memory or Jolt allocator cap. AssetManager caches, provider buffers and shared
assets are separately application owned. Publication/cooking is synchronous and
bounded by content limits; there is no worker-based scene construction or frame-time
latency guarantee. `stats()` and `residency()` expose waiting, errors, pins,
reservations, starts, publication, cancellation and eviction.

## Physics safety, crossing and identity

Every update scans active registered nonstatic rigid bodies, including sleeping,
kinematic and sensor bodies, using authoritative `PhysicsWorld::bodyBounds()`.
Their AABBs plus `bodyPinMargin` pin intersecting collision regions. Additional
explicit pin AABBs (<=128) cover wheel/suspension queries, motors, projectiles or
application consumers outside body footprints. Choose margin/extra pins for those
consumers. Unregistered bodies or a body-scan cap overflow fail closed: no support
collision eviction and readiness stays false. More pinned regions than the budget
produces backpressure, never forced removal under a body.

`collisionReady(bounds)` requires union coverage from registered ready collision
regions. Holes, outside-manifest space, missing/failed parts and safety-blocked
state return false. Subtraction work is bounded to 8192 operations/1024 fragments;
exhaustion also returns false. `visualReady(key)` is independent. The engine never
silently brakes or moves actors: applications must gate predicted/swept movement,
request ahead, and wait for destination collision before teleporting. Pausing only
rendering/culling never changes simulation residency. Pins protect current bodies;
they do not predict arbitrary future movement or guarantee a gameplay support plane.

Saved resident IDs are `rg:<scope>:<key>:v|c:<sourceSavedId>`, source IDs <=48 bytes.
`resolve(key, part, sourceSavedId)` returns a borrowed node only while that part is
resident; `regionOf(ActorId)` maps a resident actor. Eviction invalidates handles;
reloading retains saved IDs but creates fresh generation-checked runtime handles.
Keep persistent application entities outside region payloads and store logical
keys/IDs for unloaded references. Do not mutate, destroy, reparent or rename managed
resident content; editor operations expose it as read-only.

`clear()` refuses to remove ready collision needed by current bodies/extra pins
and cancels pending work. Release/move consumers and update with cleared extra pins
before final clearing. Destroy the stream while Scene/AssetManager still live and
the hierarchy is idle. If clearing is unsafe, scene-owned resident actors remain
until scene destruction; destruction must not pull support out under bodies.
Requests are dropped safely and late completions cannot access the scene. Normal
application teardown destroys the stream before its scene.

## Persistence and restoration

Scene/application envelope **v31**, reading v1..31, adds manifest records/resources.
Runtime resident roots carry a private transient marker: archive node indexing,
child writing and resource discovery omit those entire subtrees. Requests, caches,
residency statuses and runtime handles are never saved. `SceneArchive::cloneSubtree`
preserves source identities/resources for detached payload preparation; ordinary
`duplicateSubtree` still regenerates identities. Resident subtrees cannot be copied
as authored content.

`checkpoint(requiredCollisionKeys, applicationData, views, assets, codecs)` clones
the persistent scene, finds the unique matching authored manifest scope, records
the selected required collision keys and writes caller-selected opaque world state
through the existing application-data channel. The authored scene/manifest is not
modified. Persistent entities outside caches retain existing archive behavior.

Load into an unpublished/paused scene, construct a new stream for its restored
manifest and drive IO/update. Required keys remain requested without interests.
`restorationReady()` stays false on missing collision; `finishRestore()` is the
explicit application handoff and succeeds only once required parts are ready.
Then automatic restore interests are released. The application owns when to bind
or resume gameplay and how to interpret world state. No solver-exact checkpoint,
automatic per-region dynamic-state persistence or seamless world rebasing is added.

## Editor and example

`EditorSession::createRegionManifest`, `selectedRegionManifest` and
`setRegionManifest` provide grouped exact-state undo/redo with atomic validation.
The Details **Region streaming** section authors scope, keys, source IDs, world
bounds, part allowances and restore requirements; it adds/removes records and draws
DPR-correct world-box overlays. World transforms are read-only for manifests.

Open `?editor=1&scene=regions`. The seven-road example is application code in
`src/region_example.hpp`. Its isolated scene, camera, manager and stream have a
4-region/2 MiB reservation limit. Controls expose collision delay/failure, focus,
retry, driving and deferred teleport. Live rows show each part's state and pins.
A real VehicleComponent launches at 65 m/s; its example policy gates fixed simulation
on predicted footprint collision readiness. The synthetic loader measures delay
in preview steps (one 1/60 s step per presentation frame), not network seconds.
The preview is a diagnostic example, not a generic vehicle controller.

Capture a selected static Actor as a separate visual/collision `.wescene` source.
Register validated source files under logical IDs in the preview session; scene
files retain references, not uploaded source buffers. Arbitrary application loaders
must be registered by the consumer. Unknown references report failures. Close the
preview before editing, Play, opening/replacing or saving the authoring document.
Renderer bindings release borrowed preview scene/camera references before teardown;
source document/history and isolated runtime state remain separate.

See [recorded validation](region-streaming-validation.md).
