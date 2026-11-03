# Cloth milestone four validation

Checks performed on 2026-10-25 in the shared Windows checkout. Unrelated changes
were preserved. M1–M3 records remain historical evidence, not new M4 test runs.

## Build and contract tests

The final incremental `clothing_tests` and `clothing_demo` build passed using the
repository-local Emscripten 6.0.11 SDK. CTest `clothing_contracts` passed 1/1 in
3.59 seconds. The test log reports all four groups: M4 surfaces, M3 collision,
M2 physical clothing and M1 clothing contracts.

New cases exercise thin triangle crossings from both sides, misses outside finite
triangle edges, swept thin boxes/sphere capsules, a vertex crossing a pinned face,
a moving face contacting a pinned vertex, barycentric response, discrete surface
thickness, frame partition, pause, malformed/duplicate/index-invalid topology,
faces absent from the garment mesh, invalid environment rejection and unchanged
render revision after rejection. The shared scene registers triangle contact.

```powershell
$env:EMSDK_PYTHON='C:/Users/EJOHDAV/source/repos/WebEngine/.tools/emsdk/python/3.13.3_64bit/python.exe'
$env:PATH='C:/Users/EJOHDAV/source/repos/WebEngine/.tools/emsdk/python/3.13.3_64bit;'+$env:PATH
cmake --build build/web-release --target clothing_tests clothing_demo -j 2
ctest --test-dir build/web-release -R '^clothing_contracts$' --output-on-failure
```

The build used authorized temporary-directory access for Emscripten linking,
following the sandbox write failure diagnosed during M3.

## Browser

`scripts/clothing-browser-smoke.cjs` passed WebGL2 and WebGPU in Edge 156.0.4314.42.
Reports and screenshots are in `build/cloth-m4-smoke`, selected with
`CLOTHING_SMOKE_OUTPUT`. Both cloth captures were visually inspected: the cape stays
attached to the animated shoulders, bends against the block and renders without
broken geometry. The scene supplies the visible block's transformed mesh triangles.

| Backend | Outfit changed pixels | Surface projections at pause | Swept projections at pause | Errors |
| --- | ---: | ---: | ---: | ---: |
| WebGL2 | 20,167 | 5,728 | 5,728 | 0 |
| WebGPU | 20,522 | 5,947 | 5,947 | 0 |

The smoke verifies movement, environment/surface/swept contact, frozen motion and
all contact counters during pause, teleport/reset, equip/unequip, outfit/color
changes, save/load, rejection of an invalid save and reload. The short rendered
runs had zero self-contact projections; self vertex/face separation and fast
crossings are established by deterministic contract fixtures, not the screenshots.
These tests are not sustained performance measurements or physical mobile checks.

## Native Windows

Re-ran configuration in `build/clothing-native` with Visual Studio 18 2026 x64,
clothing/tests/demo and Windows enabled. It failed because CMake cannot find any
Visual Studio instance. No native executable was linked or run; neither Dawn nor
direct D3D12 runtime acceptance is established by this task.

The updated `examples/clothing/windows.cpp` passed
`em++ -std=c++20 -fexceptions -fsyntax-only -Iinclude` with the local SDK. This checks
portable C++ usage, not MSVC compilation or Windows linking/rendering. The native
host uses the same surface-enabled scene and requires triangle contact in its smoke.

## Documentation and limits

Documentation links and `git diff --check` passed (line-ending warnings only).
All six engine-skill copies passed `quick_validate.py`: repository, EJOHDAV personal,
RPGGame, CafeGame, required Johan personal and CatGame. M4 reference hashes matched;
SKILL routes and bundled engine guides were updated without changing consumer pins
or overwriting project-specific guidance.

This milestone covers vertex/triangle response and bounded linear sweeps, not full
mesh/mesh CCD. Edge/edge, inter-garment, swept particle-pair and moving external-proxy
collision guarantees remain absent. Conservative search exhaustion can over-constrain
motion. Conflicting constraints, finite iterations and nonlinear motion can still
intersect; dense-garment performance and native parity remain unverified.
