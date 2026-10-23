# Asset dependencies, reimport and nested prefabs

`AssetManager::setDependencies(id, keys)` supplies an explicit invalidation graph
over registered logical asset IDs. It does not schedule a loader's own dependencies
or infer filenames. Cycles and invalid candidate edges are rejected atomically.
`dependencies` / `dependents` inspect the graph.

`reload(roots)` stages roots and reverse dependents as one transaction. Existing
ready requests keep their resources until every candidate succeeds. Publication in
`update()` changes request `get<T>()` and increments `revision()` together. Previously
obtained shared resources remain valid. Failure, timeout or `cancelReload()` retains
the whole old batch. `reloadProgress()` reports generation, counts, status and error.
New reloads supersede unfinished work and retain its affected set; cancelled or late
completions cannot publish. Applications explicitly rebind scene resources after a
successful revision change; no worker mutates scenes or GPU resources.

`AssetHotReload` in `engine/asset_reload.hpp` owns bounded fingerprint probes and
borrows a target AssetManager. Explicit `watch(source, fingerprintLoader, affected)`
maps sources to assets; call `poll(monotonicSeconds)` on the owner even while paused,
then continue target `update()`. The default polling interval is .5 seconds (valid
.1–3600); there are at most 64 distinct source identities per watcher lifetime and
64 affected assets per source. Enable starts a new baseline and one reimport to
close the baseline/import race. Changed bytes trigger later transactions. Disable
stops probes; cancellation of an existing target reload is a separate action.
Missing/corrupt sources keep valid content and expose diagnostics.

Windows uses `DesktopAssetExecutor::fingerprintFileLoader`; browser uses
`browserAssetFingerprintLoader` and isolated worker decoding. `AssetSourceOptions`
is shared: `fresh=true` bypasses browser transport caches for reimports; disk reads
already read current bytes. Fingerprints are bounded FNV content change detectors,
not cryptographic integrity checks. Each source is hashed independently: the API
does not promise a filesystem-wide snapshot of files changing during a batch.

Nested prefab capture, archive and instantiation preserve scoped instance links
and remap binding identities. `PrefabAsset::dependencies()` returns direct immutable
base/nested dependencies. `PrefabAssetEditor::nestPrefab` edits a private definition
through its normal history. `refreshPrefabDependencies` refreshes nested/base
revisions bottom-up using existing three-way overrides; `updatePrefabs` publishes
only topmost affected roots as one scene command. Dependency cycles, stale/conflicting
revisions, unsupported edits and budget violations retain valid state. Limits include
512 expanded nodes, depth eight dependency edges, 64 unique revisions and bounded
4096 graph visits. Existing archive v34 already carries the required links; there
is no additional archive format for this workflow.

Try browser `?scene=content-iteration&editor=1` or the native
`webengine_content_windows` developer target. Their shared example composes fresh
model/texture loaders, a texture→model invalidation dependency, source polling,
atomic failed-reimport retention, nested edits, history and prefab export.
The [shared authoring frontend](editor-authoring.md) also exposes nested prefab
definition edits and explicit publication. These mechanisms do not provide automatic
project scanning, OS filesystem events, a content database or cross-process locks.
