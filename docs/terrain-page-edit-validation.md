# Paged terrain sculpt/paint and dirty writeback validation

Verified on **2026-10-16**, Windows desktop Chrome **155.0.8059.80**, using both
forced WebGPU and WebGL 2. Current behavior is documented in [terrain](terrain.md)
and [architecture](../architecture.md). Prior paging evidence remains historical
in [stage four](terrain-paging-validation.md).

## Build and reproduction

- Repository-local Emscripten **6.0.11**, Release Ninja build
  `build/web-terrain-page-edit-release`, local Jolt **5.3.0** source.
- Matching host, worker, storage adapter, page files and fonts staged at
  `build/terrain-page-edit-browser-site`, served on loopback port **8189**.
- Final `webengine.wasm` SHA-256:
  `41971C511CAC2AFFDDA360D107EFF141144E931CBC3271C5B3229FAB53686911`.
- Matching `asset_decode.wasm` SHA-256:
  `526301BC4A5B4313696AE59BB65D65FF436A8836C3F5D7620F8C3C4D85923151`.

```powershell
.\.tools\emsdk\upstream\emscripten\emcmake.exe cmake -S . -B build/web-terrain-page-edit-release -G Ninja -DCMAKE_BUILD_TYPE=Release -DFETCHCONTENT_SOURCE_DIR_JOLT=C:/Users/Johan/source/repos/WebEngine/build/dependencies/JoltPhysics-5.3.0
cmake --build build/web-terrain-page-edit-release --target webengine -j6
```

The target authors static WETP v1 base fixtures page by page. Ship
`terrain-worker.js`, **terrain-storage.js**, `asset_decode.js/.wasm` and the host
from the same build. WETP v2 writes preserve per-page generations; old base files
decode with generation zero. Scene snapshots remain format **15**.

Run browser scripts with the established `NODE_PATH`, `BROWSER_EXECUTABLE`,
`DEMO_URL=http://localhost:8189` and per-suite `EVIDENCE_DIR`. Evidence:

- `build/evidence/terrain-page-edit/results.json`, screenshots and downloaded
  scene files; `page-edit-final.log` records both backends.
- `build/evidence/terrain-page-edit-{paging,base,detail,edit}-regression/` and the
  corresponding `*-final.log` files record the four earlier terrain suites.
- `build/evidence/terrain-page-edit/native-final.log` records native CTest.

## Native checks

All **49 native CTest cases passed**, including `terrain`, `terrain_paging`,
`terrain_page_edit`, material, physics, mesh detail, archive and persistence suites.
The configured WinLibs runtime directory was placed on PATH for execution.

`terrain_page_edit_tests` validates:

- A brush crossing a four-page corner publishes matching height/RGBA halo copies
  together, with triangle ground queries and Jolt rays agreeing.
- Unchanged pages retain geometry sharing; paint retains exact geometry/collision
  pointers and actor/component handles. Invalid height results publish nothing.
- Flatten/smooth use the expected frozen global samples, including shared borders.
- Required-page loading is atomic; focus change and stale completions cannot
  publish an abandoned stamp.
- Dirty pages remain pinned within residency/source budgets. Clear rejects unsaved
  state; explicit discard releases it. Eviction/reload restores saved edited data.
- Acknowledging an older write snapshot leaves newer edits dirty. Retry uses the
  acknowledged stored generation.
- Write failure retains edits and stops retry loops. A generation conflict rejects
  the entire batch, preserving the competing stored version without partial writes.
- Bounded WETP/WETW transport and CRC checks reject malformed transactions.

Existing paging/full-source tests retain codec v1 compatibility, global mesh/LOD
equivalence, old scene fixtures, source budgets, lifecycle and archive coverage.

## Browser acceptance

`scripts/terrain-page-edit-smoke.cjs` passed on both forced backends with actual
worker reads and IndexedDB transactions:

| Check | Result |
| --- | --- |
| Source acquisition | Start with one resident page; neighboring pages load before the cross-corner stamp publishes. Main frames continue while HTTP is held/delayed. |
| Shared halo edit | Four pages become dirty together; each stored copy contains the same raised corner height and updated generation. |
| Dirty pinning | **26,912 source bytes** across four dirty pages remain resident after moving focus; total resident count/source bounds hold. |
| Write completion | Pages unpin only after the actual IndexedDB batch completes; eviction and re-admission preserve edited height/paint. |
| Painting | Visible weight pixels change; native pointer identity and browser cache/rebuild diagnostics confirm no geometry/collision rebuild. |
| Brush tools/input | Raise/lower/flatten/smooth/paint, real clicks/drags, released hover and owned pointer cancellation behave correctly. Network requests are held explicitly for the cancellation case. |
| Dynamic collision | A settled ball follows changed paged ground after sculpting; painting leaves sleeping-body wake policy unchanged. |
| Failure/retry | Injected `QuotaExceededError` at the adapter boundary retains dirty pages and stops automatic retries; the real save/retry button recovers. This is not physical disk exhaustion. |
| Conflict | A competing coherent snapshot commits through a real IndexedDB batch; stale local generation compare-and-replace fails, retaining local data. Discard reloads the competing committed height. |
| Lifecycle | Reset/return reject unsaved state; explicit discard cancels pending interests. The clear-overlay control restores original base files outside paging. |
| Persistent reload | Full browser page reload reconstructs the edited world from IndexedDB, without retaining the former terrain owner/assets. |
| LOD | Full versus mixed-neighbor overhead coverage has **zero differing pixels** on each backend after edits and reload. |
| Scene snapshots | Real downloaded format-15 scenes preserve current edited patch source/materials/policy/collision. Malformed loads preserve the displayed scene. |
| Recovery | Paused WebGPU device and WebGL context recovery preserve snapshot pixels. |

All four previous terrain browser suites also passed on both backends: source
paging/cancellation/failure budgets, base terrain import/save/query/collision,
layered materials and independent camera/shadow detail, full-source sculpt/paint,
real gestures, synchronous streaming/absent edits, archives and recovery. The paging
suite now checks the demo's configured writer while the native default stream
continues to cover read-only behavior.

## Documentation and skill checks

Architecture, terrain, archive, README and fallback contracts distinguish delivered
paged editing/writeback from future remote adapters, merge/history policy and
geomorphing. Both installed and CatGame-local engine-skill copies passed
`quick_validate.py`, content/link checks, terrain parity and scoped preservation
against before snapshots. CatGame's index and checked-out engine HEAD remain
`3fe7d1dc1dc610c703944803cbb717cd65024ec9`; no game dependency or runtime was changed.

## Scope and limits

The supplied writer is an **origin-bound IndexedDB overlay**. Static source files
are not modified, and no remote server/file-system writer or automatic conflict
merge is included. Native hosts supply their own atomic generation-checked writer.
Origin storage can be cleared or evicted by browser/user policy.

Load-time decode/generation remains in workers. Brush evaluation/edit rebuilding,
transport validation/copy, Jolt cooking/registration and GPU uploads retain owner/
backend costs. Resident sample counters exclude edit/batch staging, older bound
materials, colliders, worker heaps and driver resources. The limits do not provide
a hard frame-time or total-process memory cap.

Paged history is application-owned; the demo's 16-stamp undo/redo stack still
applies to full-source hills. Scene downloads include local committed source edits
but do not flush storage or serialize page generations/dirty flags/pending writes.
Flush and await clean state before releasing an owner if unsaved edits must survive;
destruction or explicit discard cancels interests and releases unsaved data. An
already committed write cannot be undone by cancellation.

WebGL 2 retains basic lighting and no shadow maps, while preserving authored shadow
policy in snapshots. Desktop acceptance does not establish physical Android/iOS
acceptance, sustained mobile performance or FPS gains.
