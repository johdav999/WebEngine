# Cloth milestone three validation

Checks performed on 2026-10-25 in the shared Windows checkout, using repository-local
Emscripten 6.0.11. Existing unrelated work was preserved. M1/M2 evidence remains in
its original records; it is not counted as a new M3 run.

## Browser and portable contracts

Incremental `clothing_tests` and `clothing_demo` build passed. CTest
`clothing_contracts` passed 1/1 (2.29 seconds), including the new folded-particle
separation, free/pinned weighting, linked/rest-near exclusions, disabled default,
frame partition, floor/inside-box clearance, invalid-input rollback and pause cases.
The existing M1/M2 tests also passed, including rendered-stream atomicity after an
invalid environment update and actual demo obstacle contact.

Commands used:

```powershell
$env:EMSDK_PYTHON='C:/Users/EJOHDAV/source/repos/WebEngine/.tools/emsdk/python/3.13.3_64bit/python.exe'
$env:PATH='C:/Users/EJOHDAV/source/repos/WebEngine/.tools/emsdk/python/3.13.3_64bit;'+$env:PATH
cmake --build build/web-release --target clothing_tests clothing_demo -j 2
ctest --test-dir build/web-release -R '^clothing_contracts$' --output-on-failure
```

The first build encountered the Windows Python alias; selecting local Python fixed
compilation. Emscripten's sandbox temporary-directory write permissions then blocked
JavaScript linking; the authorized build was retried with the necessary access.

`scripts/clothing-browser-smoke.cjs` passed both backends in Edge 156.0.4314.42,
using `CLOTHING_SMOKE_OUTPUT=build/cloth-m3-smoke`. Reports and before/after/cloth
screenshots are retained there. Both cloth screenshots were visually inspected:
the cape bends against the visible block, the character and controls render, and
the surface remains attached to its shoulders.

| Backend | Outfit changed pixels | Environment projections at pause | Browser errors |
| --- | ---: | ---: | ---: |
| WebGL2 | 19,551 | 3,107 | 0 |
| WebGPU | 20,062 | 3,193 | 0 |

Smoke checks exercised wind movement, obstacle contact, frozen motion/contact
counters while paused, teleport/reset, equip/unequip, colors, save/load, rejected
invalid saves and reload. Self-contact was enabled in both demos, but these short
rendered runs recorded zero self-contact projections; separation itself is verified
by the deterministic overlapping-particle contract fixtures, not these screenshots.

## Native Windows

Re-ran the documented Visual Studio 18 2026 configuration with clothing, tests, demo
and Windows enabled in `build/clothing-native`. It failed because CMake could not
find any Visual Studio instance. No native executable was linked or run; neither
Dawn nor direct D3D12 runtime acceptance is established by this task.

`em++ -std=c++20 -fexceptions -fsyntax-only -Iinclude examples/clothing/windows.cpp`
passed with the repository-local SDK. This checks portable C++ integration only,
not MSVC compilation, Windows linking, native rendering or performance. Both hosts
use the same collision-enabled scene; the Windows smoke path now requires obstacle
contact in addition to motion and pause checks.

## Documentation and skill copies

`git diff --check` passed (line-ending warnings only); new feature-document links
passed the local link check. All six engine-skill copies passed `quick_validate.py`:
repository, EJOHDAV personal, RPGGame, CafeGame, required Johan personal and CatGame.
M3 reference hashes were checked across all copies. SKILL routes and bundled engine
guides were updated while preserving consumer-specific guidance and dependency pins.

This milestone implements discrete particle self-contact and plane/AABB proxies.
It does not establish triangle/edge contact, continuous collision, arbitrary mesh
collision, physical mobile acceptance, dense-garment performance or native parity.
