# Library L1 — release stabilization

Status: **L1 complete**, 2026-10-25. Validated implementation:
`c7db4b446afe4b7ae92b3122c7b3b5c89f7365e3`, based on R10 `227b08d`.
API 1.7.0 and wire 1 remain unchanged. Both R10 rendered CI failures are resolved
for the neutral RPG sample. RPGGame's pin remains `56080eca124c9a62e15b146799a8004bdad5a9b2`.

## Release evidence

| Gate | Result | Evidence |
| --- | --- | --- |
| RPG library, Linux/Windows Release/Wasm | 16/16 tests on each platform | [RPG workflow](https://github.com/johdav999/WebEngine/actions/runs/40220603506) |
| Portable SDK, omissions and independent composition | Linux/Windows/Wasm passed | [SDK workflow](https://github.com/johdav999/WebEngine/actions/runs/40220603520) |
| Windows rendered host | Release build, controller test, WARP smoke and captured pixels passed | [Rendered workflow](https://github.com/johdav999/WebEngine/actions/runs/40220603552) |
| Browser rendered hosts | WebGPU under Xvfb and headless WebGL2 passed | Same rendered workflow |
| Local Windows hardware | Same CI binary, Intel Iris Xe, CPU validation, 40 frames, input/save/load/shutdown passed | `build/rpg-l1/native-final-hardware` in the RPGGame workspace |

CI uses Windows Server 2022, Ubuntu 24.04, Emscripten 6.0.11 and Chromium
151.0.7922.34. WARP reports Microsoft Basic Render Driver and `software:true`,
with CPU/GPU validation enabled. The sample submits 50 mesh instances in one draw.
Its before/after captures are 1100x760 with 98 colors and 337814/326989 visible pixels.
WebGPU reports the Google SwiftShader fallback adapter. Both browser backends
report eight quantized colors and 349636 visible pixels and complete the scenario.
The native hardware captures are 1099x759 with 103/104 colors and
293175/297970 visible pixels; hardware reports `software:false` and no recovery attempts.

Native artifact `12609024401` was downloaded and SHA-256 verified before the
local run: `85137b6a4d8d17161e0eec4906eb4e00d67a06dd712b54142122da07adbf8ce1`.
The local run uses the inbox hardware driver, not the experimental WARP package.
Reproduction commands and environment controls are in [host acceptance](rpg-hosts.md).

## Changes and failure isolation

Direct D3D12 now checks adapter-enumeration HRESULTs and returned interfaces before
use. `WEBENGINE_D3D12_WARP=1` explicitly selects Microsoft's software adapter for
diagnostic/CI rendering. It is not a fallback: ordinary launches still require
hardware. Adapter JSON reports `software:true` for WARP. Recovery recreates the
selected mode; do not change this environment setting during a host's lifetime.

Linux software browser tests explicitly initialize SwiftShader Vulkan and Vulkan
compositing, with ANGLE SwiftShader and `VulkanFromANGLE`. WebGPU runs full Chromium
in a windowed Xvfb session (`RPG_HEADED=1`); WebGL2 also passes headlessly. The former
configuration acquired a WebGPU device but Chromium could not allocate the shared
image for its canvas (`SharedImageBackingFactory` / `SharedImageStub` errors).
Headless Chromium subsequently kept the device alive but captured a black canvas;
windowed Xvfb resolves that presentation failure. Run [40219447124](https://github.com/johdav999/WebEngine/actions/runs/40219447124),
source `f9c6c40`, passes both browser scenarios: WebGPU and WebGL2 each report eight
quantized colors and 349636 visible pixels, plus complete input/save/load checks.
The normal GPU watchdog remains enabled. The harness retains its five-minute
deadline, pixel checks and complete input/save/load scenario, and records GPU
errors/loss. See [Chromium's headless Vulkan guidance](https://developer.chrome.google.cn/blog/supercharge-web-ai-testing?hl=en).

Native pipeline descriptions initialize valid comparison, stencil and blend
enums even for disabled states, following the [Direct3D defaults](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/ns-d3d12-d3d12_depth_stencil_desc).
Debug-layer errors are checked before submission as well as during servicing.
Earlier WARP runs crashed inside
`d3d10warp.dll`, after host/controller construction, with no reported validation
error. Adapter enumeration alone did not resolve that crash.

Scene-only isolation reproduces WARP's execution crash while GUI-only rendering
passes. Individual (non-instanced) draws reproduce the same crash, ruling out
instancing as the cause. A trial bounded descriptor binding did not resolve it
and was removed; the original object-buffer binding/lifetime is retained. Shared
sample meshes and native performance assertions exercise multi-instance rendering.
The non-layered unlit pixel-shader specialization resolves the sample crash;
it preserves alpha, reflection clipping, emission and fog while excluding the
unused lighting/water graph. Browser pipelines already specialize materials.

## Scope and remaining limitations

L1 closes the RPG library's named release gates. The general engine workflow still
has the existing `vehicle` and `spotlight` failures (94/96 core tests pass), also
recorded in [R1 baseline evidence](rpg-validation.md). See the [current general run](https://github.com/johdav999/WebEngine/actions/runs/40220603476).
No full-engine green-suite claim is made; those systems were not changed.

WARP/SwiftShader acceptance covers the neutral sample, not every lighting, layered,
shadow or water material on software drivers. The combined shader's broader WARP
behavior is not certified by this unlit-material fix. Hardware evidence is limited
to the recorded Intel adapter; no mobile, other hardware-vendor, sustained
performance or real driver-reset acceptance is inferred. Input is scripted through
the browser/native event paths. Adoption and game-specific content remain a separate
Game R1/G1 task; no consumer pin, gameplay policy or save format changed.

Installed engine skills and available WebEngine/RPGGame/CafeGame/CatGame copies
were synchronized while preserving unrelated project guidance.
