# Terrain source paging and asynchronous generation validation

Verified on Windows desktop Chrome **155.0.8059.79**, 2026-10-15. This extends
[terrain](terrain.md) without changing the full-source editing path. Both forced
WebGPU and WebGL 2 use actual isolated workers and HTTP page files.

## Build and evidence

- Repository-local Emscripten **6.0.11**, Release Ninja build
  `build/web-terrain-paging-release`, local Jolt 5.3.0 source override.
- Staged host, worker, Wasm, fonts and page fixtures together in
  `build/terrain-paging-browser-site`; loopback preview on port **8187**.
- Final `webengine.wasm` SHA-256:
  `BCD68B8C27B4F7348E11C960E338D711EFA4E43AF62E6262C08773C3D4DF4BF6`.
- Matching `asset_decode.wasm` SHA-256:
  `B10E447014E7D08F521327D4C88F0214427A9500BC579E8D5C8B7B394501B046`.
- Paging evidence: `build/evidence/terrain-paging/results.json`, screenshots,
  downloaded `.wescene` files and `paging-final.log`.
- Regression evidence: `build/evidence/terrain-paging-base-regression/`,
  `build/evidence/terrain-paging-detail-regression/` and
  `build/evidence/terrain-paging-edit-regression/`.

Configure/build using the README setup and pinned SDK:

```powershell
.\.tools\emsdk\upstream\emscripten\emcmake.exe cmake -S . -B build/web-terrain-paging-release -G Ninja -DCMAKE_BUILD_TYPE=Release -DFETCHCONTENT_SOURCE_DIR_JOLT=C:/Users/Johan/source/repos/WebEngine/build/dependencies/JoltPhysics-5.3.0
cmake --build build/web-terrain-paging-release --target webengine -j6
```

The demo build runs `tools/terrain_demo_pages.py --output <site/assets/terrain>`.
This authors 1,849 static files for a 1025² sample, 512² m world, generating samples
page by page. The browser never allocates that entire source. Serve the selected
site using `scripts/serve.py`; preserve unknown existing port owners.

## Native checks

All **48 CTest cases passed** in `build/native-physics`, including terrain,
terrain_paging, physics, material, mesh_detail, scene_archive and persistence.
Use the configured compiler's WinLibs runtime directory on PATH when running tests.

`terrain_paging_tests` checks clipped halo counts, partial edge chunks, CRC/truncated
and invalid input rejection, large metadata-only layouts, page material texel
mapping and paged/full-source base and LOD positions/normals/UVs/topology. Worker
transport retains prepared levels and optional raw collision. Patch scene archives
rebuild at their original global coordinates and preserve query/collision coverage.

A real background `std::thread` reports an immutable result without changing
Scene; the owner publishes only on update. Controlled loaders test pending and
admission budgets, source bounds, resource expiry, stale handles/revisions, focus
cancellation, late reports, explicit failure retry and destruction. Disagreeing
height and weight halos are rejected while existing residents remain. Scene guards
reject update during traversal.

The native runner also loaded the actual previous format-14 stage-three browser
archive at `build/evidence/terrain-edit/webgpu.wescene`, saved/reloaded it as format
15 and compared resident height. `terrain_tests.exe` generated the native format-15
fixture `build/evidence/terrain-paging/native.wescene` for browser interchange.
The existing checked-in format-10 fixture remains covered by the terrain suite.

## Browser acceptance

`scripts/terrain-paging-smoke.cjs` passed on **both forced backends**:

| Check | Result |
| --- | --- |
| Delayed HTTP and owner responsiveness | Two pending requests, no premature terrain/query results; main frames continue during delayed IO |
| Actual worker generation | Nine worker-generated admissions; two peak active workers, zero remaining worker jobs after settling |
| Resident source budget | Nine pages, **60,552 bytes** of heights/RGBA under 64 KiB; each source axis at most 29 samples |
| Smaller source budget | Two pages, **13,456 bytes** under 14 KiB |
| Unloaded queries | No source/world ground outside resident patches; no implicit IO |
| Chunk LOD seams | **Zero** full-versus-mixed overhead coverage differences on both backends |
| Layer weights | Layered-versus-isolated grass changes actual rendered pixels on both backends |
| Collision and traversal | Resident ground and Jolt ray heights agree across distant focus moves; source/count bounds hold |
| Cancellation and teardown | Obsolete workers cancel; old focus cannot reappear; reset terminates pending jobs and restores editable hills |
| Failure and retry | HTTP 404 and corrupt CRC become bounded failures; no automatic repeat requests; explicit retry recovers |
| Provider immutability | Sculpt rejects paged patches; returning to hills restores normal editing |
| Demo controls | Real paging/return/retry/framing buttons work; editing controls disable for read-only pages |
| Format-15 snapshots | Resident patch sources, layers, LOD policy and collision restore without the provider; malformed loads preserve valid state |
| Renderer recovery | Paused WebGPU device/WebGL context recovery preserves saved terrain pixels |

The existing base, detail and edit smoke suites passed on both backends, including
native/browser archive interchange, independent camera/shadow policy, real brush
gestures/cancellation, paint/undo pixels, ball contact after sculpt, synchronous
streaming bounds and malformed-save preservation.

Both installed and CatGame-local engine-skill copies passed `quick_validate.py`,
terrain-content parity, local Markdown link checks and scoped preservation checks.
CatGame's dependency index and checked-out engine HEAD remain
`3fe7d1dc1dc610c703944803cbb717cd65024ec9`; this work updates its skill guidance,
not its engine dependency or game integration.

## Limits

Paged providers are immutable/read-only: cross-page sculpt/paint and dirty-page
writeback remain future work. Saved resident snapshots exclude provider URLs,
revision/controller configuration and pending requests. Recreate owners explicitly
when restoring streaming. Full-source sculpt/paint remains available separately.

Worker IO/decoding/base/LOD/raw-collider generation runs off the owner thread, but
transport hydration/validation/copy, Jolt shape cooking/registration and GPU uploads
still have owner/backend costs. Resident source/geometry counters exclude shared
tiles, worker heaps, pending transport, collision, external references and driver
storage. Bounds/admission counts do not establish a hard frame-time or total-process
memory cap. Native applications provide their own executor/IO adapter.

WebGL 2 retains basic lighting and no shadow maps; it preserves the authored shadow
policy in archives. Desktop acceptance does not establish physical Android/iOS
acceptance, sustained mobile performance or an FPS improvement.
