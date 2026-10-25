# Library L1 — release stabilization

Work in progress against `227b08d`. API 1.7.0 and wire 1 remain unchanged.
This milestone addresses the two R10 rendered CI failures; no RPGGame pin changes.

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
Validation of these corrections is pending; earlier WARP runs crashed inside
`d3d10warp.dll`, after host/controller construction, with no reported validation
error. Adapter enumeration alone did not resolve that crash.

Scene-only isolation reproduces WARP's execution crash while GUI-only rendering
passes. Individual (non-instanced) draws reproduce the same crash, ruling out
instancing as the cause. A trial bounded descriptor binding did not resolve it
and was removed; the original object-buffer binding/lifetime is retained. Shared
sample meshes and native performance assertions exercise multi-instance rendering.
An ordinary unlit pixel-shader specialization is undergoing platform validation;
it preserves alpha, reflection clipping, emission and fog while excluding the
unused lighting/water graph. Browser pipelines already specialize materials.

Native WARP and browser SwiftShader checks establish software-renderer acceptance,
not physical GPU or performance coverage. Local hardware checks remain separate.
Results and the final release gate decision will be recorded after validation.
