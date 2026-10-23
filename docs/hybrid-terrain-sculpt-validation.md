# Sculpted cave demo validation — 2026-10-23

The default cave now contains 15,498 base triangles, with connected floor, wall,
ceiling and roof strips. Application-owned shaping rounds the walls and ceiling,
adds irregular rock bulges and recomputes normals. Periodic 256×256 limestone
albedo and normal/height maps provide finer surface detail. Engine defaults,
portable interfaces and checkpoint formats are unchanged.

The source hole rim, material blend coordinates and walking floor remain
protected. Full-detail sculpted geometry supplies collision; constrained LOD is
generated afterward. Entrance/hole editing rebuilds the same sculpt. Older saved
caves retain their stored geometry until an edit regenerates them.

## Focused tests and builds

Release Windows builds passed for `webengine_hybrid_terrain_windows`,
`hybrid_terrain_tests` and `terrain_lod_archive_tests`. The browser
`hybrid_terrain_demo` build passed with repository Emscripten 6.0.11.

CTest passed 4/4: `hybrid_terrain`, `terrain_seam`, `terrain_hole_lod`, and
`terrain_lod_archive`. Demo assertions cover actual curved geometry, all eight
combinations of entrance width/depth/height extrema, seam preservation, the
30,000-triangle bound, and ceiling ray coverage near both ends of the cave.

## Browser

`scripts/hybrid-terrain-smoke.cjs` passed on Chrome 155.0.8059.85 with forced
WebGPU and WebGL2 against the rebuilt site on port 8157. Both runs reported no
unexpected console/page errors. Checks include real hole painting, undo/redo,
entrance edits, blended seams, walking in/out, all three LOD levels, independent
shadow selection, automatic distance LOD, resize and graphics recovery.

Checkpoint checks cover edited geometry and manual holes, scene replacement,
fresh-page restoration, repeated loads, corrupted-save preservation, and editing
and walking after load. The checkpoint status now occupies a fixed footer slot,
so save/load messages cannot change the canvas aspect ratio. Tests retain their
exact before/after submission assertions.

## Native Windows

The demo's complete `--smoke` flow passed on Windows Dawn and direct D3D12.
`tools/verify_hybrid_terrain.py` passed for both renderers. Each completed three
loads of a 3,136,616-byte edited checkpoint with nine manually painted cells and
three regenerated LOD levels. Before/after load scene captures had zero changed
pixels; failed loads preserved the valid scene. Post-load editing and traversal
passed.

For the edited checkpoint, Dawn color submissions decreased from 24,841 at full
detail to 5,528 at LOD 3; direct D3D12 decreased from 24,840 to 5,527. Shadow LOD
independently decreased submissions from 25,384 to 5,879 on both backends.
These are whole-frame counts for the test view, not the default cave's base count.

## Evidence and limits

Current evidence is under `build/evidence/hybrid-terrain-sculpt/`:

- `core-tests.log`, `native-build.log`, `web-build.log`
- `browser/report.json` and browser screenshots
- `dawn/`, `d3d12/`, `native-pixels.json`, `pixel-verification.log`
- `preview-overview.png`, `preview-seam.png`, `preview-interior.png`

Final WebGL2 and native D3D12 interior captures were visually inspected. The
connected approach strips remove the old wall/ceiling gaps exposed by sculpting.
This is a procedurally authored demo cave, not an interactive freeform sculpting
tool. Physical mobile, large-world workloads and sustained performance were not
validated by this change.
