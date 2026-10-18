# Terrain stage-two validation — 2026-10-15

Verified on Windows using pinned Emscripten 6.0.11/Jolt 5.3.0 and desktop Chrome
155.0.8059.79. Builds/tests preserve concurrent editor and water work. No physical
mobile or sustained-performance acceptance is implied.

| Check | Result |
| --- | --- |
| Native regression suite | 45/45 CTest suites passed in `build/native-physics` |
| Native terrain detail | Triangle reduction, positive complete fan coverage, every original perimeter position/normal/UV at every LOD, partial chunks and independent camera/shadow selection passed |
| Native materials/archives | Layer definitions/texture sharing, semantic roles, validation/failed-edit preservation, LOD regeneration/policy restoration, allocation limits and a real format-10 fixture passed |
| Release browser build | Dedicated `build/web-terrain-stage2-release`; staged matching assets in `build/terrain-detail-browser-site` |
| WebGPU/WebGL 2 materials | Four separately rendered tiled layers, mixed color, linear weight roles, zero-weight fallback, equal weights and diagnostics passed |
| Automatic LOD | Changing projected-size scale selected finer/coarser geometry in both renderers |
| Rendered seams | Overhead full-detail, all-coarse and mixed-neighbor coverage differed by **zero pixels** on both backends |
| Independent shadow detail | WebGPU shadow triangles changed independently of camera triangles; camera LOD changed no shadow submissions. WebGL 2 consistently reported no shadow maps and disabled its shadow controls |
| Source geometry | Ground/collision heights unchanged through all camera/shadow choices; six physics ray samples matched source queries within 0.0003 m |
| Views/lifetime | Perspective/orthographic, canvas resize, demo reset/return/restart and projection restoration passed |
| Persistence/recovery | Format-12 layered appearance and mixed camera/shadow policies restored with matching paused pixels; corrupted archives preserved the valid scene; device/context recovery preserved rendered results |
| Base terrain regression | Heightmap import/failure preservation, Wasm rigid-body contact, real scene downloads, fresh-page restore and native format-10 fixture import passed in both backends |
| Cross ABI | Both stage-two browser downloads loaded, reserialized and validated natively: 769,418 bytes each |
| Browser errors | None recorded by either terrain smoke run |
| Documentation | Installed skill validator, JavaScript syntax, Markdown links and focused whitespace checks passed |

The default demo has 16 terrain chunks, each with 24x24 source cells. Its full
terrain is 18,432 triangles; LOD 3 retains 1,920 terrain triangles with the entire
source perimeter. Whole-demo submissions, including the ball and camera clear:

| Backend/pass | Full | Coarse | Independent pass |
| --- | ---: | ---: | --- |
| WebGPU camera | 19,153 | 2,641 | Shadow stayed at 19,152 |
| WebGPU shadows | 19,152 | 2,640 | Camera stayed at 19,153 |
| WebGL 2 camera | 19,153 | 2,641 | No shadow-map pass |

WebGPU shadow-detail changes affected 35,198 camera pixels in this fixture while
leaving its camera triangle count unchanged. These are geometry/submission and
pixel observations, not measured FPS gains. The retained full perimeter and extra
LOD assets have CPU/GPU memory costs.

Tested Wasm SHA256:

```text
71F4599B1110D72C187C9799428AD279A3023877FA5BEFB18DCEA02B90D5E810
```

The dedicated build and staged site matched this hash. Evidence:

- `build/evidence/terrain-detail/result.json`: both-backend detail counters/results.
- `build/evidence/terrain-detail/`: layer, weight, overhead coverage, shadow,
  archive, resize/recovery and UI screenshots; actual downloaded archives.
- `build/evidence/terrain-stage2-base/`: the repeated base-terrain acceptance.
- `build/evidence/terrain-detail/native-ctest.log`: native suite output.
- `tests/fixtures/terrain_v10.wescene`: the original engine-generated native
  stage-one fixture (source grid, collider and material), retained for compatibility.

The preview serves the staged site at `http://localhost:8179/?scene=terrain`.
Reproduce browser checks with the repository's Playwright/pngjs dependencies:

```powershell
$env:NODE_PATH=(Resolve-Path build/detail-browser-tools/node_modules).Path
$env:BROWSER_EXECUTABLE='C:/Program Files/Google/Chrome/Application/chrome.exe'
$env:DEMO_URL='http://localhost:8179'
node scripts/terrain-detail-smoke.cjs
$env:EVIDENCE_DIR='build/evidence/terrain-stage2-base'
$env:NATIVE_TERRAIN_PATH=(Resolve-Path tests/fixtures/terrain_v10.wescene).Path
node scripts/terrain-smoke.cjs
```

The installed `engine-skill` entrypoint and contract, integration and validation
references were updated. See [terrain contracts](terrain.md) for defaults,
ownership, backend limits and remaining extension scope.
