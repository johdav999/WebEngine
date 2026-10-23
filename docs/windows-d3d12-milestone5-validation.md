# Direct D3D12 milestone 5 validation

Milestone 5 adds the existing four-layer material contract to direct D3D12,
including terrain chunks, normal/height blending and world triplanar projection.
Shared material/terrain APIs, simulation, collision, paging and archive formats
are unchanged. See [backend scope](windows-d3d12.md) and [terrain](terrain.md).

## Native Windows

Validated 2026-10-22 on Windows x64 MSVC Release, Visual Studio 18 2026,
Windows SDK 10.0.26100.0, NVIDIA RTX 5060 Ti, SDL 3.4.18 and pinned DXC.
`WEBENGINE_D3D12_VALIDATION=1` enabled the actual debug layer. Successful direct
capture runs reported no ERROR/CORRUPTION messages.

`windows_terrain_smoke` and `tools/verify_windows_terrain.py` passed 46 captures
per backend under `build/evidence/d3d12-m5/{dawn,d3d12}/`. Checks include:

- Four isolated layers, normalized mixtures, missing/zero weight fallback, white
  albedo/flat-normal/zero-height defaults, normal relief and height bias/blending.
- World triplanar projection, transformed/negative UVs, independent color/detail
  wrapping, weighted roughness, unlit mode, conventional-map exclusion and semantic
  cache separation when the same texture occupies different material roles.
- Mirrored/nonuniform scale, blend/additive/masked materials, layered cutout sun/
  spot shadows, caster flags and three directional cascades.
- Imported skeletal animation plus morphing with layered materials and cutouts.
- Independent camera/shadow LOD submissions, automatic projected-size selection,
  matching full/coarse/mixed-neighbor overhead coverage and unchanged ground queries.
- Invalid material/texture rejection preserving valid state, unchanged GUI colors,
  fog/HDR/4x MSAA, odd 803x603 resize, two views and restoration.

The two backends passed paired-image comparisons with maximum mean absolute
RGB-channel difference **0.239491**, below the .75 limit on the 0–255 scale.
`comparison.json` records each image. This is fixture agreement, not bit-identical
shading or a throughput/full-parity guarantee. Dawn acceptance waits for a fresh
periodic metrics snapshot before asserting submission counts.

A separate Dawn OFF/D3D12 ON configuration in `build/windows-direct-m5` built
the terrain gallery, acceptance executable and desktop API tests. Its terrain
suite also passed all 46 captures at `direct-only/d3d12/`. Native executable,
SDL and imported-fixture SHA-256 identities are recorded in `binary-hashes.json`.
The normal `build/windows` configuration retains both backends enabled.

The new retained `webengine_terrain_windows` gallery was captured at 1280x720.
Both captures were visually inspected: all controls fit, with the layered
terrain, sphere and pillar visible beside the panel. Images are
`build/evidence/d3d12-m5/gallery-{dawn,d3d12}.png`.

Fresh native regressions under the same milestone evidence root passed:

- `lighting-regression/{dawn,d3d12}/`: conventional maps, material/lighting/shadow
  changes, transparency/winding and MSAA; maximum paired mean difference .212482.
- `environment-regression/{dawn,d3d12}/`: all 36 sky/fog/HDR cases; maximum paired
  mean difference .055163.
- `animation-regression/{dawn,d3d12}/`: all 37 skin/morph cases, including changed
  revision uploads, animated bounds, layers/trees, cutouts and palette limits;
  maximum paired mean difference .074006.
- `backend-regression/{dawn,d3d12}/`: vehicle physics, retained native GUI/text and
  pointer input, resize, resolution scaling, actual borderless fullscreen and captures.
- CTest `terrain`, `terrain_page_edit`, `material`, `render_frame`, `desktop_api`
  and `desktop_api_d3d12`: 6/6 passed. The separate Dawn-free configuration also
  passed `desktop_api_d3d12` (1/1). Logs are `build/d3d12-m5-ctest.log` and
  `build/d3d12-m5-direct-ctest.log`.

## Browser

The pinned repository-local Emscripten 6.0.11 Release browser target and web assets
built successfully. A frozen copy in `build/d3d12-m5-browser-site` was served on
port 8351. The served Wasm bytes matched the frozen build; SHA-256:
`4642BFA2FAEC41C627E1F20832BFFD087B955F4ACA96434D94C459B548F677BD`.
This checkout includes concurrent editor/input work; the hash identifies the
actual tested artifact, not a claim that all unrelated changes were reviewed here.

Desktop Chrome 155.0.8059.85 passed `scripts/terrain-surface-smoke.cjs` on explicitly
acquired WebGPU and WebGL2. Evidence is in
`build/evidence/d3d12-m5/browser-terrain-surface-smoke/result.json` and PNG captures.
The successful report has an empty error array. Both backends verified:

- Visible normal, height and triplanar changes, isolated layers, zero fallback,
  invalid edit retention and the surface controls.
- Independent camera/shadow detail, actual triangle reductions, automatic LOD,
  full/coarse/mixed coverage, ground/collision invariance and resize.
- Layered mixed-LOD save/load, restored detail settings, malformed archive retention,
  paused device/context recovery, reset/return/restart and user-facing controls.

The smoke's stale exact-v23 archive assertion now accepts versions at least 23
while still testing round trips and malformed-file preservation. Its outdated
WebGL2-no-shadows assertion now checks current shadow submissions and enabled UI.
WebGL2 still uses basic lighting without PBR/IBL; this work does not change that.

## Limits

No new terrain generation, sculpt/paint, streaming or collision feature is added.
Layer height is shading only, without parallax or displacement. World projection
adds texture samples and is application-selected. Existing upload/descriptor/draw
bounds remain failure limits, not a global memory budget or performance guarantee.

Direct water, particles/ribbons, GPU instancing, compressed uploads and device
recovery remain outside scope. HDR scene rendering still presents an SDR swapchain.
Other GPU vendors, physical mobile devices, TDR recovery and sustained performance
were not tested. Browser/native desktop acceptance does not establish those results.
