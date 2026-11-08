# R10 release audit evidence

Baseline: `077d0eba22330177e797cff9fd699de2c1ffc4fe`. R10 changes the independent
consumer, CI evidence retention and documentation; kernel and rendered runtime
sources are unchanged from validated R9 runtime
`0f951f70e896eb99637afae9449595b351672d1c`.

API remains 1.7.0, snapshot/catalogue/bridge wire 1, PCG32 version 1. No consumer
pin, paid authoring service or release tag is changed.

## Commands

From the engine root, CMake 3.22+ and a C++20 compiler:

```sh
cmake -S examples/rpg -B build/rpg-sdk -DWEBENGINE_SOURCE_DIR=/absolute/WebEngine -DWEBENGINE_BUILD_RPG=ON -DWEBENGINE_BUILD_RPG_TESTS=ON -DWEBENGINE_BUILD_MODULE_TESTS=ON -DWEBENGINE_BUILD_QUESTS=ON -DWEBENGINE_BUILD_QUEST_TESTS=ON -DWEBENGINE_BUILD_RPG_QUESTS=ON
cmake --build build/rpg-sdk --config Release --parallel 4
ctest --test-dir build/rpg-sdk -C Release --output-on-failure
```

Wasm uses the same options with the repository's Emscripten **6.0.11**, SDK Node
**24.19.0**, Ninja and `-DCMAKE_BUILD_TYPE=Release`. Activate emsdk, then prefix
configuration with `emcmake`. Windows uses MSVC x64 Release. CI records compiler/
Node versions and uploads CMakeCache plus LastTest.log for each platform.
`rpg_cmake_contract` builds/runs default-off and all-off consumers and checks
missing Modules, Quests, RPG and invalid standalone-demo errors. `rpg_bridge_off`
builds/runs base RPG with Quests omitted, including the new two-ruleset consumer.
Both check no dependency download directory; standalone CMake rejects engine core,
Jolt, desktop and GUI targets. Configuration adds only local modules/rpg and
explicitly selected quests subdirectories; no FetchContent/download calls occur.

The added `rpg_rulesets` check produces the fixed behavioral fixture:

```text
scholar: actor=1 ap=1 target=16
operator: actor=2 ap=1 target=1500
```

Assertions cover equipment modifiers, opposite initiative policy, resource/AP
spending, RNG use vs deterministic effects, stable restore without repeated damage,
and rejection preserving the entire live state. Existing suites retain their
canonical state/RNG/snapshot fixtures and Modules/Quests regressions.

## Results and remaining gates

Local Emscripten Release compilation and `rpg_rulesets` passed. Local MSVC is unavailable;
native Windows compilation is performed in the workflow, not claimed locally.
The first sandbox link attempt stalled; retry uses access to the local SDK cache.
The full local rerun timed out in `rpg_cmake_contract` at 180 seconds after the
default-off nested consumer passed. The subsequent redundant bridge-off build
was stopped explicitly; these local omission checks are not claimed passed.
The complete omission/configuration matrix passed on all three CI platforms below.
The focused local rerun excluding those two configuration tests passed **14/14**
in 344.96 seconds, including the final exact ruleset fixture and all kernel suites.

Local standalone configuration used CMake 4.2.1 and the bundled Python directory
prepended to PATH (the Windows Store Python alias could not launch emcc):

```powershell
cmake -S build/rpg-r9/checkout/examples/rpg -B build/rpg-r10/wasm -G Ninja -DCMAKE_BUILD_TYPE=Release -DWEBENGINE_SOURCE_DIR=C:/Users/EJOHDAV/source/repos/RPGGame/build/rpg-r9/checkout -DCMAKE_TOOLCHAIN_FILE=C:/Users/EJOHDAV/source/repos/WebEngine/.tools/emsdk/upstream/emscripten/cmake/Modules/Platform/Emscripten.cmake -DCMAKE_CROSSCOMPILING_EMULATOR=C:/Users/EJOHDAV/source/repos/WebEngine/.tools/emsdk/node/24.19.0_64bit/node.exe -DWEBENGINE_BUILD_RPG=ON -DWEBENGINE_BUILD_RPG_TESTS=ON -DWEBENGINE_BUILD_MODULE_TESTS=ON -DWEBENGINE_BUILD_QUESTS=ON -DWEBENGINE_BUILD_QUEST_TESTS=ON -DWEBENGINE_BUILD_RPG_QUESTS=ON
cmake --build build/rpg-r10/wasm --parallel 2
ctest --test-dir build/rpg-r10/wasm -E 'rpg_cmake_contract|rpg_bridge_off' --output-on-failure
```

CMake's Emscripten shared-library warning does not affect these static targets.
No `_deps` directory was created in the standalone build. Documentation link
checks passed; the six skill packages were read back after synchronization.

See [R9 validation](rpg-r9-validation.md) for established WebGL2, hardware WebGPU
and Intel Iris Xe D3D12 evidence on the unchanged sample runtime. The hosted
rendered workflow remains a release gate: Ubuntu software WebGPU device destruction
and Windows runner startup access violation `0xc0000005` have not been resolved.
The latter's cause is unproven. These are failures, not successful skips. No
physical driver reset, mobile device, sustained performance or broad driver
coverage is claimed. [The release checklist](rpg-release-1.md) remains conditional.

RPGGame adoption is documentation only; its engine pin remains
`56080eca124c9a62e15b146799a8004bdad5a9b2`. Required available engine-skill copies
are synchronized with the release candidate and its remaining gates.

## Validated candidate and platform evidence

Published candidate: **`1d3685b30343ed54723aed629468973bbed9eaeb`**.
[RPG CI run](https://github.com/johdav999/WebEngine/actions/runs/40210781271):
all **16 CTests passed on each of three platforms**:

| Platform | Toolchain | Result |
| --- | --- | --- |
| Windows 2022 x64 | MSVC 19.44.35229.0, Release | 16/16 passed |
| Ubuntu 24.04 | GCC 13.3.0 | 16/16 passed |
| Wasm / Node | Emscripten 6.0.11, SDK Node 24.19.0, Release | 16/16 passed |

The default Linux single-configuration build does not select Release; the Windows
and Wasm builds do. Configuration/omission tests include their own nested builds.
Logs and cache artifacts are retained per platform by `rpg.yml`.
The [separate Modules SDK regression workflow](https://github.com/johdav999/WebEngine/actions/runs/40210781280)
provides additional existing-consumer coverage.

Fresh local R10 rendered rechecks passed on unchanged R9 host/kernel sources:

- Edge 155.0.4283.62 hardware WebGPU: full input/pixel/storage/reload/shutdown
  script passed; 349638 visible pixels, eight quantized colors.
- Same Edge, SwiftShader WebGL2: same script passed; 349636 visible pixels,
  eight quantized colors.
- CI-built Windows Release executable from exact runtime `0f951f70...`: hidden
  SDL input/D3D12 smoke exited 0; before/after 1099x759 captures passed frame checks
  with 103/104 colors and 293175/297970 visible pixels.

The WebGPU screenshot was inspected. The native executable and browser Wasm were
reused because `git diff 0f951f70 HEAD -- src include cmake` and the shared/host
source comparisons were empty. This is a fresh runtime check of unchanged binaries,
not a claim that the R10 consumer runs inside the rendered host.

Exact local launch pattern, from the RPGGame workspace (`checkout` below is
`build/rpg-r9/checkout`):

```powershell
$env:NODE_PATH='C:/Users/EJOHDAV/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules'
$env:RPG_SITE='C:/Users/EJOHDAV/source/repos/RPGGame/build/rpg-r9/wasm/rpg-site'
$env:EVIDENCE_DIR='C:/Users/EJOHDAV/source/repos/RPGGame/build/rpg-r10/webgpu'
$env:BROWSER_EXECUTABLE='C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe'
$env:RPG_BACKENDS='webgpu'
$env:RPG_HARDWARE='1'
node build/rpg-r9/checkout/scripts/rpg-host-smoke.cjs
# For WebGL2: RPG_BACKENDS=webgl2, RPG_HARDWARE unset, evidence directory webgl2.
python build/rpg-r9/checkout/scripts/rpg-native-smoke.py build/rpg-r9/native-ci4/build/rpg-host/Release/rpg_demo_windows.exe build/rpg-r10/d3d12
python build/rpg-r9/checkout/scripts/rpg-check-frames.py build/rpg-r10/d3d12
```

`node` and `python` above were the bundled Codex runtimes, not Windows Store
aliases. Reports/captures remain in ignored `build/rpg-r10/{webgpu,webgl2,d3d12}`.
The [new hosted rendered run](https://github.com/johdav999/WebEngine/actions/runs/40210781293)
completed: Windows Release and browser builds/controller tests passed; browser
WebGL2 passed. Software WebGPU again failed with `GPU recovery exhausted: Device
was destroyed.` Windows startup again exited `0xc0000005`, before capturing.
These reproduce the unresolved R9 gates; both jobs correctly remain red.
No rendered CI pass is inferred from a successful compile or controller test.

Skill synchronization verified all six existing packages: WebEngine, RPGGame,
CafeGame, EJOHDAV installed skill, Johan installed skill and Johan/CatGame, plus
WebEngine's `engine.md`. Existing project-specific and unrelated guidance was retained.
