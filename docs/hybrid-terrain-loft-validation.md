# Continuous cave loft — 2026-10-23

The cave now starts as a continuous indexed surface rather than floor/wall/ceiling
panels. The sampled heightfield perimeter flows through a collar into an arched
mouth, varying rounded tunnel sections and a closed rear dome. Source vertices
are shared around each ring and along the tunnel; one limestone material covers
the whole cave. The centre floor remains flat for the explorer.

Existing seam preparation attaches the source to terrain. Up to two bounded
uniform subdivision passes share edge midpoint indices, followed by subtle rock
displacement, reconstructed smooth normals, attribute welding and constrained
LOD regeneration. The default cave has 28,560 base triangles. Geometry, masks,
collision, history and checkpoint publication retain the existing shared host
contracts. The separate overhang sample is unchanged.

## Core and browser checks

Release builds passed on browser and Windows. Four focused suites passed:
`hybrid_terrain`, `terrain_seam`, `terrain_hole_lod`, `terrain_lod_archive`.
Tests establish source disk topology, a single outer boundary with no internal
opening, and indexed connectivity of the final mesh. All eight entrance extrema
pass terrain rim scans, arched ceiling coverage, closed rear collision and the
30,000-triangle limit. Centreline rays use exact axis coordinates rather than
floating-point trigonometric residues. Walking and archive round trips pass.

The former rectangular ceiling samples at the back corners now sample the arch
before its rear taper; a separate forward ray checks the closed dome. This
matches the new geometry while preserving coverage checks for actual openings.

The full browser smoke passed on WebGPU and WebGL2, including real brush input,
undo/redo, entrance editing, material blending, collision traversal, three LOD
levels, independent shadows, automatic LOD, graphics recovery, resize, corrupt-load
preservation, and fresh-page checkpoint restoration. Both runs had zero unexpected
errors and zero changed scene pixels after load. The edited browser checkpoint
was 2,187,703 bytes. Overview/interior WebGL2 captures were visually inspected.

## Evidence and compatibility

Evidence: `build/evidence/hybrid-terrain-loft/`, including `core-tests.log`,
build logs, `browser/report.json`, and `preview-interior.png`.
Native smoke passed on Windows Dawn and direct D3D12, including actual retained
GUI edits, three checkpoint loads, corrupt-load preservation, post-load editing
and walking, all three LOD levels and mouse/keyboard flight. Native edited saves
were 2,187,775 bytes with nine painted cells and three regenerated LOD levels.
`tools/verify_hybrid_terrain.py` passed on both renderers with the same image and
submission checks used for the prior demo. Completed Dawn raw captures were
compressed losslessly to PNG after a disk-space interruption of the first D3D12
run; the subsequent complete D3D12 run passed. The verifier accepts retained PNG
captures as well as raw PPM, with unchanged acceptance assertions.

Older checkpoints retain their saved mesh; reapply entrance settings to rebuild
them with the loft. No archive migration is needed. Physical mobile, large-world
performance, arbitrary branching caves and an interactive volumetric sculpting
tool were not tested or added.
