# Terrain mesh lakes validation — 2026-10-23

Delivered `buildTerrainMeshLake` in portable `engine_core`: a refined basin in an
existing static mesh, fitted non-solid water geometry, and finite convex
`WaterRegion` metadata. The cave demo includes a small seven-sided pond beside
the Grand Chamber walking route, with Cave pond and Underwater view controls on
both hosts. Basin, surface, region and water level participate in editing history
and saved checkpoints.

## Checks completed

- Windows Release: all nine focused CTest suites passed: `water`, `terrain_lake`,
  `terrain_mesh`, `terrain_lake_cave`, `hybrid_cave_route`, `terrain_lod_archive`,
  `hybrid_terrain`, `terrain_seam` and `terrain_hole_lod`.
- Node/WebAssembly: `terrain_lake_tests.js` and `terrain_mesh_tests.js` passed
  using the pinned Emscripten 6.0.11 build.
- Chrome forced WebGPU and WebGL2: pond and underwater GUI views, finite-bottom
  exclusion, entrance edit, undo/redo, save/load, page reload and restored
  underwater rendering passed with zero unexpected page/console errors.
- Native Windows Dawn and direct D3D12: `--pond-smoke` passed on both backends.
  It uses retained GUI controls through SDL input, captures the pond/underwater/
  below-bed views, checks collision and region membership, edits the entrance,
  verifies undo/redo, replaces the scene from an actual checkpoint file and
  renders the restored underwater view. Both used the NVIDIA GeForce RTX 5060 Ti.

Core coverage includes source immutability, roof/disconnected-floor isolation,
protected source attributes, basin collision independent of render LOD,
non-solid water, dry polygon corners, finite bottom and exact ray clipping,
transformed water, metadata preservation/removal, archive round trips, invalid
geometry/settings and work/output-budget rejection. The cave integration checks
the default and all eight entrance-dimension extrema, dry walking-route collision,
live terrain seam scans, history and checkpoints.

During acceptance, expansion of the underwater uniform from 256 to 512 bytes
exposed a stale WebGPU view-cache size. The cache now uses `UnderwaterUniform`
directly with a size assertion, preventing an overwrite of adjacent GPU handles.
The passing underwater and restored-underwater captures exercise this repair.

## Reproduce

Build the Windows `webengine_hybrid_terrain_windows`, `terrain_lake_tests` and
`terrain_lake_cave_tests` targets in Release with the hybrid demo enabled. Run:

```powershell
ctest --test-dir build/windows -C Release -R '^(terrain_lake|terrain_lake_cave|terrain_mesh|water|hybrid_terrain|hybrid_cave_route|terrain_lod_archive|terrain_seam|terrain_hole_lod)$' --output-on-failure
build/windows/Release/webengine_hybrid_terrain_windows.exe --backend dawn --pond-smoke build/evidence/terrain-mesh-lakes/dawn
build/windows/Release/webengine_hybrid_terrain_windows.exe --backend d3d12 --pond-smoke build/evidence/terrain-mesh-lakes/d3d12
```

Build `hybrid_terrain_demo`, `terrain_lake_tests` and `terrain_mesh_tests` in
`build/web-release`, then run the two generated test JS files with Node. Serve
`build/web-release/hybrid-site` on port 8167. With Playwright available to Node:

```powershell
$env:HYBRID_URL='http://127.0.0.1:8167'
$env:BROWSER_EXECUTABLE='C:/Program Files/Google/Chrome/Application/chrome.exe'
node scripts/terrain-lake-smoke.cjs
```

Acceptance used an isolated native build at
`C:/Users/Johan/Documents/Codex/2026-10-23/in-x20/work/lake-native` to avoid locking
other work in the shared checkout. The exact fresh browser build was served on
port 8167. The standard native build directory was not rebuilt by this run.

## Evidence and limits

Compact captures, reports, logs and a source hash manifest are retained in
`build/evidence/terrain-mesh-lakes/`. Browser and both native surface/underwater
captures were visually inspected. Raw native captures and checkpoints are under
`C:/Users/Johan/Documents/Codex/2026-10-23/in-x20/work/lake-evidence/`.

This is focused pond acceptance plus the listed regression suites; it does not
claim a new full hybrid-demo smoke run or physical mobile validation. Water
regions are convex finite prisms; detailed ground collision follows the basin.
Swimming, fluid simulation and concave/multi-part lake authoring remain
application work. See [the API contract](terrain-mesh-lakes.md).
