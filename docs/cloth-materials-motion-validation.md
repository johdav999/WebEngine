# Cloth milestones six to eight: validation

Checks performed on 2026-10-25. Implementation sequence: M6 weighted fabric, M7 aerodynamic airflow, then M8
translating capsule/triangle collision. Existing unrelated checkout changes were
preserved. Contracts and limitations are in [the feature guide](cloth-materials-motion.md).

## Sequential regression gates

- M6 Emscripten build and clothing_contracts passed 1/1 in 3.36 seconds.
- M7 Emscripten build and clothing_contracts passed 1/1 in 7.87 seconds.
- Initial M8 solver fixtures passed, but the shared demo deformation assertion
  failed. The demo's unit particle masses were too heavy for the aerodynamic
  replacement of uniform wind acceleration. The sample now uses inverse masses
  20 for fabric and 10 for its hem (0.05/0.1 kg respectively).

Mass fixtures cover unequal-weight link response, weighted edge-contact balance,
pins, mass-independent gravity, invalid inputs and unit-default compatibility.
Wind fixtures cover mass response, winding, tangential airflow, vacuum, default
disable, relative fabric/pin velocity, collapsed faces, partitioning and rollback.
Motion fixtures cover moving sphere/triangle crossings, reverse sweep, friction
transfer, pins, fractional-step retention, partitioning, pause/reset and rejection.

## Platform validation

Final Emscripten 6.0.11 Release builds of clothing_tests and clothing_demo passed.
CTest clothing_contracts passed 1/1 in **3.71 seconds** (3.85 total), with all eight
wardrobe/physical/particle/surface/edge/mass/wind/motion groups reported.

Native VS2022 MSVC 19.44 Release builds of clothing_tests and
clothing_demo_windows passed. Native CTest clothing_contracts passed 1/1 in
**6.33 seconds** (6.43 total). The build uses WEBENGINE_WINDOWS_DAWN=OFF and
WEBENGINE_WINDOWS_D3D12=ON. Native Dawn is still blocked by the previously verified
pinned-library/MSVC 19.44 link incompatibility; it was not rerun or claimed passed.

Both updated browser smoke paths passed in Edge 156.0.4314.42, with no reported
errors. Reports and screenshots are in `build/cloth-m8-browser`:

| Backend | Outfit changed pixels | Aerodynamic face evaluations at pause | Moving contact projections at pause |
| --- | ---: | ---: | ---: |
| WebGL2 | 20,775 | 20,736 | 1,611 |
| WebGPU | 22,377 | 20,736 | 1,813 |

Checks include rendered motion, positive aerodynamic/moving-contact counters,
frozen counters and obstacle while paused, block stop/restart, wind toggle,
teleport/reset, cape replacement and outfit save/load/invalid-load rollback.
Both runs recorded zero edge projections; earlier deterministic edge fixtures
remain the evidence for edge response.

`clothing_demo_windows.exe --d3d12 --smoke build/cloth-m8-native` exited 0 with
`Windows clothing smoke PASS`. Its checks require positive aerodynamic and moving
contacts, verify paused counters/obstacle, and exercise outfit persistence,
reset/teleport and shutdown. Native 1099x750 before/after captures differ in
23,695 pixels. The native cloth capture and both browser cloth captures were
inspected: the cape stays attached and visibly deforms around the obstacle, with
no broken geometry observed. The compact Windows panel now shows all thirteen
controls at the tested display scaling. This is not a full accessibility/input test.

All six engine-skill copies passed quick_validate.py; the new supporting reference
hashes match, and existing consumer-specific guidance was preserved. Feature links
and whitespace checks passed. An intermediate final-build attempt caught duplicate
test declarations; these were removed before the passing builds above.

Reproduction (repository-local SDK/Python environment as in README):

```powershell
cmake --build build/web-release --target clothing_tests clothing_demo -j 1
ctest --test-dir build/web-release -R '^clothing_contracts$' --output-on-failure
# Serve build/web-release/clothing-site at 127.0.0.1:4193; set NODE_PATH and
# BROWSER_EXECUTABLE for the installed Playwright and Edge.
node scripts/clothing-browser-smoke.cjs
cmake --build build/clothing-native-vs2022 --config Release --target clothing_tests clothing_demo_windows --parallel 1
ctest --test-dir build/clothing-native-vs2022 -C Release -R '^clothing_contracts$' --output-on-failure
./build/clothing-native-vs2022/Release/clothing_demo_windows.exe --d3d12 --smoke build/cloth-m8-native
```

These bounded solver checks do not establish universal nonintersection, rotating
proxy CCD, inter-garment collision, physical mobile acceptance or sustained
performance. Rendered tests need not produce edge contacts; deterministic edge
fixtures establish that response separately.
