# Hybrid terrain milestone 3 validation (2026-10-22)

## Delivered scope

`buildTerrainSeam` adds bounded source-boundary matching, small-gap welding,
source sample insertion, tessellation, normal transitions and two-layer material
blending. The shared demo starts with a 15 cm inset shell and rebuilds the join
after hole/entrance edits. Both hosts expose a seam close-up and material-only
blend comparison. Existing collision and v35 archives store the prepared mesh.

## Portable and native Windows

Release `terrain_seam` and `hybrid_terrain` passed, together with `editor_terrain`,
`editor_authoring`, `terrain`, `terrain_paging`, `terrain_page_edit`, `scene_archive`
and `render_frame`: 9/9 focused CTests. Additional seam tests passed for opposing
undersides, triplanar scale/offset/detail-map preservation and boundary budgets.
Core checks cover matching corners and every boundary sample, continuous collision
rays, material/normal endpoints, unchanged sources, translations, malformed input,
unsupported material rejection, and work/triangle caps. The ceiling regression
caught during development is fixed: opposing undersides do not weld or blend.

The actual SDL Windows demo ran `--smoke` with strict `--backend dawn` and
`--backend d3d12`. Checks include native GUI touch events for painting/restoring,
undo/redo, cancellation, editable entrance fields, invalid-value preservation,
walking in/out, detail selection, scene archive copies, seam close-up and blend
comparison. The blend toggle preserves the exact collider resource. Ray scans
cross both sides of the roof seam, rear edge and entrance toe in live and restored
scenes. Native screenshots are 1100x800. `tools/verify_hybrid_terrain.py` checks
nonblank scene detail and changed material pixels outside the controls.

## Browser

`scripts/hybrid-terrain-smoke.cjs` ran Chrome 155.0.8059.85 with forced WebGPU and
WebGL2. Actual DOM buttons and pointer drags cut and restored 12 cells; queries
and physics agreed. The suite covers grouped history, Escape cancellation,
8 m wide / 3.5 m high / 12 m deep entrance edits, invalid input, motor traversal,
renderer loss/recovery, archive checks and a 780x700 viewport. It compares blend
on/off pixels from the same camera, asserts unchanged triangle counts and runs
live/archive seam collision scans at both detail settings. No unexpected console
or page errors occurred. The edited generated cave contains 6,106 triangles.

## Reproduction and evidence

Use the build and launch commands in [hybrid terrain](hybrid-terrain.md).
The repository SDK is Emscripten 6.0.11. Browser host/worker and optional demo were
rebuilt in `build/web-release`; native targets use `build/windows/Release`.
The served demo is `http://localhost:8157/`, with `?backend=webgpu|webgl2`.
Set `EVIDENCE_DIR=build/evidence/hybrid-terrain-m3/browser` for browser smoke.
Native smoke directories are `build/evidence/hybrid-terrain-m3/dawn` and `d3d12`.
Run `python tools/verify_hybrid_terrain.py build/evidence/hybrid-terrain-m3`.

Machine-readable reports and PNG/PPM captures live in
`build/evidence/hybrid-terrain-m3/`: `browser/report.json`, `native-pixels.json`,
and each backend's seam-blended/unblended images. Build artifact SHA256 values
are recorded in `artifact-hashes.json`; served HTML/JS/Wasm were compared to disk.

## Limits

This is bounded small-gap preparation for approximately matching, axis-aligned
source geometry. It does not construct arbitrary intersections, close unrelated
holes, simplify meshes, stream provider hole data, or blend painted multilayer
source materials. Generated geometry retains full detail. The comparison toggle
changes material weights only; welded geometry and the normal transition remain.
No physical mobile, large-world throughput, sustained performance or other-GPU
acceptance is claimed. Consumer dependency pins were not advanced. The main editor
brush regression was covered by focused native tests; its full browser editor
suite was not rerun for this milestone, which changes the standalone demo.
