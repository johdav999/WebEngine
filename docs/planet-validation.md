# Planet stage 1 validation

The [planet contracts](planet-terrain.md) describe the implemented stage and its
limits. Acceptance uses one generated site and a shared C++ demo on the requested
platforms. Evidence is written under `build/evidence/planet/`.

## Repeatable checks

```powershell
ctest --test-dir build/windows -C Release -R '^planet$' --output-on-failure
node build/web-release/planet_tests.js

$env:NODE_PATH = "$PWD/build/detail-browser-tools/node_modules"
# Or use another installed Playwright/pngjs location.
node scripts/planet-smoke.cjs

./build/windows/Release/webengine_planet_windows.exe --backend dawn --smoke build/evidence/planet/native-dawn
python tools/verify_planet.py build/evidence/planet/native-dawn
./build/windows/Release/webengine_planet_windows.exe --backend d3d12 --smoke build/evidence/planet/native-d3d12
python tools/verify_planet.py build/evidence/planet/native-d3d12
```

`PLANET_SITE` selects a generated site for the browser runner; its default is
`build/web-release/planet-site`. The runner serves that exact directory on an
ephemeral loopback port and closes its own server/browser when complete. Optional
`PLANET_URL` selects an already running server instead. `PLANET_BACKEND=webgpu`
or `webgl2` restricts a run; omit it to verify both. `BROWSER_EXECUTABLE` selects
Chrome/Edge; `EVIDENCE_DIR` changes browser evidence output.

Core tests cover matching cube-face edges, outward surface winding, valid normals
and indices, actual parent morph endpoints, double-before-float subtraction,
complete/non-overlapping six-face quadtree coverage, rejected-update preservation,
four-mesh preparation limits, bounded residency, continuous descent refinement,
source independence from LOD, and reclamation during ascent. These run as native
MSVC and the same portable code compiled to Wasm.

Browser acceptance exercises actual buttons, renders orbit/approach/low/surface/
return views, samples continuous descent positions, checks the effective renderer,
surface refinement, leaf/resident bounds, mouse look, manual movement, Escape and
focus cancellation, and ascent merges. Pixel checks exclude most UI and require
visible shaded terrain rather than trusting state alone. Runtime/page/console
errors fail the run. The script records trajectories and per-backend reports.

Native acceptance drives the shared flight controller through the normal desktop
Application input snapshots. It captures orbit, approach, low flight, surface,
scripted cube-face traversal and return-to-space views. It verifies descent
monotonicity, source clearance, local camera origin, detailed refinement, manual
movement, cancellation and residency. The scripted seam traversal is diagnostic
position sampling; it is not a physical flight dynamics test. Native capture emits
PPM files; `verify_planet.py` checks pixels and writes PNG previews.

## Recorded results

Verified 2026-10-23 with repository-local Emscripten 6.0.11 Release and MSVC
Release. The final run uses `build/evidence/planet/final/`:

| Target | Result | Continuous descent samples | Surface detail | Presented frames through return |
| --- | --- | --- | --- | --- |
| Chrome WebGPU, explicit backend 0 | Pass | 754 | LOD 18 at 4 m | 3,122 |
| Chrome WebGL2, explicit backend 1 | Pass | 755 | LOD 18 at 4 m | 3,095 |
| Windows Dawn/D3D12 | Pass | 537 | LOD 18 at 4 m | 1,760 |
| Windows direct D3D12 | Pass | 537 | LOD 18 at 4 m | 1,761 |

Browser runs returned to 5,733.9 km altitude, reclaimed detailed patches and
reported no page, console or runtime errors. Largest observed relative altitude
steps during sampled descent were 2.05% (WebGPU) and 3.01% (WebGL2). Both browser
runs checked actual descent/ascent buttons, strafe, right-drag look, Escape and
focus cancellation, visible orbit/approach/low/surface/return pixels, camera at
local origin and a 240-leaf/318-resident surface tree.

Both Windows runs selected the requested backend, passed input cancellation and
local-origin checks, sampled a 101-step scripted cube-face crossing, and captured
all six views. Their largest relative descent step was 2.50%; final residency
after the seam traversal and return was 165 leaves/218 nodes. Pixel verification
passed on every capture and produced PNG previews from the GPU PPM readbacks.

MSVC CTest `planet` and the Wasm `planet_tests.js` runner both passed after the
final geometry/source changes, including raised-terrain horizon visibility and
fine-detail descent to level 18. Documentation checks verified 327 local links
across the five touched repository documents. The installed and CatGame skills
both have matching planetary references, contract-map/build guidance and stage 1
SKILL entries; CatGame-specific dependency/pin guidance was retained.

Evidence files:

- `final/browser/report.json` and `{webgpu,webgl2}-trajectory.json` record browser
  state, input and pixel results; PNGs capture the five flight views.
- `final/native-{dawn,d3d12}/report.json` and `pixels.json` record Windows results;
  PPM and PNG files capture all six views.
- `final/artifact-hashes.json` records SHA-256 identities for demo JS/Wasm/HTML,
  native executable/SDL, core test artifacts and the implementation/test sources.

Earlier exploratory runs outside `final/` used preceding geometry or startup
code and do not establish acceptance of the final build. No FPS or hard frame-time
claim is inferred from the frame/sample counts.

## Scope

These are desktop Chromium GPU and Windows desktop checks. They do not establish
physical mobile, non-Chromium, sustained performance, landing physics, arbitrary-up
walking, authored terrain, atmosphere, oceans or save/load support. Skirts hide
visual LOD seams; this stage does not supply welded planetary collision geometry.
