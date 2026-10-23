# Planet stage 2 validation

Contracts and limitations are in [planetary physics](planet-physics.md).
Stage 1's historical evidence remains in [its validation record](planet-validation.md).
Stage 2 evidence is separate under `build/evidence/planet-stage2/final/`.

## Repeat

```powershell
cmake --build build/windows --config Release --target planet_tests planet_physics_tests webengine_planet_windows --parallel 6
ctest --test-dir build/windows -C Release -R '^planet(_physics)?$' --output-on-failure

$env:TEMP = "$PWD/build/planet-emcc-temp"
$env:TMP = $env:TEMP
cmake --build build/web-release --target planet_tests planet_physics_tests planet_demo --parallel 4
node build/web-release/planet_tests.js
node build/web-release/planet_physics_tests.js

$env:NODE_PATH = "$PWD/build/detail-browser-tools/node_modules"
$env:EVIDENCE_DIR = 'build/evidence/planet-stage2/final/browser'
node scripts/planet-smoke.cjs

./build/windows/Release/webengine_planet_windows.exe --backend dawn --smoke build/evidence/planet-stage2/final/native-dawn
python tools/verify_planet.py build/evidence/planet-stage2/final/native-dawn
./build/windows/Release/webengine_planet_windows.exe --backend d3d12 --smoke build/evidence/planet-stage2/final/native-d3d12
python tools/verify_planet.py build/evidence/planet-stage2/final/native-d3d12
```

Use repository-local Emscripten 6.0.11. Create the task-local temporary directory
first if absent. `BINARYEN_CORES=2` can limit optimizer concurrency on constrained
Windows hosts. Browser runner settings `PLANET_SITE`, `PLANET_URL`,
`PLANET_BACKEND`, `BROWSER_EXECUTABLE` and `EVIDENCE_DIR` retain their existing
meanings; omit `PLANET_BACKEND` to test both. The runner owns its ephemeral server
and browser and closes them after the run.

## Coverage

Portable tests exercise radial/inverse-square gravity, five up orientations,
free-fall landing, stationary support, jump/relanding, continuous surface travel
through a cube-face boundary and physics-origin rebasing, source collision identity
while visual LOD changes, blocking walls, missing-coverage holding, invalid-input
preservation, and a dynamic Jolt body landing under per-body radial gravity.

Browser acceptance uses actual buttons and keyboard/mouse input. It performs a
continuous orbit-to-ground trip, verifies support against the registered source
collision, tests rest and visual-detail independence, walks more than 115 m through
a physics rebase, jumps/relands, checks Escape/focus cancellation, releases
thrusters from a visible 20 m hover bookmark, verifies acceleration/contact and
cargo support, then takes off continuously to space. Screenshot pixel checks cover
the rendered surface/flight views. Native acceptance exercises the same shared
controller and simulation through DesktopHost input snapshots and GPU readbacks.

State reports include actual backend, fixed steps, collision source triangle count,
support gap, gravity, landings, jumps, origin changes, rest drift and resource bounds.
The guided descent is velocity controlled; landing uses the registered collision
mesh rather than a height clamp. Bookmark repositioning is explicitly separate
from the continuous descent and ascent paths.

## Recorded results

Verified 2026-10-23 using repository-local Emscripten 6.0.11 Release and MSVC
Release. All requested targets passed the complete loop:

| Target | Result | Ground detail | Walking distance | Rest drift | Physics-origin changes during walk |
| --- | --- | --- | --- | --- | --- |
| Chrome WebGPU, explicit backend 0 | Pass | LOD 18 | 115.006 m | 0 m measured | 1 |
| Chrome WebGL2, explicit backend 1 | Pass | LOD 18 | 115.006 m | 0 m measured | 1 |
| Windows Dawn/D3D12 | Pass | LOD 18 | 123.044 m | 0 m measured | at least 1 |
| Windows direct D3D12 | Pass | LOD 18 | 123.044 m | 0 m measured | at least 1 |

Both browsers sampled 774 descent states, reached supported body altitude
approximately 0.68183 m, and returned continuously to 5,733.9 km altitude.
Registered source ray distance at touchdown was approximately 0.68176 m.
Ground gravity measured approximately 9.79154 m/s², with inverse-square falloff
verified independently. Each run jumped and relanded, retained support through
visual-detail changes, cancelled held input, and completed an unpowered gravity
drop. Dynamic cargo support gap was approximately 0.35757 m. No browser page,
console or runtime errors were reported. Final frame counts were 4,503 (WebGPU)
and 4,501 (WebGL2); these are acceptance counts, not performance measurements.

Both Windows runs selected the requested backend, captured nine GPU readback
views, passed all pixel checks, performed a physical walk through source-chart
replacement, jumped/relanded, cancelled input, dropped under gravity, and took
off to orbit. Cargo gap measured approximately 0.35752 m; final leaf/resident
counts were 141/186 and only two source collision meshes were prepared for the
whole scripted loop. Native descent sampled 537 states without discontinuities.

MSVC CTest `planet` and `planet_physics` and both corresponding Wasm runners
passed. The physics test also proves a dynamic cargo body's global pose and
velocity survive a translation-only origin change. Repository documentation checks
verified 346 local links across eight documents. Both installed/CatGame skill
copies have matching stage 2 references, current API/build entries and preserved
CatGame-specific dependency guidance.

Final acceptance fixed two issues found by real runs: copied solver poses must
normalize scale to exactly one before the existing teleport API; native font/UI
preparation must finish before asynchronous GPU acquisition begins. The latter
removed the planet demo's Dawn startup timeout. Earlier failed/exploratory runs
are not counted as final-build acceptance.

Artifacts under `build/evidence/planet-stage2/final/`:

- `browser/report.json`, both trajectory JSON files and ten screenshots per
  renderer record the complete browser flow and visible output.
- `native-dawn/` and `native-d3d12/` contain reports, pixel results, PPM GPU
  readbacks and PNG previews for orbit, approach, low flight, surface, walking,
  relanding, gravity descent/landing and return to space.
- `artifact-hashes.json` records 19 final artifact/source SHA-256 identities,
  including demo JS/Wasm/HTML, Windows binaries, tests and stage 2 implementation.

## Limits

These checks establish desktop Chromium WebGPU/WebGL2 and native Windows behavior.
They do not establish physical Android/iOS, other browsers, sustained performance,
hard frame-time guarantees, exact source-surface collision, full rigid-body ship
flight, capsule/stair walking, planetary atmosphere/oceans or save compatibility.
The collision chart is bounded and synchronous; the motor waits if required
coverage is absent. Visual LOD remains an approximation, with stage 1 seam skirts
and material-transition limitations. Earlier exploratory runs are not final-build
acceptance evidence.
