# Cave panel continuity — 2026-10-23

The screenshot exposed attribute and material boundaries within a single mesh
resource. Exact vertex optimization retained distinct normals at panel borders;
normal reconstruction used those split vertices independently. Displacement fade
then blended the old hard normals back into the new sculpt. A separate floor
limestone tint emphasized the boundary.

The demo now shares floor/wall rock textures and tint, rounds interior corners
up to 0.8 m, and accumulates normals across coincident source positions. Only the
heightfield blend band retains source normals. Attribute splits remain supported;
the centre walking path, hole rim, full-detail collision and three derived LOD
levels remain protected. The default base count remains 15,498 triangles.

The focused native suites `hybrid_terrain`, `terrain_seam`, `terrain_hole_lod` and
`terrain_lod_archive` passed 4/4. New assertions verify shared interior normals
and shared rock textures; the existing eight entrance extrema, seam scans,
ceiling ray coverage and traversal assertions also pass. Browser and Windows
Release builds passed. The final WebGL2 interior preview was visually inspected.

Runtime validation and captures are under
`build/evidence/hybrid-terrain-continuity/`. The final browser smoke passed on
WebGPU and WebGL2: painting/undo, entrance editing, seam scans, walking collision,
all three LOD levels, independent shadow detail, graphics recovery, resize and
checkpoint restoration. Both runs reported zero unexpected errors and zero
changed scene pixels after loading.

The complete Windows smoke passed on Dawn and direct D3D12, including retained
GUI edits, traversal, LOD, repeated file loads, corrupt-load preservation and
mouse/keyboard flight. Native pixel verification passed for both renderers,
including identical scene captures before and after checkpoint restoration.

Older checkpoints retain their saved geometry and shading; reapply the entrance
settings to rebuild that cave. Physical mobile and large-world performance were
not tested. This repairs the application-owned cave sculpt rather than adding
an interactive mesh-sculpting API.
