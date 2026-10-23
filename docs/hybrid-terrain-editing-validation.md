# Hybrid terrain milestone 2 validation

Validation date: 2026-10-22. Build outputs and evidence are local to this checkout.
See [contracts and limits](hybrid-terrain.md). Milestone 1 evidence remains a
separate historical record.

## Checks

All eight native CTests passed: `hybrid_terrain`, `editor_terrain`,
`editor_authoring`, `terrain`, `terrain_paging`, `terrain_page_edit`,
`scene_archive`, and `render_frame` (final run: 1.66 seconds).

Native Windows x64 Release demo acceptance passed separately on Dawn and direct
D3D12. Both exercised GUI tool selection, pointer cut/fill, undo/redo, cancellation
followed by a new stroke, paired entrance edits, invalid width retention, archive,
motor traversal, pause and shutdown. Each captured six 1100×800 frames: overview,
inside, coarse, returned, painted, edited-entrance. PNGs and a pixel-diversity report
are in the evidence directory. The core test also walks the minimum 4×2.5 m
opening with 6 m depth and checks that the inside camera and lantern fit.

Chrome 155.0.8059.85 passed the hybrid demo smoke separately on forced WebGPU and
WebGL2, with no console/page errors. A real drag cut 12 manual cells; before/after
canvas differences were 2,883 and 3,218 pixels respectively. Queries and collision
disappeared at the painted point and returned after filling. Cancellation restored
source/history. The entrance changed from 6×4.3×10 m to 8×3.5×12 m; paired undo/redo,
invalid width rejection, edited archive, motor entry/exit, resize and renderer
recovery passed.

The main editor terrain suite also passed on forced WebGPU and WebGL2 (DPR 1):
Hole/Fill controls, missing-source picking, visible pixel changes, cancellation,
ordered history, existing sculpt/color brushes, Play gating, save/reopen, compact
layout, and emulated second-pointer ownership. No page errors were recorded.

Commands used after building with repository Emscripten 6.0.11 and Windows SDK:

```powershell
ctest --test-dir build/windows -C Release -R '^(hybrid_terrain|editor_terrain|editor_authoring|terrain|terrain_paging|terrain_page_edit|scene_archive|render_frame)$' --output-on-failure
./build/windows/Release/webengine_hybrid_terrain_windows.exe --backend dawn --smoke build/evidence/hybrid-terrain-m2/dawn
./build/windows/Release/webengine_hybrid_terrain_windows.exe --backend d3d12 --smoke build/evidence/hybrid-terrain-m2/d3d12
$env:NODE_PATH=(Resolve-Path build/detail-browser-tools/node_modules).Path
$env:BROWSER_EXECUTABLE='C:/Program Files/Google/Chrome/Application/chrome.exe'
$env:EVIDENCE_DIR='build/evidence/hybrid-terrain-m2/browser'
node scripts/hybrid-terrain-smoke.cjs
$env:DEMO_URL='http://127.0.0.1:8158'
$env:EVIDENCE_DIR='build/evidence/hybrid-terrain-m2/editor'
node scripts/editor-terrain-smoke.cjs
python tools/verify_hybrid_terrain.py build/evidence/hybrid-terrain-m2
```

Port 8157 serves `build/web-release/hybrid-site`; port 8158 served the updated
main editor staged with its web files/assets in `build/hybrid-regression-site`.
The hybrid HTML/JS/Wasm served bytes matched local SHA-256 digests. Final Wasm:
`00830eb0286f1c2c3b2457231d0da477c538776839dec07d8ef8f17c9b20f8ed`.
All three digests are recorded in `artifact-hashes.json` in the evidence directory.

## Scope

Tests cover immutable mask edits, chunk sharing, empty/fill transitions, restored
LOD and collision, source-only resident streaming, authoring picks through holes,
multi-chunk and consecutive undo/redo, cancel, paired cave mesh/opening edits,
invalid-input retention, scene archives and walkable edited cave geometry.

Evidence directory: `build/evidence/hybrid-terrain-m2`. Browser runs use actual
DOM controls and pointer drags; native runs inject SDL contacts through DesktopHost
into retained GUI controls and scene input, with native GPU frame captures.
Native entrance fields are populated programmatically before activating the real
Apply control; this does not claim keyboard text-entry acceptance.

The disk filled during compilation. Only old, non-running generated test binaries
under verified `build/native` were removed; source and current builds were preserved.
The new tests found and fixed terrain history collision identity invalidation and
missing exception catching in the standalone browser demo build.

Not validated: physical Android/iOS, mobile performance, large-world authoring,
remote/provider hole paging, automatic arbitrary-mesh seams, or persistent demo
parameter/history restoration. Scene roundtrip checks preserve resulting geometry
and collision; they are not an OS file dialog or a document-editing workflow.
