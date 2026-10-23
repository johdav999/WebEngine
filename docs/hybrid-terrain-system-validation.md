# Three-cave system — 2026-10-23

The continuous cave now contains three chambers joined by two sloping passages:
the entrance cave at floor elevation 0 m, a larger 9 m × 6 m grand chamber at
−3 m, and a smaller 4.2 m × 3.5 m grotto at −8 m (nominal default cross-sections).
Default total extent is approximately 46 m including the approach. The mesh has
25,540 base triangles, three constrained render LOD levels and full base collision.
One indexed source surface covers all three rooms and both tunnels.

Browser and Windows expose camera shortcuts and a full walking route; the
existing exit command climbs back to the entrance. The plateau continues as a
static mesh beyond the heightfield, and deep rooms have separate point lights.
Application metadata HYBRID6 saves new views while HYBRID5 remains readable;
generic archive v36 and the browser storage key are unchanged.

## Focused checks

Four focused suites passed: `hybrid_terrain`, `terrain_seam`, `terrain_hole_lod`,
`terrain_lod_archive`. New coverage checks all three floor/ceiling collisions,
exact elevations, indexed connectivity, closed rear, all eight entrance extrema,
and complete motor descent/ascent through both connecting slopes. An archived
system reconstructs its meshes, collision and chamber configuration.

A real previous single-cave WebGL2 checkpoint from the loft validation was loaded,
its entrance edited, and the resulting three-cave system saved and rebound.
This compatibility check passed (`legacy-checkpoint.log`).

Windows Release builds succeeded into `build/windows/cave-system/` using the
existing engine libraries and an isolated OutDir because the normal executable
was running. The running user session was preserved. Launch the new executable:

```powershell
.\build\windows\cave-system\webengine_hybrid_terrain_windows.exe --backend dawn
```

The browser Release build passed with repository Emscripten. Grand chamber and
grotto WebGL2 previews were visually inspected. Full runtime evidence is under
`build/evidence/hybrid-terrain-system/`.

WebGPU and WebGL2 each passed the full terrain smoke plus new chamber camera
checks, complete motor descent/ascent and saving/loading the explorer and camera
in the lower grotto. Both runs reported zero unexpected errors and zero changed
scene pixels for the checkpoint comparison. The original hole painting, undo/redo,
seam scans, collision, three LOD levels, independent shadows, graphics recovery
and corrupt-load preservation checks passed in the same runs.

Windows Dawn and direct D3D12 passed the complete native flow, including new
chamber captures, restored grand-chamber view and full motor descent/ascent
(`dawn/system.json`, `d3d12/system.json`). Both also passed native pixel/submission
verification with `tools/verify_hybrid_terrain.py`. The initial edited checkpoints
were 1,837,709 bytes on each renderer with three restored LOD levels and nine
painted cells. The existing three-load checkpoint regression and separate saved
grand-chamber view both passed.
The isolated executable includes staged runtime DLLs and the font asset; attempts
before staging these dependencies did not establish runtime acceptance.

## Limits

The rooms form a linear connected route; branching and arbitrary volumetric
editing are not included. The authored plateau extension is outside heightfield
brush/query ownership. Older checkpoints retain their old cave geometry until
an entrance change rebuilds it. Physical mobile and sustained large-world
performance have not been validated.
