# Hybrid terrain milestone 5: save/load validation

Validated 2026-10-22 on Windows with local Release builds and repository Emscripten
6.0.11. This extends the milestone 4 implementation with archive v36 recipes and
actual checkpoint scene replacement. Existing unrelated checkout work was retained.

## Implementation verified

- Seam meshes save versioned, explicit blend-band vertex locks and border policy.
  Scene/asset loads rebuild derived LOD once per shared resource before publishing.
  Full base geometry, material assignments and collision remain independent.
- Browser Save/Load uses origin-local localStorage; native Save/Load uses an atomic
  `.wescene` file. Both stage and validate a replacement, rebind host camera/views,
  and unload the previous scene. Application metadata restores entrance dimensions,
  manual-hole ownership and presentation settings. History/gestures/tool/movement
  reset; edits and traversal work again after load.
- Unsupported recipes, invalid lock sizes/values, allocation limits, corrupt bytes
  and malformed demo metadata reject. Failed loads retain the running scene.

## Portable/native tests

15/15 CTest targets passed: terrain_lod_archive, terrain_hole_lod, terrain_seam,
hybrid_terrain, terrain, terrain_paging, terrain_page_edit, editor_terrain,
editor_authoring, scene_archive, render_frame, mesh_optimizer, mesh_detail,
persistence and camera_follow. Evidence: `build/evidence/hybrid-terrain-m5/core-tests.log`.

The new archive test compares complete base and derived vertex/index buffers,
material partitions, LOD thresholds and saved locks through three round trips.
It checks shared asset identity, absent caches regenerated from recipes, no-recipe
opt-out, version-35 base-mesh compatibility, invalid recipes with repaired valid
checksums, failed regeneration atomicity, holes, physical seam scans, motor traversal
and post-load entrance editing/undo. Existing v1 archive fixtures also pass.
Two version-35 assertions were updated for v36; existing persistence/camera version
assertions now match the writer. A transient missing-object linker failure cleared
on retry; no source workaround was needed.

## Browser and native runtime acceptance

Browser smoke runs Chrome 155.0.8059.85 with forced WebGPU and WebGL2,
using the newly built `build/web-release/hybrid-site` served on port 8157. The main
`webengine` and its decoder worker were rebuilt together for the new archive codec.
The automated browser test uses actual DOM controls and pointer painting, writes
checkpoint bytes, changes the entrance, loads, then reloads the page into a fresh
Wasm/renderer instance and opens the saved checkpoint. It repeats save/load twice,
rejects corrupt storage without replacing live state, edits/undoes the restored
entrance, and walks through it in both directions. Browser errors are checked.

One repeat run reported `Cannot create shadow framebuffer` during the deliberate
WebGL context-loss recovery probe, before its save/load checks. A fresh sequential
run after native GPU tests exited passed the complete WebGPU/WebGL2 script,
including recovery and all checkpoint checks. No renderer recovery change or
root-cause claim is included in this milestone.

Native smoke runs the actual SDL window/retained GUI with Dawn and direct D3D12,
uses injected native touch events on Save/Load, restores a physical scene file,
checks three successful loads and one rejected corrupt file, confirms only one
Application scene remains, and repeats editing and motor entry/exit. Screenshots
and actual renderer counters prove all three restored levels reduce submissions,
camera/shadow LOD remain independent, and automatic distance selection works.

Saved fixture: edited 8 m wide, 3.5 m high, 12 m deep cave plus painted surrounding
holes. The different browser/native painted cell counts come from their viewport
and pointer footprints. The table compares fixed camera LOD 3 and shadow LOD 1
before saving and after loading; counts are identical in each renderer.

| Target | Checkpoint bytes | Manual hole cells | Loaded color / shadow triangles | Changed scene pixels |
| --- | ---: | ---: | ---: | ---: |
| Browser webgpu | 633,900 | 12 | 2,656 / 4,557 | 0 |
| Browser webgl2 | 633,900 | 12 | 2,656 / 4,557 | 0 |
| Windows dawn | 633,972 | 9 | 2,630 / 4,563 | 0 |
| Windows d3d12 | 633,972 | 9 | 2,629 / 4,563 | 0 |

The pixel verifier excludes the native GUI; browser comparisons use the canvas.
It counts channel differences above its threshold and reported zero changed scene
pixels on all four paths. Browser WebGL2 and native D3D12 restored captures were
also visually inspected for the cave opening and blended seam. The D3D12 color
counter excludes the one background triangle counted by the other renderers.

Native post-load detail evidence:

| Target | Full | LOD 1 | LOD 2 | LOD 3 | Far full → automatic |
| --- | ---: | ---: | ---: | ---: | ---: |
| dawn | 7653 | 4116 | 2758 | 2630 | 8197 → 3070 |
| d3d12 | 7652 | 4115 | 2757 | 2629 | 8196 → 3069 |

Camera-only detail changes retain 8,196 shadow triangles; shadow-only LOD reduces
that to 2,981 while retaining full camera geometry. These are submission counts,
not an FPS or sustained performance claim.

Artifacts under `build/evidence/hybrid-terrain-m5/`: browser/report.json and saved
`.wescene` bytes, before/loaded screenshots, dawn/ and d3d12/ checkpoint files,
per-view performance JSON and PPM/PNG captures, native-pixels.json, build/test logs.
`artifacts.json` records matching served/local HTML, JavaScript and Wasm hashes,
plus the final native executable and browser host/decoder-worker hashes.
Reproduce with `scripts/hybrid-terrain-smoke.cjs`, each native demo's `--backend
<dawn|d3d12> --smoke <directory>`, and `python tools/verify_hybrid_terrain.py
build/evidence/hybrid-terrain-m5` after building the documented targets.

## Boundaries

Regeneration is bounded synchronous CPU work at staged load, not background LOD
streaming. Unmarked ordinary meshes and v1–35 mesh records load without a recipe;
older seam meshes require explicit preparation once before saving the new format.
Dense locked geometry may not simplify. New v36 files require an updated reader.
Demo history is not saved. Physical mobile, large-world load latency, sustained
performance and cloud persistence were not validated.
