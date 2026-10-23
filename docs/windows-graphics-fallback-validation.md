# Windows graphics fallback validation — 2026-10-22

The shared checkout contained concurrent direct-renderer and editor/input work;
it was preserved. Runtime acceptance used an independent `build/windows-fallback`
configuration with Dawn ON/direct D3D12 OFF. The normal both-renderer configuration
in `build/windows-services` also compiled `desktop_fallback_tests` successfully.
No SDK/driver installation, version upgrade or global DLL search-path change was
needed. Native and browser results below are separate runs.

## Windows

Visual Studio 18 2026 x64 Release, pinned SDL3/Dawn/DXC and Windows SDK. Real GPU:
NVIDIA RTX 5060 Ti; D3D12/D3D11 driver 32.0.16.1088 and Vulkan driver 610.88.0.0.
Build/device access used approved host execution.

- CTest **5/5** passed: `desktop_fallback`, `desktop_api`, `desktop_api_automatic`,
  `desktop_api_vulkan`, `desktop_api_d3d11`. This includes synchronous/async failure,
  release-before-replacement, scoped camera re-resolution, retained quality/shadow/
  water inputs, readback cancellation, monotonic frame totals, exhaustion and retry.
  Public host regressions cover source transitions/unload, pause/resume, removed
  cameras, callback reentry/exceptions, shutdown and fault-injection opt-in.
- `windows_fallback_smoke` passed real **Dawn D3D12 → Vulkan → D3D11** replacement
  for the shared feature gallery. The same source scene, camera handles, skinned
  pose, terrain, water/reflection/refraction/foam, particles, shadows, lighting/HDR
  post and retained GUI rendered on all three APIs. There is no scene clone/import
  or alternate gameplay implementation during the handoff.
- Source archive bytes and elapsed simulation totals stayed equal. A 1000-second
  host timestamp jump per handoff did not become presentation catch-up. A submitted
  GPU readback was cancelled before its late callback could publish. GUI text/input
  remained usable. Three terminal diagnostic failures exhausted the chain; explicit
  retry returned to the preferred backend and kept the source.
- `verify_windows_fallback.py` compared actual 1280×800 readbacks. Vulkan mean
  channel difference from D3D12 was **0.00000488**, D3D11 **0.00013574**, on a
  0–255 channel scale; neither alternate image had a pixel differing by >12 in
  any channel. The Vulkan gallery was also visually inspected. D3D11 correctly
  reported timestamp queries unavailable rather than assuming D3D12 capabilities.
- `windows-backend-smoke.ps1 -Backend Vulkan -BuildDir build/windows-fallback`
  passed vehicle driving, native SDL text/mouse menus, resize, resolution budgets,
  borderless fullscreen/restore, capture cancellation and framebuffer checks.

Evidence: `build/evidence/windows-fallback/report.json`, `pixels.json` and three
PPM/PNG captures; `build/evidence/windows-fallback-vehicle/vulkan/` contains the
separate vehicle report, hashes and framebuffer checks. CTest logs remain in
`build/windows-fallback/Testing/Temporary/`.

The failures that advance the real native devices were injected with the explicit
diagnostic API. This proves the handoff and GPU resource/callback path, not a real
driver crash, physical GPU removal, missing-runtime machine, multivendor/older-GPU
coverage, performance comparison or clean-VM install. The direct-only configuration
was not newly runtime-tested here; it deliberately has no alternate API candidate.

## Browser

The current `webengine` target rebuilt with repository-local **Emscripten 6.0.11**
and a repository-local temporary directory. Chrome **155.0.8059.85** passed
`scripts/render-fallback-smoke.cjs` against the current host/Wasm.

Checks covered forced WebGL2 real pixels and native text/GUI; absent WebGPU API,
null adapter, acquisition timeout and rejected device falling back to WebGL2;
late completion unable to revive WebGPU; normal WebGPU preference; real WebGL
context loss/restoration with identical paused pixels/clocks; animation/morph
changes, particles/flipbooks/ordering and split/inset views. Explicit WebGPU-only
and both-backends-unavailable cases failed clearly. No browser page errors were
reported. The suite's obsolete “WebGL2 shadows disabled” assertions were corrected
to the current directional-shadow capability; no browser renderer policy changed.

Evidence: `build/evidence/windows-fallback-browser/results.json` plus screenshots.
Desktop Chrome does not establish physical mobile or other-browser acceptance.

See [API, build and reproduction commands](windows-graphics-fallback.md).
