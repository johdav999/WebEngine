# Cloth milestone five validation

Checks performed on 2026-10-25 in the shared Windows checkout. Unrelated work was
preserved; prior milestone records remain historical evidence, not new M5 runs.

## Build and contracts

The final incremental `clothing_tests` and `clothing_demo` build passed with the
repository-local Emscripten 6.0.11 SDK. CTest `clothing_contracts` passed 1/1 in
3.65 seconds. The log reports all five groups: edges, surfaces, particle collision,
physical clothing and wardrobe contracts.

The edge fixture first verifies that M4 vertex/face contact misses its crossing,
then checks M5 separation at the edge midpoint. Cases include both sweep directions,
discrete overlap, pinned endpoints, four-free-endpoint mass balance, linked-neighbor
exclusion, all-pinned pairs, frame partition, positive-counter pause/reset/rollback,
missing topology and a 2049-edge budget rejection within valid vertex/face bounds.
Geometry cases cover parallel overlap, collapsed/coincident segments and finite
segments that would intersect only if extended into infinite lines.

```powershell
$env:EMSDK_PYTHON='C:/Users/EJOHDAV/source/repos/WebEngine/.tools/emsdk/python/3.13.3_64bit/python.exe'
$env:PATH='C:/Users/EJOHDAV/source/repos/WebEngine/.tools/emsdk/python/3.13.3_64bit;'+$env:PATH
cmake --build build/web-release --target clothing_tests clothing_demo -j 1
ctest --test-dir build/web-release -R '^clothing_contracts$' --output-on-failure
```

Initial C: free space was about 28 MB. The build ran serially to reduce temporary
usage; free space subsequently increased to about 2.1 GB before browser validation.
Emscripten linking used authorized access to its normal temporary directory.

## Browser

`scripts/clothing-browser-smoke.cjs` exited successfully for WebGL2 and WebGPU in
Edge 156.0.4314.42. Reports and before/after/cloth screenshots are retained in
`build/cloth-m5-smoke` (`CLOTHING_SMOKE_OUTPUT`). Both cloth captures were inspected:
the cape stays attached, deforms against the obstacle and has no visible broken
geometry. Both hosts' shared scene enables edgeSelfCollision on the cape topology.

| Backend | Outfit changed pixels | Surface/swept projections at pause | Edge projections at pause | Errors |
| --- | ---: | ---: | ---: | ---: |
| WebGL2 | 20,170 | 5,707 | 0 | 0 |
| WebGPU | 20,428 | 6,042 | 0 | 0 |

Smoke checks cover motion, obstacle/surface/swept contact, frozen motion and all
counters during pause, teleport/reset, equip/unequip, colors, save/load, invalid
save rejection and reload. The short rendered runs did **not** generate edge
contacts; the deterministic crossing fixtures establish edge response, not these
screenshots. These are not sustained performance tests or physical mobile checks.

## Native Windows

Re-ran Visual Studio 18 2026 x64 configuration in `build/clothing-native` with
clothing/tests/demo and Windows enabled. CMake could not find any Visual Studio
instance on the initial attempt. The subsequent retry is recorded below.

Retry: C: free space increased to about 6.1 GiB and Visual Studio 2022 Build Tools
17.14.42 became available. The old VS2026 configuration still failed, but a fresh
`build/clothing-native-vs2022` configuration with `Visual Studio 17 2022`, x64,
MSVC 19.44 and Windows SDK 10.0.26100 succeeded. The repository's pinned DXC,
SDL and Dawn packages were downloaded and SHA256-verified by
`tools/setup_windows.py`. Configuration reused the existing Jolt source via
`FETCHCONTENT_SOURCE_DIR_JOLT=.../build/web-release/_deps/jolt-src`.

The native Release `clothing_tests` executable built and CTest
`clothing_contracts` passed 1/1 in 197.20 seconds (200.45 seconds total). This was
an actual MSVC/native run, including all five clothing contract groups. Available
physical memory was about 1 GiB during the run; this is not a performance baseline.

The first demo build exposed MSVC C2026 in the generated WGSL header. Shared
`cmake/shaders.cmake` now splits embedded shader lines into adjacent raw literals.
All 14 reconstructed shader strings matched the previous generated contents, and
the native Dawn renderer library then compiled successfully. Direct D3D12's
renderer library also built. The complete Dawn demo failed to link because the
pinned prebuilt `webgpu_dawn.lib` references newer STL symbols absent from MSVC
19.44: `__std_rotate`, `__std_min_element_8u`, `__std_min_element_4u`,
`__std_unique_8`, `__std_max_element_4u`, and `__std_unique_4`. Dawn execution
remains unverified on this machine; a compatible newer toolchain is required.

With `WEBENGINE_WINDOWS_DAWN=OFF` and `WEBENGINE_WINDOWS_D3D12=ON`, the Release
`clothing_demo_windows` build succeeded. Running
`clothing_demo_windows.exe --d3d12 --smoke build/cloth-m5-native-d3d12` exited 0
with `Windows clothing smoke PASS`. This checks outfit changes, disk save/restore,
cloth stepping, environment/surface contacts, frozen cloth/edge counters during
pause, teleport/reset, cape replacement and orderly shutdown. It does not require
a positive edge contact count; deterministic contracts establish that response.

Native before/after captures are 1099x750 and differ in 19,962 pixels. The converted
`after.png` was inspected: character, attached cape, obstacle and wardrobe render.
At the captured display scaling, the fixed-height menu extends below the viewport;
the smoke invokes commands directly and does not establish access to every button.
Artifacts are in `build/cloth-m5-native-d3d12`.

After the shader embedding fix, the Emscripten browser build passed again and
`clothing_contracts` passed 1/1 in 2.85 seconds (2.91 seconds total).
The fresh browser smoke passed both WebGL2 and WebGPU with no errors in Edge
156.0.4314.42. Artifacts are in `build/cloth-m5-retry-browser`: WebGL2 changed
22,372 pixels with 5,711 surface/swept projections at pause; WebGPU changed 20,464
pixels with 5,691 projections. Both recorded zero edge projections, so the same
deterministic-fixture limitation applies. All six final skill copies validated
with matching edge-reference hashes. Final free disk space was about 4.9 GiB.

The updated native demo passed the repository-local
`em++ -std=c++20 -fexceptions -fsyntax-only -Iinclude examples/clothing/windows.cpp`
check. This establishes portable C++ usage only, not MSVC or native renderer
acceptance. Windows uses the same edge-enabled scene and now checks paused edge
counters as part of its smoke lifecycle.

## Documentation and remaining limits

Feature links and `git diff --check` passed (line-ending warnings only). All six
engine-skill copies passed `quick_validate.py`: repository, EJOHDAV personal,
RPGGame, CafeGame, required Johan personal and CatGame. M5 reference hashes match;
SKILL routes and bundled engine guides were updated without changing consumer pins
or overwriting project-specific guidance.

This is bounded edge/edge self-contact within one garment. Search exhaustion can
cause early contacts; conflicting constraints, finite iterations, initial
intersections and nonlinear motion can still intersect. Inter-garment contact,
moving external-proxy history, universal mesh CCD and dense-garment/native
performance are not established by this milestone.
