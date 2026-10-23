# Direct D3D12 milestone 6 validation

Milestone 6 implements existing water presentation on the direct native backend:
surface waves/ripples, depth refraction/foam, planar reflections, underwater fog/
absorption/caustics and transient contact ripples/wakes. Shared water settings,
simulation, contact detection and archive formats remain unchanged. See
[water](water.md) and [backend scope](windows-d3d12.md).

## Native Windows

Validated 2026-10-22 on Windows x64 MSVC Release, Visual Studio 18 2026,
Windows SDK 10.0.26100.0, NVIDIA RTX 5060 Ti, SDL 3.4.18 and pinned DXC.
All 17 HLSL entry points compile with DXC `-WX`. The actual D3D12 debug layer was
enabled with `WEBENGINE_D3D12_VALIDATION=1`; successful capture runs reported no
ERROR/CORRUPTION messages.

`windows_water_smoke` and `tools/verify_windows_water.py` capture/verify 63 cases
per renderer under `build/evidence/d3d12-m6/{dawn,d3d12}/`. The same suite runs
from the separate Dawn OFF/D3D12 ON build in `build/windows-direct-m6`, with
evidence under `direct-only/d3d12/`. Native PPM/PNG captures, per-case graphics/
performance state, verified JSON and executable/SDL/FBX SHA-256 identities remain
in those directories. Checks cover:

- Isolated refraction, normal ripples, distortion, absorption, foam width/color,
  Fresnel/IBL, environment rotation, shadows, alpha/additive/masked water and
  translated/rotated/mirrored/nonuniform placement.
- Nearby opaque/cutout planar reflections, changed reflection sources, foreground
  rejection, clear depth, reflected height fog, orthographic and ordered views.
- Underwater fog/absorption/scattering/caustics, distance limits, transformed
  footprints, masks and footprint exits, perspective/orthographic rays.
- Rings/wakes, wake direction, expired/future/elevated/masked inputs, invalid snapshot
  retention, simulation pause/resume, stable paused pixels, view retention and
  clearing contacts on scene handoff and return.
- Imported skin/morph animation in reflection and water materials; 1x/4x MSAA,
  HDR/fog/4x underwater, odd 803x603 resize, split cameras and restoration.
- Optional pass counts and scratch-target shrinkage, unchanged GUI colors and
  invalid water setting retention.

All paired captures pass the .75 mean absolute RGB-channel difference limit on
the 0–255 scale. Maximum observed mean difference is **0.075976**; per-image values
are in `comparison.json`. This is local fixture agreement, not bit-identical
shading, full renderer parity or a throughput comparison.

These tests found and fixed two native lifetime/clock issues: water had followed
the presentation clock, which advances while simulation is paused; native Dawn
scene replacement had retained contact inputs. The host now supplies simulation
time separately, and successful Dawn/direct scene handoff clears contacts. The
mirrored surface case also exposed front-face orientation being lost on direct
double-sided PSOs; culling and front-face selection are now independent.

The retained `webengine_water_windows` gallery was captured through both backends
at 1280x720. Its controls include waves, refraction, foam, planar reflection,
underwater camera/fog/caustics, moving-object wake/contact emission, pause, height
fog, HDR and MSAA. Both galleries were visually inspected with all controls
visible beside the scene. Captures are `gallery-{dawn,d3d12}.png` under the same
evidence root.

Focused CTest `water`, `material`, `render_frame`, `desktop_api` and
`desktop_api_d3d12` passed (5/5). The Dawn-free build separately passed
`desktop_api_d3d12` (1/1). Logs are `build/d3d12-m6-ctest.log` and
`build/d3d12-m6-direct-ctest.log`. The normal build retains Dawn and direct D3D12
enabled; the separate direct build has no Dawn renderer dependency.

Fresh rendering regressions under the same evidence root passed:

- `terrain-regression/{dawn,d3d12}/`: 46 layered terrain/LOD cases per renderer;
  maximum paired mean channel difference .019223.
- `lighting-regression/{dawn,d3d12}/`: material maps, lighting/shadows, alpha/winding,
  MSAA and rejection of still-unsupported particles; maximum paired mean .004364.
- `environment-regression/{dawn,d3d12}/`: 36 sky/fog/HDR cases per renderer;
  maximum paired mean .055163.
- `animation-regression/{dawn,d3d12}/`: 37 skin/morph cases per renderer, including
  bounds, layered/tree palettes and revision uploads; maximum paired mean .002018.
- `backend-regression/{dawn,d3d12}/`: vehicle physics, retained GUI/text/pointer
  input, resize, resolution scaling, actual borderless fullscreen and captures.

## Browser

The pinned repository-local Emscripten 6.0.11 Release target and web assets built
successfully. `build/d3d12-m6-browser-site` is a frozen site served on port 8352.
The served Wasm bytes match that build; SHA-256:
`4248BCB5A23D436EC71C094CEE8D4F37F8B3C4699B93D01D669B6EBDCB0F5412`.
This shared checkout includes concurrent editor/input/fallback work. The hash
identifies the actual tested artifact; this task does not claim independent
acceptance of every concurrent feature.

Desktop Chrome 155.0.8059.85 passed all six suites on explicitly acquired WebGPU
and WebGL2, each with an empty error array:

- `water-smoke.cjs`: waves/ripples/reflections, pause, validation, alpha/winding,
  MSAA, ordered views, resize, recovery, controls and archive round trip.
- `water-depth-smoke.cjs`: refraction/distortion/absorption, isolated foam,
  foreground rejection, 1x/4x capture, orthographic views and optional targets.
- `water-planar-smoke.cjs`: reflected-object pixels, below-plane clipping, optional
  pass/storage, MSAA, ordered views, resize, recovery and archive round trip.
- `water-underwater-smoke.cjs`: depth-varying fog/absorption, ray limits, waterline/
  footprint exits, views, Overlay isolation, resize, recovery and archive round trip.
- `water-caustics-smoke.cjs`: animated receiver lighting, independent settings,
  footprints, clock/pause, views, recovery and archives.
- `water-interactions-smoke.cjs`: independent ring/wake pixels, propagation and
  direction, input validation/filtering, views, pause, resize, recovery and clearing
  transient sources on scene replacement.

Reports and PNGs are in `build/evidence/d3d12-m6/browser-water-*-smoke/`.
Stale exact-v29/v31 archive assertions were changed to minimum supported versions,
while keeping real load/round-trip and pixel checks. Browser C++/water shader
implementation remains unchanged; WebGL2 retains its existing lighting fidelity.

## Limits

Water is presentation shading: no fluid simulation, buoyancy, collision, new mesh
generation, physical refraction, volumetric scattering or automatic contact
detection is added. Rectangular underwater footprints and the first selected
reflection plane retain existing approximation limits. Reflection captures exclude
transparent/particle sources. Existing upload/draw/descriptor limits include
reflection submissions and remain failure bounds, not a total GPU memory budget.

Native water acceptance uses explicit Dawn/D3D12. Other native Dawn APIs and
automatic fallback have separate contracts/evidence. This suite does not force a
native Dawn device loss; browser WebGPU/WebGL2 recovery is exercised. Direct device
recovery still requires host restart. Particles/ribbons, GPU instancing and
compressed direct uploads remain outside scope. HDR presents an SDR swapchain.
Other GPU vendors, physical mobile devices, driver/TDR recovery and sustained
performance were not tested.
