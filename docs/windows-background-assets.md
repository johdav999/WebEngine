# Background Windows assets

`DesktopAssetExecutor` in `engine/asset_desktop.hpp`, linked through
`WebEngine::Platform`, supplies optional background directory/file IO and decoding.
It does not create a window or renderer. Browser and Windows adapters return the
same `AssetLoader`; applications use the same `AssetManager`, `AssetRequest`,
`AssetProgress`, `AssetPreparation` and immutable resource types on both targets.

```cpp
#include "engine/asset_desktop.hpp"

engine::DesktopAssetExecutor executor; // Explicit owner, two workers by default.
engine::DesktopFiles files;
engine::AssetManager assets; // Destroy/cancel consumers before executor when convenient.
engine::AssetDecodeOptions policy;
policy.source.maxBytes = 32 * 1024 * 1024;
policy.source.timeoutMilliseconds = 30000;
policy.maxResultBytes = 256 * 1024 * 1024;
const engine::AssetId model{"level/model@1"};
assets.registerAsset<engine::Mesh>(model,
    executor.decodedAssetLoader(files, "models/car.glb", engine::AssetDecodeKind::Glb, policy));
auto request = assets.load<engine::Mesh>(model);
// Each owner frame, including a loading/pause screen:
assets.update();
auto progress = request.progress();
if (progress.status == engine::AssetStatus::Ready) {
    auto mesh = request.get<engine::Mesh>(); // Adopt into a scene on its owner.
}
// request.cancel(); // Does not cancel another consumer of the same logical key.
```

For the browser, link `engine_assets_browser` and use
`browserDecodedAssetLoader(url, kind, policy, "asset-worker.js")`. The existing
three-argument `BrowserDecodeOptions` API and aggregate initialization remain
supported. `BrowserAssetOptions` aliases the portable `AssetSourceOptions`.
Shared source/decode policy carries no browser, SDL, Win32 or renderer types.

## Workers, progress and limits

The explicit executor admits FIFO jobs to 2 workers, configurable 1..8. Its queue
holds at most 64 pending jobs, configurable 1..256; active jobs are separate.
Excess submissions fail through their request with `Background asset queue full`.
The queue holds copied paths/configuration, not source buffers. The default
32 MiB source and 256 MiB decoded transport/allocation budgets match the browser.
Windows caps either configured byte limit at 512 MiB. Importer limits still apply.
Concurrency times per-job budgets is not a total process-memory bound.

Source reads run in 64 KiB chunks and report monotonic byte progress. A known
100% source count can remain `Loading` during decode. `Fbx`, `Glb`, `Image` and
`TextureBundle` reuse `decodeAssetBytes` and the same archive/variant validator
`hydrateDecodedAsset`; source parsing, serialization and hydration all run on a
Windows worker. Browser hydration remains on its Wasm owner. No worker touches
scenes, GUI, input, GPU state or application callbacks. Publication occurs only
during the owner's normal manager update. GPU upload/pipeline creation remains a
separate renderer responsibility; this is not GPU prewarming or a hard frame-time
guarantee.

`decodedFileLoader(path, kind, policy)` snapshots absolute paths on the owner,
independently of subsequent CWD changes. `decodedAssetLoader(files, relative, ...)`
copies DesktopFiles configuration and rechecks its directory confinement on the
worker. Existing path/alias/traversal rules still apply; this is not a security
boundary against another process replacing junctions during an open. No implicit
companion-file discovery, HTTP download, terrain paging executor, hot reload or
package mount is added.

`fileLoader<T>(path, decoder, sourcePolicy)` and
`assetLoader<T>(files, relative, decoder, sourcePolicy)` support custom decoders,
including `decodeDesktopWav`. The decoder borrows its span for that call, returns
an owning `shared_ptr<const T>`, and must be safe on a worker. Captured services
must outlive execution; do not capture scene/GUI/device owners, mutate published
resources, capture/destroy the executor, or call owner-only APIs. Null results,
exceptions, missing/corrupt files and exceeded limits fail without replacing a
valid resource. The older `DesktopFiles::assetLoader` remains synchronous by design.

## Timeout, cancellation and teardown

Timeout starts at worker admission, excludes queue time and includes IO, decoding
and hydration. Browser deadlines also begin at admission, but owner hydration
follows worker completion. A watchdog reports failure within
normal thread scheduling, independently of owner frames; the owner sees it at its
next update. Completion also checks the deadline so a late watchdog cannot admit
an overdue result. Abort drops queued work or marks active work stopped without
waiting. Per-consumer cancellation, last-consumer abort, clear/unload and stale
generation rejection retain the portable manager contract.

Windows does **not** forcibly terminate threads. A synchronous decoder or blocked
filesystem operation already running may continue after timeout/cancellation,
retaining its worker slot and temporary buffers until return. Chunk/phase checks
skip subsequent work; stopped results cannot publish. Browser cancellation can
terminate its isolated worker immediately. Use trusted bounded custom decoders;
there is no hard execution deadline against a hung native importer or kernel IO.

Construct/use/destroy the executor on its owner thread. Destruction fails queued
and active operations, cancels work and joins all workers/watchdog before captured
state is released. It can wait for an already running synchronous operation.
Loaders retain weak service references: registrations can outlive the executor and
subsequent loads fail with an expired-service error. Manager destruction also
cancels safely; weak completion contexts cannot resurrect it. Diagnostics report
active and queued counts, including cancelled/timed-out tasks still executing.

The Windows vehicle example imports GLB previews asynchronously, displays source
progress, offers **Cancel model loading**, retains the previous preview until
success and commits scene changes only from its presentation callback. Its bundled
WAV also loads through the executor. Its model decoder explicitly retains native
directory-confined companion-image resolution on the worker, using the existing
GLB importer limits; the source-byte counter covers the GLB itself. Small font/checkpoint reads and native modal
dialogs keep their existing synchronous contracts.

## Validation

Build with `scripts/build-windows.ps1`, then run:

```powershell
ctest --test-dir build/windows -C Release -R '^(desktop_assets|assets|application)$' --output-on-failure
scripts/windows-smoke.ps1 -BuildDir build/windows -Model tests/fixtures/skeletal_textured.glb -EvidenceDir build/evidence/background-assets-windows
```

Build `asset_browser_tests` and `webengine` using the repository-local SDK/version
from `.emscripten-version`. `scripts/assets-smoke.cjs` self-hosts real HTTP/Wasm
fixtures (`ASSET_TEST_SITE` selects the matched build's `asset-tests` directory).
Use `scripts/mobile-assets-render-smoke.cjs` for WebGPU and WebGL2 rendering.
See [recorded validation](windows-background-assets-validation.md).
