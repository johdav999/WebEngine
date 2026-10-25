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

Local Emscripten Release compilation and `rpg_rulesets` passed. Full matrix and
rendered rechecks are recorded below as they complete. Local MSVC is unavailable;
native Windows compilation is performed in the workflow, not claimed locally.
The first sandbox link attempt stalled; retry uses access to the local SDK cache.

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
