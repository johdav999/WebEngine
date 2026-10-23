# Hybrid terrain milestone 4 validation (2026-10-22)

## Implementation

Cut heightfield chunks now subdivide mixed tiles and simplify only solid regions.
All tile, chunk and hole boundary segments retain full source samples. Curved
cut regions keep source triangles; only planar solid tiles merge. A visual check
caught shadow receiver mismatch in the first candidate, leading to this rule. Cave mesh
LOD uses exact attribute welding followed by constrained per-part simplification:
blend-band vertices and open/material boundaries are locked. Camera and shadow
selection reuse the existing projected-size/hysteresis system. Source geometry,
hole queries, collision and motor movement remain independent of render detail.

Both demo hosts expose full/coarse detail and a Distance / auto LOD view. Edits
prepare new base geometry, constrained levels and source collision before paired
publication. Undo/redo restores their snapshots. Limits/defaults and load-time
cache rebuilding are documented in [hybrid terrain](hybrid-terrain.md).

## Portable regression evidence

12 rebuilt Release CTests passed: terrain_hole_lod, terrain_seam, hybrid_terrain,
terrain, terrain_paging, terrain_page_edit, editor_terrain, editor_authoring,
mesh_optimizer, mesh_detail, scene_archive and render_frame.

The new hole test uses concave and disconnected cuts, cross-chunk openings, a
terrain-edge cut and partial chunks. Every derived level has exactly the source
boundary edge set; sampled coverage remains solid iff the source cell is solid.
It reduces 1,778 base triangles to 648 at the coarsest levels. Source/archive
collision is checked across all cells; entirely cut chunks remain empty. Prepared
cut-patch topology that disagrees with regenerated source is rejected. Curved
cut-chunk triangle centroids retain source height; checkerboard masks correctly
refuse unsafe reduction.

Seam tests require a real reduction, exact open-edge preservation and unchanged
position/normal/UV for every blend-band vertex at all generated levels. They also
cover LOD opt-out, unsupported inputs, budgets, translation and source immutability.
Constrained simplification removes zero-area collapse faces before publication.
Generic unconstrained LOD behavior retains its existing regression coverage.

## Native Windows

The SDL demo completed actual --smoke runs with strict --backend dawn and
--backend d3d12. Every fixed level, independent shadow LOD and automatic distant
selection was rendered and captured. The exact cave collider resource stays the
same across detail changes. Existing native GUI cut/fill gestures, undo/redo,
cancellation, entrance forms, scene archives and motor traversal also passed.

Dawn color submissions decreased from 5,859 to 3,455 / 2,484 / 2,374 triangles.
Direct D3D12 decreased from 5,858 to 3,454 / 2,483 / 2,373 (Dawn also counts its
background triangle). Shadow submissions stayed at 5,858 while camera detail
changed; shadow-only LOD reduced them to 2,373 with unchanged color submissions.
Distant automatic selection reduced the edited scene from 8,215 to 2,928 color
triangles on Dawn, and 8,214 to 2,927 on direct D3D12. These are submission counts,
not FPS or GPU-time improvement claims.

## Browser

Chrome 155.0.8059.85 passed the demo smoke on forced WebGPU and WebGL2, with no unexpected console/page errors. It validates actual
renderer counters for fixed levels, independent shadow detail, automatic distant
selection, invalid-policy preservation, unchanged hole queries/collision, editing,
traversal at coarse detail, scene archives, resize and loss/recovery. It captures
close seam views and material comparisons after entrance changes. Final counters,
browser version and errors are in browser/report.json.

## Reproduction and artifacts

Build using [the documented commands](hybrid-terrain.md) with repository Emscripten
6.0.11. Browser host/worker and demo use build/web-release; native tests and demo
use build/windows/Release. Serve build/web-release/hybrid-site on localhost:8157.
Run scripts/hybrid-terrain-smoke.cjs with EVIDENCE_DIR set to
build/evidence/hybrid-terrain-m4/browser and installed Playwright/pngjs/Chrome.
Use --smoke build/evidence/hybrid-terrain-m4/dawn (or d3d12) on the Windows demo.
Then run python tools/verify_hybrid_terrain.py build/evidence/hybrid-terrain-m4.

Evidence is under build/evidence/hybrid-terrain-m4: native PPM/PNG captures and
per-view renderer JSON, native-pixels.json, browser screenshots/report.json and
artifact-hashes.json. Served HTML/JS/Wasm are compared with the rebuilt files.

## Limits

Boundary locks trade reduction for continuity: dense hole patterns and heavily
blended meshes may produce fewer levels or no reduction. Mesh LOD adds synchronous
preparation work and retained memory; no streaming/sustained performance result is
claimed. No arbitrary Boolean cuts, geomorphing or painted multilayer seam input.
Scene archives retain base meshes and source collision; derived mesh LOD caches
require application-side regeneration after loading, while terrain LOD rebuilds
from saved settings. No archive format bump or consumer pin update. Physical
mobile and other-GPU acceptance were not tested. The full main-editor browser
suite was not rerun; the standalone demo and focused native authoring suites cover
the affected topology and editing contracts.
