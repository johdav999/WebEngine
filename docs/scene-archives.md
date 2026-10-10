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

## Saved data

| Data | Contents |
| --- | --- |
| Hierarchy | Scene, actor, component and plain node names, ordered children, local transforms, disabled state, tags and tick flags; required effect/skeletal children are restored once |
| Geometry | Vertices, indices, tangents, material ranges, skin streams, morph targets/defaults, warnings and independent component morph weights |
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

Readers accept versions 1 through 4. Version 2 adds the sensor boolean to body
settings; version-1 bodies load with `isSensor=false`. Version 3 adds clip markers,
shared animation controller definitions, typed instance state and queued output;
versions 1/2 load with empty markers and no controller. Version 4 adds saved node
identities, custom component codecs and application-state envelopes. New scene and
asset saves write version 4. Trigger overlap history and executable marker response
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
