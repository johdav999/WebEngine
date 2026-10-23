# Open-path cliff validation — 2026-10-23

Implemented `buildTerrainMeshCliff`, the paired heightfield cut/mesh replacement,
exact source-grid perimeter, source heights/normals, triplanar ground/rock
transitions, lip/toe LOD protection and full-base collision. The shared demo has
a 5 m, 85-degree cliff with a gently bent open path. Its final base has 3,826
triangles, replaces 54 cells and has three derived LOD levels.

## Portable and integration checks

The cliff suite passed on Windows Release and Node/WebAssembly, built with the
repository's Emscripten 6.0.11. It covers shared manifold topology, exact unsplit
perimeter edges and rendered source normals, material boundary weights, steep
face collision, LOD lock retention, character movement across both joins, scene
round trips, deterministic generation, gentle bends, extra forward path samples,
malformed paths/materials, existing-hole rejection and bounded atomic failure.
The source remains unchanged on success or rejection.

Seven focused native suites passed: terrain_cliff, terrain_mesh,
hybrid_cave_route, terrain_lod_archive, hybrid_terrain, terrain_seam and
terrain_hole_lod. Archive/cave tests also retain painted holes beside the collar;
intentional painted openings are not treated as seam defects.

## Browser and native rendering

Forced WebGPU and WebGL2 cliff acceptance passed in Chrome 155.0.8059.85 with
zero unexpected console/page errors. Real controls exercised the cliff view,
visible material blending, coarse detail, live/archive collar collision scans,
checkpoint replacement and reload, cave resizing/undo, and corrupt-load
preservation. The existing full hybrid browser regression also passed on both
backends, including painting/history, LOD/shadows, renderer recovery/resize,
checkpoint pixel equivalence and full cave descent/ascent.

Native Windows `--cliff-smoke` passed separately on Dawn and direct D3D12. This
uses the actual retained GUI button through SDL touch events, captures full,
coarse, restored and unblended frames, verifies collision at the collar and face,
proves LOD does not replace collision, replaces the live scene from a file
checkpoint, and verifies restore-brush protection of the mesh-owned footprint.
Both renderers restored identical cliff pixels; material blending visibly changed
102,365 pixels in the unobscured comparison region. Browser and native captures
were visually inspected. An earlier broad native smoke stopped during pond
capture; that run is not counted as passing acceptance. Native acceptance here
is scoped to the final cliff flow.

## Evidence and limits

Raw captures/reports are under
`C:/Users/Johan/Documents/Codex/2026-10-23/how-x20/work/evidence/`.
Compact evidence and source hashes are copied to `build/evidence/terrain-cliff/`.

Existing scene archive records preserve baked geometry, materials, colliders and
LOD recipes; no engine archive version changed. Editable descriptors remain
application-owned. HYBRID9 adds the cliff; the concurrent HYBRID10 demo retains
its binding. Existing checkpoints remain readable. Architecture, contracts and
both engine-skill copies were updated while preserving consumer-specific guidance.
No consumer engine pin, unrelated work or published Git state was changed.

This accepts bounded open-path steep cliffs and compatible material joins. It
does not accept overhangs, closed mesas, arbitrary mesh Booleans, painted
multilayer inputs, large-world performance or physical mobile devices.
