# Mobile asset pipeline

The optional browser decoder moves source parsing and image decoding into isolated
Web Workers. Renderer admission budgets spread new GPU resources over frames.
Offline tooling exports semantic mips and packages pre-encoded KTX2 variants with
their original image fallback. These mechanisms reuse `AssetManager`, shared
textures/meshes, scene preparation and backend caches; simulation is unchanged.

## Background decoding

```cpp
#include "engine/asset_browser.hpp"

const engine::AssetId model{"level/ship@1"};
app.assets().registerAsset<engine::Mesh>(model,
    engine::browserDecodedAssetLoader("assets/ship.glb", engine::AssetDecodeKind::Glb));
app.assets().registerAsset<engine::Texture>(engine::AssetId{"level/wall@1"},
    engine::browserDecodedAssetLoader("assets/wall.wetex", engine::AssetDecodeKind::TextureBundle));
```

Link `engine_assets_browser`. Ship `asset-worker.js`, `asset_decode.js` and
`asset_decode.wasm` together; normal browser builds publish them into `dist`.
`BrowserDecodeOptions::workerUrl` selects the wrapper URL, resolved against the
page. The wrapper resolves its decoder module relative to itself. Worker creation,
script loading and asset HTTP must be permitted by the host's CSP/CORS policy.
No shared memory, pthreads or cross-origin-isolation headers are needed.

`Fbx`, `Glb`, `Image` and `TextureBundle` use the current portable importers and
their default resource limits. Importing embedded textures is supported; this
adapter does not perform implicit companion-file discovery or serialize arbitrary
decoder callbacks. Existing `browserAssetLoader` still accepts custom synchronous
decoders. Native hosts can run `decodeAssetBytes` on their own executor and use
`completeDecodedAsset` with the synchronized completion endpoint.

Each main Wasm module admits at most two active workers, in request order. Queued
operations hold URLs/options, not source bytes. A worker owns HTTP, source parsing,
image decoding and archive serialization in its own Wasm heap. Source reads have
the same default 32 MiB cap; the decoded transport/owner archive allocation limit
defaults to 256 MiB. Importer limits still apply independently. Timeout defaults
to 30 seconds from worker admission and includes module startup, HTTP and decode;
time in the queue is excluded. Oversized responses, HTTP errors, missing scripts,
invalid source data and hydration failure fail the request without publication.

Success transfers a buffer, validates/hydrates existing asset representations on
the owner, terminates the worker and reports through the existing context. Normal
manager updates publish the immutable resource; shared consumer requests use one
operation. Abort terminates an active worker or removes a queued job, releases its
slot and invalidates pending reports. Manager shutdown uses the same cleanup.

Owner hydration still copies and validates resource data, so this is not a promise
of zero main-thread load cost. Isolated heaps, source chunks, archive staging,
transfer buffers and owner resources can coexist temporarily. Worker concurrency
and per-operation caps are not a total application memory budget. No implicit
native thread pool or automatic LRU is introduced.

## GPU upload admission

```cpp
renderer.setAssetUploadBudget({
    .maxBytes = 2 * 1024 * 1024,
    .maxResources = 4,
    .maxMilliseconds = 2.0
});
```

All limits default to zero (unlimited), preserving existing host policy. Setters
validate before replacing a valid budget. One resource is indivisible: a mesh's
vertex/index/skin buffers, a semantic texture with its complete mip chain, a
material binding/uniform, a new bone/morph buffer, or an environment's cubemaps/LUT.
WebGL 2 budgets its mesh and RGBA texture cache creation. WebGPU uses the existing
device-supported compressed variant selection; WebGL 2 retains RGBA fallback.

Each submitted frame starts fresh admission counters shared across ordered views
and shadow passes. Cached resources consume no admission. Byte counts describe
actual asset payload writes; resource counts describe admitted units. Time is
elapsed CPU wall time since the render frame began, checked before a new unit.
One oversized first resource is allowed even if the byte/time target is exceeded,
so a large asset cannot remain permanently blocked. This is a soft admission
budget, not a hard latency cap or a chunked upload scheduler.

Deferred dependencies keep their existing CPU resources and are retried in scene
order on later frames. Mesh/particle draws wait for their required GPU resources;
no partial texture/material binding is submitted. Draw readiness can differ across
frames during loading. Authoritative poses, collision and animation clocks do not
depend on readiness. Existing weak-cache cleanup drops unused assets. Recovery
rebuilds caches through the same budget. Only selected mesh LODs are uploaded;
unused LODs are no longer eagerly allocated with the base mesh.

Dynamic instance/camera/light/bone/morph/particle writes, GUI, framebuffer/shadow
allocation and pipeline creation are outside admission. They remain necessary
presentation work. RGBA semantic mip generation runs inside an admitted texture
unit on the owner; compressed mips avoid that work when supported. Asset scene
preparation still gates CPU readiness, not GPU prewarming or full visual readiness.

Performance JSON `current` includes `assetUploadBytes`, `assetUploadResources`,
`deferredAssetUploads` and `deferredAssetDraws`. Deferral counts are attempts/draw
batches in this frame, not unique outstanding assets. The playground exposes
`Module._engine_asset_upload_budget(bytes, resources, milliseconds)` for measurement.

## Compressed-texture tooling

Build the native `texture_tool` target with the README's CMake setup:

```powershell
cmake --build build/native --target texture_tool
./build/native/texture_tool.exe mips wall.png build/wall-mips color
# Encode the exported TGA mip chain using an offline encoder/converter.
# Its output must be a complete, pre-encoded BC7/ETC2-RGBA/ASTC-4x4 KTX2.
./build/native/texture_tool.exe pack wall.png wall.wetex color wall-astc.ktx2 color wall-bc7.ktx2
```

`mips` writes top-down RGBA TGA levels into a new directory. `color` filters in
linear light and re-encodes sRGB; `data` filters raw channels; `normal` renormalizes
filtered normals. `pack` validates every chain and matching fallback dimensions
before staging a new output file. Existing output paths are rejected. Runtime
applications can also call `loadKtx2`, `packTextureBundle` and `loadTextureBundle`
from [texture_bundle.hpp](../include/engine/texture_bundle.hpp).

KTX2 support is a restricted container reader based on the
[Khronos specification](https://registry.khronos.org/KTX/specs/2.0/ktxspec.v2.html):
single 2D image, no arrays/cubes/depth, base dimensions divisible by four and at
most 4096, complete mip chain through 1x1, exact non-overlapping block payloads,
standard basic DFD with matching model/block layout/transfer, top-left orientation,
identity swizzle and straight alpha. Color uses Vulkan sRGB formats 146/152/158;
data/normal uses linear 145/151/157. File and retained-byte limits apply before
publication; retained bytes include RGBA fallback and compressed variants.

The delivery bundle `WETEX001` stores the encoded fallback, variant count and
semantic-tagged KTX2 files. Worker transport explicitly retains those variants
alongside the existing asset archive. Ordinary scene/application archives continue
to save RGBA fallback and omit these derived payloads. GPU feature selection and
compression disable always have a valid fallback.

Block encoding is an external authoring dependency. For example,
[Khronos KTX tools](https://github.khronos.org/KTX-Software/ktxtools/ktx_create.html)
can encode ASTC KTX2 from PNG input/mips (convert exported TGAs when using that
tool). There is no bundled encoder, Basis/UASTC transcoder, supercompression
inflater or compressed GLB extension. Container validation does not assess artistic
quality or equivalence between fallback and compressed images.

## Validation on 2026-10-10

- Emscripten 6.0.11 Release worker and browser executable built in
  `build/web-mobile-assets-release`.
- Native Debug `mobile_assets`, `assets`, `render_quality` and `application`
  passed in `build/assets-native`. They cover malformed container sizes/ranges,
  metadata, semantic mismatch, mip layouts, retained limits, fallback, worker
  transport, resource publication and upload admission/progress.
- `scripts/texture-tool-smoke.cjs` passed semantic mip export, validated packing,
  overwrite rejection and invalid-semantic rejection using the native tool.
- `scripts/assets-smoke.cjs` passed real isolated FBX/GLB/image/texture-bundle
  decoding, sharing, owner-frame progress, active HTTP cancellation, timeout,
  byte limits and startup/decode failure cleanup;
  `ASSET_TEST_SITE=build/web-mobile-assets-release/asset-tests`. Evidence is
  `build/evidence/mobile-assets/results.json`.
- `scripts/mobile-assets-render-smoke.cjs` runs its own local server and checks
  static, skeletal and morph scenes on both backends: one admitted resource per
  frame, oversize progress, deferred draws, device recovery and identical final
  paused pixels. Evidence is
  `build/evidence/mobile-assets-render/results.json` and paired PNGs.

Browser evidence is desktop Chrome plus scoped emulation where explicitly stated.
Physical Android/iOS, sustained mobile performance, external encoder execution,
artistic compression quality and ETC2/ASTC execution on supporting hardware remain
separate acceptance work.
