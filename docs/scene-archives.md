# Scene and asset archives

Use **Save scene / Open scene** in the inspector for `.wescene` files. The file owns
all required CPU asset data; original model/image files and external URLs are never
read during loading. Edits to materials and environments survive reopening. A saved
scene remains editable and can be saved again. **Return to playground** restores the
original scene, which stays suspended while another scene is displayed.

**Export assets / Import assets** uses `.weassets` libraries. Libraries can contain
meshes, materials, textures, skeletons/clips, environments, emitter/effect definitions
and collision shapes. Import creates mesh instances and retains other assets for
subsequent scene/library saves. Libraries do not contain placement or audio.

The [scene editor](scene-editor.md) reuses this format and staged open/save workflow.
Authored transforms persist; editor selection, gizmos and undo history do not.

## Saved data

| Data | Contents |
| --- | --- |
| Hierarchy | Scene, actor, component and plain node names, ordered children, local transforms, disabled state, tags and tick flags; required effect/skeletal children are restored once |
| Geometry | Vertices, indices, tangents, material ranges, skin streams, morph targets/defaults, warnings and independent component morph weights |
| Terrain | Shared source/material, chunk references and mesh instance settings; meshes regenerate on load; optional full-resolution collision uses existing physics archives |
| Terrain detail and layers | Format-12 LOD generation thresholds/count, independent mesh camera/shadow policies, four-layer texture/color/roughness/UV definitions and linear RGBA weights; derived LOD meshes regenerate |
| Terrain editing and streaming | Format-14 eager/source-only mode, edited height/weight source and resident chunk actors/collision; source-only loads rebuild saved chunks only; runtime streaming owners and undo history are application state |
| Paged terrain | Format-15 patch interior/global-offset metadata and bounded source halo; resident snapshots load without the provider; provider revision/URLs, jobs and budgets are application state |
| Paged edits/writeback | Format 15 retains current edited patch heights/weights; page generations, dirty flags, provider transactions and conflict/history policy are separate storage/application state |
| Materials | Shared asset identity, every scalar/map/render-state setting, optional slot overrides including explicit map removal, tint/opacity and UV transforms |
| Textures | Names, RGBA8 pixels, dimensions and wrap modes; mips and GPU resources rebuild normally |
| Cameras | Parent hierarchy, projection/lens settings, normalized viewport, masks, ordered render views and optional clear colors |
| Lighting | Directional/point lights, masks, shadow settings, ambient light and exact baked environment radiance/LUT with intensity/rotation |
| Animation | Skeletons, bindings, baked clips/markers, shared controller definitions, typed instance parameters/triggers, current control/completion state, playback/loop/speed clocks, paused state, blend/crossfade source poses, root-motion joint/stripping and pending event/movement output |
| Effects | Shared definitions/textures/flipbooks, curves/bursts/seeds, independent live particles, death heap, emission clocks, RNG and paused/draining states |
| Physics | Collision shapes and geometry, body settings, actor transforms, active linear/angular velocities, gravity, layer collision matrix and debug setting |
| Movement | Character motor settings, actor transforms and persistent movement velocity commands; jump edges and vertical solver velocity restart |
| Browser metadata | Orbit/reset targets, orbit angles/distance, global pause, playground motion phase and encoded MP3/WAV bytes, name, state and position |
| Asset library | Explicitly retained unreferenced assets, alongside resources used by the scene |

Resource references are deduplicated by identity across the entire file, including
the optional library. Two components that shared one mesh/material/texture before
saving share one reconstructed asset afterward. Runtime scene handles are recreated;
old handles cannot resolve against the new scene.

The archive is an engine data format. Registered custom component subclasses use
application-owned versioned codecs; unregistered subclasses throw instead of being
silently converted to base nodes. Executable behavior, callbacks, subscriptions,
input bindings, GUI trees and profiling configuration are reconstructed by the
application. [Persistence and storage](persistence.md) describes codec registration,
stable saved node identities, application snapshots and browser byte adapters.
`applicationData` offers an opaque byte
payload for application-specific persistence. Material graph output is stored as
material parameters and baked maps; editable graph source uses the graph editor's
separate JSON save/load.

Physics bodies are rebuilt, retaining settings (including `isSensor`), transforms and active velocities.
Solver contact caches, sleep history, pending force/kinematic commands and collision
callbacks are not a deterministic solver checkpoint. Browser audio that was playing
opens paused at its saved position; press **Play** for browser audio permission.
The browser viewer creates a preview camera for files without one and uses a default
view when the archive contains no render views. Its scene tree previews up to 2,048
nodes; all archive nodes remain loaded and saved.

## In-memory subtree duplication

`SceneArchive::duplicateSubtree(node)` stages a built-in node hierarchy with shared
immutable CPU assets and fresh saved identities. It reuses validated node records
with an in-memory resource table; no new on-disk format or asset-byte copy is
introduced. Scene/protected roots and unregistered custom types are rejected.
The returned detached subtree has no copied executable callbacks or physics joints;
the editor publishes it through ordinary hierarchy ownership and synchronization.
See [editor duplication and history](scene-editor.md).

## C++ API

```cpp
#include "engine/scene_archive.hpp"

const engine::RenderView view{&camera};
auto bytes = engine::SceneArchive::save(scene, {&view, 1});
auto document = engine::SceneArchive::load(bytes);
// The old scene is untouched until the application chooses to replace it.
renderer.setScene(*document.scene, document.views);
// Keep document alive while its scene/cameras are used by the renderer.

auto assets = engine::SceneArchive::collectAssets(*document.scene);
auto libraryBytes = engine::SceneArchive::saveAssets(assets);
auto restoredAssets = engine::SceneArchive::loadAssets(libraryBytes);

// Native builds, including replacement of an existing save:
engine::SceneArchive::saveFile("world.wescene", scene, {&view, 1});
auto diskDocument = engine::SceneArchive::loadFile("world.wescene");
engine::SceneArchive::saveAssetsFile("library.weassets", assets);
auto diskAssets = engine::SceneArchive::loadAssetsFile("library.weassets");
```

`load` constructs a separate scene, validates resources and references, initializes
built-in lifecycle/physics state, then returns ownership. It throws on failure.
Save from an idle scene and call `flushPendingDestroy()` first; pending removals are
rejected by the portable API. Native saves serialize before touching the destination,
write/flush an exclusively created sibling temporary file, then atomically replace
the target. Failed writes remove their own temporary file and preserve the old save.

The browser API is `window.EngineSceneArchive`: `saveScene()`, `saveAssets()`,
`openScene(file)` and `openAssets(file)`. Open methods return a promise of a success
boolean. Scene data, application metadata and embedded audio decode are prepared
before commit; malformed files preserve the current scene and audio. Files remain
local. The save status says the download started; browser download completion is a
separate host action.

## Format and limits

Instances created by [prefabs](prefabs.md) follow the same supported-type and
resource-sharing rules. Prefab definitions, definition-local keys, instance
override records and factory registries are not stored. Registering a prefab
component factory does not register an archive codec for a custom subclass.

The archive begins with eight-byte `WESCENE\0` magic, a little-endian uint32 version,
uint64 payload length and uint32 CRC32. The payload identifies scene or asset-library
content. Scalars are explicitly encoded field by field, without struct padding or
native pointers; index fields that use `size_t` in memory encode as uint64 on disk.
This permits native 64-bit and WebAssembly 32-bit interchange. Arrays use uint32
counts; resources use typed IDs and backward references. Texture pixels and baked
assets are lossless and uncompressed, so saves can exceed the source model size.

Readers accept versions 1 through 23. Version 2 adds the sensor boolean to body
settings; version-1 bodies load with `isSensor=false`. Version 3 adds clip markers,
shared animation controller definitions, typed instance state and queued output;
versions 1/2 load with empty markers and no controller. Version 4 adds saved node
identities, custom component codecs and application-state envelopes. New scene and
asset saves write version 23. Version 5 adds optional sky backgrounds, shared
panorama pixels and per-view sky flags; older scenes load with sky off. See
[sky contracts](sky.md). Version 6 adds sphere geometry selection, atmosphere,
cloud wind phase and day/night settings. Version-5 skies load with weather and
automatic lighting disabled. Version 7 adds validated water material settings and
optional instance overrides; formats 1-6 retain their original shader meanings.
Version 8 adds refraction strength/distortion, absorption and shoreline foam
settings. Format-7 water loads with the new effects disabled. Version 9 adds
planar reflection strength; older water loads with planar capture off.
Version 10 adds shared terrain sources, terrain chunk components and terrain asset
libraries. Chunk meshes regenerate within the allocation budget; existing collider
archives preserve full-resolution physics. See [terrain contracts](terrain.md).
Version 11 adds underwater strength, fog density/color and maximum path distance;
older water retains underwater rendering disabled.
Version 12 adds optional layered materials and instance overrides, terrain LOD
generation parameters and mesh camera/shadow detail policies. Older terrain loads
with generation disabled and neutral policies; layer blending defaults off.
Version 13 adds caustics strength, scale, speed and depth fade; older water loads
with caustics off.
Version 14 adds eager/source-only terrain generation mode. Formats 10–13 terrain
remains eager. Source-only assets generate only referenced resident component
meshes on load, with conservative per-chunk allocation charges. Current edited
samples and weight maps are ordinary immutable source resources; runtime streaming
focus/budgets, pending work and edit history are not serialized.
Version 15 adds optional terrain patch interior, global sample offsets and page ID.
Only resident patches and their source halos are saved, with ordinary material and
collision resources. They regenerate in global source space without network IO;
formats 1–14 load as before. Provider configuration/revision and asynchronous
request state are not archived. Restored snapshots are static until an application
replaces their actors with a new paging owner.
Paged sculpt/paint does not change the scene archive version. A scene snapshot
includes committed local source edits even before page writeback, but does not
flush storage or retain per-page generations/dirty flags/pending write batches.
Recreate the intended loader/writer owner against its persisted dataset explicitly;
do not use a static snapshot as an assumed storage generation.
Renderer water time is caller-owned and excluded from scene archives. See [water](water.md). Trigger overlap history and executable marker response
handlers are recreated by gameplay. See [animation control](animation-control.md)
for output and controller restoration contracts.

Default limits are 512 MiB of file data and tracked CPU allocation payload, 100,000
nodes, 10,000 resources, hierarchy depth 128, and four million non-byte array elements.
Byte arrays are bounded by file/allocation budgets. Limits are configurable through
`ArchiveLimits`. Array payload/capacity accounting excludes allocator/object overhead,
GPU uploads and Jolt's internal allocations; the file, staged scene, previous scene
and browser audio decoding can coexist in memory. Files are parsed synchronously in
Wasm and large archives can temporarily pause rendering.

Load validates finite numbers, dimensions, array budgets, mesh/skin/material ranges,
enum values, required children, animation/particle state and reference types. Magic,
version, payload length, checksum and trailing-data checks precede scene publication.
CRC32 detects accidental corruption; it is not an authentication signature.

## Verification

`scene_archive_tests` covers shared resources, material override inheritance/removal,
hierarchy and stale handles, cameras/lights/shadows, environments, morphs, interrupted
crossfade and root-motion continuation, live effects, physics, all collision shape
types, physical files and overwrites, budgets, unknown types, corruption, 300
structural mutations and the complete textured Knarr model.

`scripts/scene-archive-smoke.cjs` saves actual browser downloads, checks exact rendered
canvas pixels across restoration and fresh-page loading, verifies audio position,
animation deformation, asset import, malformed-file recovery and mobile layout.
Set `NATIVE_ARCHIVE_PATH` to a native fixture for an additional compatibility check.
The native test executable also accepts `--load path.wescene` to validate and
re-serialize a browser download. Evidence belongs under
`build/evidence/scene-archive/`. These checks establish the tested Windows/Chrome
configuration; physical mobile devices and audible speaker output are separate.


## In-memory scene cloning

`SceneArchive::clone(scene, views, assets, limits)` uses the bounded built-in scene
codec with shared immutable resource references. It creates independent nodes,
views, animation/effect state and physics handles, retaining saved identities in a
separate scene scope. It serializes no geometry/image resource bytes and adds no
file-format version. The optional editor uses this for isolated Play. Host callbacks,
custom classes/codecs, input bindings, joints, GUI trees and audio voices are not
reconstructed. Unsupported classes reject before publication. See
[scene editor Play ownership](scene-editor.md#isolated-play-and-stop).

## Format 16: scene fog

Format 16 appends optional `FogSettings` to the scene payload and a `fogEnabled` flag after each view's sky flag. The validated fields are color, density, start distance, base height and height falloff. Formats 1–15 default to no scene fog and enabled view fog. Clone/editor Play use the same codec. Asset records are unchanged from v15, and v16 envelopes use v16. Host and isolated-worker codecs must be deployed together. See [fog](fog.md).


## Format 17: shaped particles and soft depth

Format 17 extends emitter definitions with emission shapes/radius/cone/speed/surface,
static Mesh references, drag/wind/gusts, vector motion curves and alignment/stretch/
soft-distance settings. Live particles retain their seeded turbulence phase. Derived
triangle-area distributions rebuild on configuration/load and do not enter files.
Formats 1–16 default these additions off and retain Box emission and camera-facing
billboards. Version-17 scene, asset and application envelopes write v17; clone/editor Play
uses the same records. Deploy matching host and isolated-worker codecs.
See [particle FX](fx.md).


## Format 18: effect start delay

Format 18 appends validated `startDelay` to emitter definitions. Formats 1–17
load with delay zero. Current scene, asset and application envelopes use v23.
Standalone `.wefx` resources use the same bounded asset codec with exactly one
EffectAsset entry and embedded shared references. Editor FX previews/history are
transient. See [FX authoring](fx-authoring.md).

Version 19 adds ribbon definitions and live bounded path ring/sampler state. Versions
1-18 default to Billboard. See [ribbon trails](trails.md) for validation and semantics.

## Format 20: plane particle alignment

Version 20 appends emitter planeNormal/surfaceOffset and normalized per-particle
birth normals. Versions 1-19 keep prior alignment with up/zero defaults. See
[plane particles](plane-particles.md). Matching host and worker codecs are required.

## Format 21: mesh particles

Version 21 appends particle mesh references, seeded XYZ rotation ranges/spin and
base-texture override, plus live Euler rotation and birth linear matrices. Older
versions keep prior modes with zero/identity defaults. Embedded mesh/material/texture
references share existing resource identity and validation; see [mesh particles](mesh-particles.md).


## Format 23: layered surface relief

Version 23 appends per-layer normalHeightTexture, normalScale, heightScale and
heightBias, plus heightBlend, triplanar, triplanarScale and triplanarSharpness on
MaterialLayers. Shared texture identity and instance overrides use the existing
resource codec. Earlier archives retain flat layer normals, linear weights and
mesh UVs. Version 22's particle collision and decal records remain unchanged.
Worker decoding and terrain transport require matching host and worker builds.
See [terrain surfaces](terrain.md#surface-relief-and-projection).


## Vehicle foundation and archive v24

See [vehicle contracts](vehicles.md) for the optional four-wheel component,
COM/inertia and point-force body APIs, typed editor/preset/Play support and supported
checkpoint state. Current scene/application/effect envelope writer version is 27;
older formats remain readable. Vehicle configuration/checkpoints and visual saved
references persist; commands, solver listeners and contact history do not.


Archive v25 adds deduplicated physical surfaces, compound collision children, collider
fallbacks, source triangle/terrain cell maps and retained surface assets. Older
files default to neutral surfaces. Queries/contact queues remain transient. See
[physical surface persistence](physical-surfaces.md#editor-and-persistence). Deploy
matched browser/worker artifacts, including WETP3 physical page support.


Archive v26 adds authored CameraFollowSettings and stable target IDs to camera
records. Older cameras default follow off. Runtime rig history and device/input
state remain transient; duplicate internal target IDs remap, while external saved
references stay external. See [camera composition persistence](driving-controls.md#editor-and-persistence).

Archive v27 adds built-in RouteComponent and shared spline/profile/route source
resources. Generated meshes/collision are rebuilt once per route identity under
allocation budgets; required implementation-child IDs persist. Old records retain
their layouts. See [route persistence](splines-routes.md#editor-and-persistence).
