# RPG library L3 validation

L3 adds exact 3D target selection and complete area-target enforcement to API 1.9.0.
See [contracts](rpg-targeting.md). Tested implementation: `9b49214`.

| Gate | Result | Evidence |
| --- | --- | --- |
| Standalone Linux | 18/18 passed | [RPG library](https://github.com/johdav999/WebEngine/actions/runs/40234230903) |
| Standalone Windows Release | 18/18 passed | Same RPG library run |
| Emscripten/Node | 18/18 passed | Same RPG library run |
| SDK and omission matrix | Windows/Linux/Wasm passed | [Portable module SDK](https://github.com/johdav999/WebEngine/actions/runs/40234230867) |
| Windows Release host/controller | Build and controller test passed | [Rendered hosts](https://github.com/johdav999/WebEngine/actions/runs/40234230869) |
| Windows D3D12 rendered smoke | Passed on WARP with CPU/GPU validation | Same rendered-host run, `rpg-windows-host` artifact |
| Browser controller and WebGPU/WebGL2 rendering | Passed; input, pixels, storage and recovery checks retained | Same rendered-host run, `rpg-browser-host` artifact |

The new `rpg_targeting` suite checks 3D sphere/cone/line boundaries, arbitrary
integer direction, self/team/LOS filtering, stable ordering, unassigned teams,
out-of-range aim points, malformed/duplicate/stale/nonresident facts, extreme
coordinates and bounded target lists. It verifies pure preview, complete target
enforcement, atomic multi-target resource effects, one-time AP/resource costs,
effect-budget rollback including RNG, stale world publication, stale quotes,
explicit LOS opt-out and save/restore continuity. Existing snapshot fixtures and
legacy tactical tests remain unchanged and pass.

The shared host's strike uses an L3 area query in both platform builds. The sample
keeps its range-only spatial policy; it is not evidence of game obstacle raycasts.
Unit tests exercise supplied LOS exclusions. Games still own those physics facts.

Local Emscripten 6.0.11 C++20 syntax checks passed for targeting.cpp, tactics.cpp
and rpg_targeting_tests.cpp. The full compiled/executed evidence above comes from CI.
Rendered CI uses Windows WARP and Linux Chromium SwiftShader (WebGPU under Xvfb,
WebGL2 headless). Fresh physical GPU and mobile acceptance were not performed.

The separate general engine suite retains its pre-existing vehicle and spotlight
failures (94/96), outside the passing RPG gates:
[general build](https://github.com/johdav999/WebEngine/actions/runs/40234230901).

API is 1.9.0; durable snapshot formats remain L2-compatible v1/v2. Installed
engine-skill copies were synchronized. RPGGame remains pinned to `c7db4b4`;
adoption of L2/L3 and game-specific Lattice rules are separate integration tasks.
