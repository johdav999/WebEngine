# Terrain surface validation

Desktop validation on 2026-10-17 uses repository-local Emscripten 6.0.11, the
isolated `build/web-terrain-surface` Release build and Chrome 155.0.8059.80.
The staged site is `build/terrain-surface-site`, served at
`http://localhost:8191/?scene=terrain`. This increment delivers runtime surface
rendering: packed per-layer normal/height maps, height-based blending and optional
world-space triplanar sampling. See [contracts](terrain.md#surface-relief-and-projection).

## Portable checks

The six focused CTests `terrain`, `terrain_paging`, `terrain_page_edit`, `material`,
`scene_archive` and `editor_terrain` pass in `build/native-physics`. Coverage includes
invalid parameter rejection, inactive projection configuration retention, map-only
appearance replacement without geometry regeneration, binding identity, normalized
normal mip RGB with linear height alpha, format-23 map/parameter persistence,
shared page-worker maps, source/query/collision invariants and existing edit/writeback.
Legacy archive fixtures continue loading with old defaults.

Commands:

```powershell
cmake --build build/native-physics --target terrain_tests terrain_paging_tests terrain_page_edit_tests material_tests scene_archive_tests editor_terrain_tests -j6
ctest --test-dir build/native-physics -R '^(terrain|terrain_paging|terrain_page_edit|material|scene_archive|editor_terrain)$' --output-on-failure
```

Evidence: `build/terrain-surface-native-focused.log` and
`build/terrain-surface-native-results.log`.

## Browser checks

`scripts/terrain-surface-smoke.cjs` forces WebGPU and WebGL2 independently. It compares
linear mesh UVs to normals, height blending, triplanar and the combined appearance,
then checks layer isolation, zero-weight fallback, invalid edit preservation,
camera/shadow triangle independence, full/coarse/mixed rendered coverage, ground
and collision invariance, perspective/orthographic framing, resize, format-23 save/load,
malformed archive preservation, paused device/context recovery and demo controls.
GPU shader parsing/validation warnings fail the suite as well as browser errors.

Evidence: `build/evidence/terrain-surface/result.json` and backend PNGs. Separate
normal, height and projection modes change thousands of rendered pixels on each
backend; screenshots were visually inspected. Both full/coarse/mixed coverage
comparisons remain closed. WebGL2 keeps basic lighting and has no shadow maps/PBR.

The five earlier terrain suites (`terrain-smoke`, `terrain-detail-smoke`,
`terrain-edit-smoke`, `terrain-paging-smoke`, `terrain-page-edit-smoke`) exercise
imports, collision/settling, edits, worker pages, halo seams, bounded residency,
dirty-page CAS/retry/discard/persistent reload, archives and recovery. Paging warms
initial material pipelines before its existing short delayed-IO frame-progress
assertion, separating shader startup from IO execution. `material-smoke` and
`water-smoke` cover conventional material/water binding regressions.

```powershell
$env:NODE_PATH=(Resolve-Path build/detail-browser-tools/node_modules).Path
$env:BROWSER_EXECUTABLE='C:/Program Files/Google/Chrome/Application/chrome.exe'
$env:DEMO_URL='http://localhost:8191'
node scripts/terrain-surface-smoke.cjs
```

Regression artifacts are under `build/evidence/terrain-surface-*`; logs are
`build/terrain-surface-*.log`. Deploy matching host and worker JS/Wasm plus shell.
Final result: **6/6 native suites and all eight browser suites pass**, with empty
browser error lists. The six terrain suites and water explicitly test both backends;
material-smoke uses its default automatic backend. `final-summary.json` in the
surface evidence folder maps each result and records artifact identity.

Final staged Wasm SHA256 values:

- Main: `2FFBA259E9D49F57CA1E8899B55840CBE71E1AF046A372477E220AFDDB178083`
- Worker: `F1AF9569CE76E58BB0A2BC7C8EA877C8E99E801F0D51C787C316DD16FB4228C4`

Main/worker JS and Wasm were compared with the build output. Final suite logs use
`build/terrain-surface-final-*.log`.

## Documentation and scope

Architecture, terrain/archive/persistence contracts and both installed/CatGame
engine-skill copies were updated. Both skills pass quick_validate plus relative
link/content checks; scoped comparison confirms previous guidance is preserved,
including CatGame's engine pin and local override/build rules. Evidence:
`build/terrain-surface-skills-validation.log` and `build/terrain-surface-skill-backup`.
The CatGame engine gitlink and gameplay were not changed.

Triplanar costs three samples per color/relief map and stays fixed in world space.
These effects do not displace geometry or change collision/ground queries. Desktop
browser evidence does not establish physical Android/iOS acceptance or sustained
mobile frame time. No CatGame runtime integration, parallax/displacement, geomorphing,
holes/overhangs or new shadow implementation is included.
