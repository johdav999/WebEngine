# Terrain mesh formations — 2026-10-23

Delivered the portable engine_core `buildTerrainMeshFormations` operation and
integrated two stalagmites and two stalactites into the shared cave demo. The
operation locally refines and extrudes the existing connected surface, reconstructs
smooth normals across attribute splits, protects inherited seam attributes and
tip LOD locks, and retains ordinary full-base collision and archive persistence.

## Focused checks

Windows Release: `terrain_mesh`, `hybrid_terrain`, `terrain_lod_archive`,
`terrain_seam` and `terrain_hole_lod` all passed. The expanded `terrain_mesh_tests`
also passed under Node/WebAssembly with the pinned Emscripten 6.0.11 toolchain.
Coverage includes floor/ceiling and arbitrary directions; exact face, edge, vertex
and nearly-on-edge anchors; continuous manifold attachment; material partitions,
UV interpolation and smooth coincident normals; isolated disconnected sheets;
bend and deterministic seed variation; protected seam attributes; exact tip retention
through three LOD levels; full-base collision and scene round trips; malformed
input, overlap, skinning, protected footprints and work/geometry budget rejection.

The shared demo regression explicitly checks floor and ceiling formation collision
at all eight entrance dimension extrema and after scene save/load. Centreline
walking and existing cave/terrain seam checks pass. Near-edge anchors initially
produced sliver triangles and unstable tip rays; local vertex/edge retriangulation
now avoids those slivers. UV attribute splits and protected seam tangents also have
regressions for their repaired normal/attribute preservation paths.

## Rendering and host acceptance

The full browser smoke passed on forced WebGPU and WebGL2 with zero unexpected
errors. It exercised visible views, brush input, entrance editing, grouped history,
material blending, all three LOD levels, independent shadows, automatic LOD,
walking, renderer recovery, resize, checkpoint restoration after page reload,
corrupt-load preservation and editing after restoration. Both native Windows
smokes passed on Dawn and direct D3D12, including retained GUI interaction,
movement, LOD, checkpoint replacement, corruption rejection and camera controls.
The native framebuffer/submission verifier passed on both renderers.

An initial in-use standard executable was preserved by linking an isolated copy
for acceptance; the normal build/windows/Release demo was subsequently rebuilt
successfully with the final feature. The isolated copy includes the existing SDL,
font and shader compiler runtime files. Browser acceptance used the exact newly
built hybrid-site on loopback port 8164.

## Evidence and scope

Compact evidence and source hashes are in `build/evidence/terrain-mesh-formations/`.
Raw captures and test working files are under
`C:/Users/Johan/Documents/Codex/2026-10-23/in-x20/work/evidence/`.
The interior preview shows the joined limestone bases and tapered formations.
Browser and both native interior captures were visually inspected.

No archive format changed. Existing checkpoints keep saved geometry until an edit
regenerates the cave. Procedural descriptors remain application-owned. No new
interactive formation-authoring panel, volumetric union, runtime growth or
automatic placement/clearance policy was added. Physical mobile, large-world
performance and arbitrary self-intersecting inputs were not accepted by this work.
