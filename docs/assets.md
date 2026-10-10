# Asset loading and resource management

`AssetManager` in `engine_core` supplies logical IDs, typed immutable resources,
asynchronous loaders, shared in-flight requests, progress, cancellation, caching,
retry and explicit eviction. `AssetPreparation` groups scene dependencies;
`Application::prepareScene` gates binding and activation on those dependencies and
a successful preparation callback.

## Register and load

An `AssetId` is an opaque case-sensitive logical key, not a URL or scene handle.
Empty keys, control characters and keys above 1024 bytes are rejected. Register
each key once with its exact resource type and source/import policy. Use a distinct
key for another version or import configuration. Resources reuse the existing
`shared_ptr<const Mesh>`, texture, material, skeleton/clip, effect and other types.

```cpp
#include "engine/application.hpp"
#include "engine/asset_browser.hpp"
#include "engine/fbx_importer.hpp"
#include "engine/mesh_component.hpp"

engine::Application app;
const engine::AssetId ship{"models/knarr@1"};
app.assets().registerAsset<engine::Mesh>(ship, engine::browserAssetLoader(
    "assets/Knarr.fbx",
    [](std::span<const std::byte> bytes, const engine::AssetLoadContext& context) {
        context.complete<engine::Mesh>(engine::FbxImporter::loadMemory(bytes));
    }));
auto request = app.assets().load<engine::Mesh>(ship);
// Application::advance drives the manager at frame boundaries.
// Standalone owners call manager.update().
if (request.progress().status == engine::AssetStatus::Ready) {
    std::shared_ptr<const engine::Mesh> mesh = request.get<engine::Mesh>();
}
```

Browser callers link `engine_assets_browser`, whose interface adds `-sFETCH=1`.
The portable header has no browser/WebGPU types. Native applications register their
own asynchronous file/IO/executor loaders; no native thread pool or implicit file
access is created. Synchronous factories can complete inside a loader, but their
work occupies `update()`.

## Ownership, publication and cancellation

`AssetLoader` starts on the owner's next `update()` and returns an optional abort
function. For nonblocking loading it must initiate IO and return promptly. Its
copyable context can report progress, fail or publish one validated immutable
resource. Worker reports are synchronized; readers see publication only during an
owner update. Registration, loading, request access/cancellation and updates stay
on one owner thread. Recursive manager mutation during update is rejected.

The loader owns its IO/executor state until completion or abort. Abort should not
throw; the manager contains abort exceptions to finish cleanup. Terminal completion
releases the abort function without calling it, so sources must finish their IO
before reporting completion/failure. Borrowed objects captured in loaders must
outlive their registrations. Callbacks must not destroy their owner. Avoid scene
pointers in long-lived loaders; scene construction belongs in preparation callbacks.

Contexts hold weak operation references and remain safe after manager destruction.
First completion/failure wins. Duplicate and late reports are ignored; null or
incorrectly typed results fail without entering the ready cache. Decoders validate
contents before publishing: the generic manager checks type/presence, not domain
mesh/texture rules. Loader exceptions become request errors.

Each move-only request owns one consumer interest. Cancelling or dropping it
releases that interest. Cancelling one consumer preserves other consumers. When the
last interest disappears, the next update cancels pending IO; abandoned queued
requests never launch. `unload` and `clear` cancel pending IO immediately, regardless
of consumer count. Late completion cannot publish into a cancelled or replacement
generation. Cancelling a ready request releases its resource interest.

## Cache, state and progress

| State | Meaning |
| --- | --- |
| `Queued` | Request exists; loader not started |
| `Loading` | IO/decode underway; no resource exposed |
| `Ready` | Validated resource published; `get<T>()` succeeds |
| `Failed` | Error retained; no resource exposed |
| `Cancelled` | Interest or pending operation cancelled |

Concurrent requests for one key share one operation. Ready entries stay strongly
cached until `unload(id)`, `collectUnused()` or `clear()`. Collection evicts terminal
entries without request interests; it does not cancel pending work. Eviction drops
manager ownership while ready requests, components and other shared owners retain
valid resources. Existing renderer weak-resource cleanup runs independently.
Failed/cancelled keys create a fresh operation on the next load; old requests retain
their terminal result. Registrations and loader closures survive `clear()`.

`completedBytes` is monotonic; reversed/invalid reports are ignored. `totalBytes=0`
means unknown, including factories without byte reports. Known totals complete when
the resource is ready. Bytes describe source IO, not decoded/GPU memory. Preparation
reports ready/total dependency counts, aggregate bytes and `totalBytesKnown`; use
dependency counts when a source length is unknown. A downloaded asset may still be
loading while decoding. Applications choose eviction policy; automatic LRU/budgets,
priorities, persistent caches and hot reload remain future work.

## Prepare before activation

Continuing the example above:

```cpp
const auto loading = app.loadScene(std::make_unique<engine::Scene>("Loading"));
app.start(loading);
const auto level = app.loadScene(std::make_unique<engine::Scene>("Level"));
auto dependencies = std::make_shared<engine::AssetPreparation>(
    app.assets(), std::vector<engine::AssetId>{ship});
app.prepareScene(level, dependencies,
    [ship](engine::Scene& scene, const engine::AssetPreparation& assets) {
        scene.spawn<engine::Actor>().addComponent<engine::MeshComponent>(
            "Ship", assets.get<engine::Mesh>(ship));
        // Optional host validation/preparation. Throw to reject this candidate.
    });
app.requestTransition(level, true);
app.advance(hostTimestampSeconds);
```

Attach preparation only to an inactive loaded scene. Duplicate dependencies are
deduplicated; empty sets are ready. Batches own request interests and no scene
pointers. Loading screens can read their progress or cancel them explicitly.
Releasing application ownership on scene unload releases interests unless another
caller retains the shared batch.

`advance` updates application assets before input/transitions, including in `Created`,
paused and host-suspended states. Suspension freezes clocks, not IO publication;
transitions wait for resume. Pending transitions retain the outgoing scene. Once all
dependencies are ready, the preparation callback runs once at an idle boundary,
then normal presentation binding/activation run. Returning to that loaded scene
does not repeat preparation; explicitly replacing its preparation resets it.
If a preparation callback requests another transition, that newer request takes
precedence at the following boundary; the current scene stays active meanwhile.

Dependency failure/cancellation cancels remaining batch interests and rejects the
transition. Preparation exceptions also reject it. Errors propagate to the host;
the active scene/binding stays intact. Callbacks mutate only the inactive candidate
and must validate before changing external state. Retry by attaching a new batch/
callback or rebuilding the candidate; partial candidate construction is not rolled
back after an exception. Initial pending scenes cannot `start`; the app stays
`Created`. Drive `advance`/`assets().update()` until ready, or start a loading screen.
Shutdown releases preparations/scenes and clears the cache.

GPU creation remains renderer-owned. This hook supports host preparation/validation;
it does not add GPU prewarming or wait for asynchronous GPU validation. Runtime
asset IDs/registrations and preparation callbacks are not archived. The existing
playground file importer remains available; it is not migrated into this registry.

## Browser source and checks

`browserAssetLoader` uses the pinned SDK's asynchronous
[Emscripten Fetch API](https://emscripten.org/docs/api_reference/fetch.html) with
timeout, progress, abort, HTTP checks and byte limits, without IndexedDB access.
Relative URLs resolve against the page; cross-origin URLs follow browser CORS.
The borrowed response span lasts only through the decoder call. Decoding remains
synchronous browser-thread work. Size limits reject declared/observed sizes and
final payloads; browser buffering may allocate before progress arrives.

Native coverage is `tests/asset_tests.cpp` (CTest `assets`): worker publication,
type validation, sharing/release, retries, cancellation/stale callbacks, batches
and scene activation/failure. The existing `application` suite covers lifecycle
compatibility. Build using [README commands](../README.md#verification).

```powershell
./.tools/emsdk/upstream/emscripten/emcmake.exe cmake -S . -B build/web-assets-release -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/web-assets-release --target asset_browser_tests
$env:NODE_PATH = 'path/to/test/node_modules'
$env:BROWSER_EXECUTABLE = 'C:/Program Files/Google/Chrome/Application/chrome.exe'
node scripts/assets-smoke.cjs
```

The smoke script owns an ephemeral localhost server with delayed HTTP chunks,
errors, corruption, oversized responses, timeout and cancellation fixtures. It
verifies real Wasm FBX importing, one shared fetch, preparation, retry and HTTP
aborts. `ASSET_TEST_SITE` selects another runner directory; `EVIDENCE_DIR` selects
output (default `build/evidence/assets/results.json`). This runner needs no GPU;
it does not establish physical mobile acceptance, decode latency, sustained
performance or renderer/GPU prewarming.

See [the recorded validation scope](assets-validation.md) for the runs completed
on 2026-10-10 and the separate full-suite limitation.
