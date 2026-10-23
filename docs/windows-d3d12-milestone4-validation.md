# Direct D3D12 milestone 4 validation

Milestone 4 adds skeletal GPU skinning and cached CPU morph vertex uploads to
the direct native renderer. Existing animation/layer/tree/IK/retargeting evaluation,
scene archives and browser runtime code are unchanged. See
[animated rendering](windows-animation-rendering.md) and
[backend scope](windows-d3d12.md).

## Native Windows

Validated 2026-10-22 on Windows x64 MSVC Release, Visual Studio 18 2026,
Windows SDK 10.0.26100.0, NVIDIA RTX 5060 Ti, SDL 3.4.18 and pinned DXC.
`WEBENGINE_D3D12_VALIDATION=1` enabled the actual D3D12 debug layer; no reported
ERROR/CORRUPTION messages occurred in the successful runs.

`scripts/windows-animation-smoke.ps1` ran the same imported binary FBX rig,
materials and two-clip scene through Dawn and direct D3D12. Its 37 matched cases
passed under `build/evidence/d3d12-m4/final-dual/{dawn,d3d12}/`, with PPM/PNG images,
per-case state, first-frame and warm-frame counters, verified JSON, executable/
SDL hashes and the imported fixture's SHA-256. Checks include:

- Opposing clips and exact symmetric blend restoration; independent players
  sharing one mesh; simultaneous morphing/skinning and negative/extrapolated
  weights; exact identity-skin/static-morph equivalence.
- Mirrored/nonuniform component scale with tangent normals, normal-map removal,
  alpha blend/additive/cutout materials, caster flags, animated sun/spot shadows
  and three directional cascades.
- Offscreen bind meshes brought into view through bone translation or morphing;
  culling on/off and the equivalent in-view bind scene produce matching pixels.
- Controller blend-tree endpoints match the individual clips; a weighted pose
  layer changes the rendered result. The renderer consumes the composed palette.
- HDR/fog/4x MSAA, odd 803x603 resize, two cameras, unchanged GUI colors,
  invalid-joint rejection and restoration, and static mesh replacement.
- A 1024-entry structured palette (128 KiB) with all four nonzero influences at
  indices 1020–1023. Distinct bone transforms include nonuniform scale. Color and
  shadow pixels match an independently CPU-baked conventional mesh exactly under
  the verifier's changed-pixel threshold.

Direct counters prove one palette upload for a changed player, one morph upload
for a changed morph pose, no uploads on unchanged/warm frames, and no multiplied
uploads across color/shadow/view passes. Static replacement retires all cached
skin and morph poses. Removed camera slots now release collected references/LOD
history, so obsolete views cannot keep those poses alive.

All 37 cross-renderer captures passed the mean absolute RGB-channel difference
limit of .75 on the 0–255 scale. The maximum measured mean difference was
.074006; per-image values are in `final-dual/comparison.json`. This is local
fixture agreement, not bit-identical shading or a performance/parity guarantee.

The retained `webengine_animation_windows` gallery was captured through both
backends and visually inspected at 1280x720. Its complete panel shows play, clip
switch/blend, pose and morph sliders, shadows/fog/HDR/MSAA and Quit. The world
shows two independently bent imported rigs and a morph-only object with shadows.
Captures are `build/evidence/d3d12-m4/gallery-{dawn,d3d12}.png`.

Fresh regression evidence:

- `static-regression/{dawn,d3d12}/`: the static material/lighting/shadow/MSAA suite
  passed, with maximum mean cross-renderer difference .213.
- `environment-regression/{dawn,d3d12}/`: all 36 sky/fog/HDR cases passed, with
  maximum mean difference .055163.
- `vehicle-regression/{dawn,d3d12}/`: vehicle/native GUI, text/input, resize,
  fullscreen, resolution scaling and framebuffer checks passed.
- CTest `animation`, `animation_layers`, `animation_control`, `animation_retarget`,
  `ik`, `morph`, `render_frame`, `desktop_api` and `desktop_api_d3d12`: 9/9 passed.

Those regression directories are below `build/evidence/d3d12-m4/`.
A configuration with Dawn OFF/D3D12 ON also built and ran the animation pixel suite
and `desktop_api_d3d12`, with evidence in `direct-only/d3d12/`. Both backends are
enabled in the normal handoff build. Direct-only builds do not link Dawn.

## Browser

The pinned repository-local Emscripten 6.0.11 Release target and web assets were
configured/built and published into fresh `build/d3d12-m4-browser-site`, served
on port 8350. `webengine.wasm` SHA-256 is
`EA33075374476F5AEFD8F3C0F52FA58D1090AF3E54AD76D2DDB08AC3F319EC9F`.
The browser C++/shader implementation was unchanged, so the hash matches the
previous build; these are fresh acceptance runs for milestone 4.

Desktop Chrome 155.0.8059.85 passed:

- `morph-smoke.cjs` with explicit `BACKEND=webgpu` and `webgl2`, asserting the
  acquired backend. FBX morph UI, combined/reset weights, morph-before-skin,
  identity palettes, independent instances, alpha/shadow rendering, animated bounds
  and scene replacement passed. WebGPU also retained zero warm uploads. WebGL2
  keeps its existing CPU deformation/per-draw vertex upload policy; no equivalent
  upload-efficiency claim is made there.
- `skeletal-animation-smoke.cjs` on explicit WebGPU: imported hierarchy/bindings,
  512-byte changed palettes, pose-only updates, blend endpoints, play/pause/stop,
  crossfade, final-pose hold, binary FBX equivalence, texture overrides, malformed
  import retention and resource/control cleanup. Stale status-text and unrelated
  playground helper-draw assertions were updated to check renderer readiness and
  the actual skeletal/color/shadow contracts.
- `animation-layers-smoke.cjs` on both forced WebGPU and WebGL2: visible composed
  layers, 1D/2D editing, masks/additive weights, preview isolation/pause/restart,
  library round trip, apply/undo/redo/reopen and compact layout.

Reports and images are under `browser-morph-{webgpu,webgl2}/`,
`browser-skeletal-animation-smoke/` and `browser-animation-layers-smoke/`, below
the same milestone evidence root. All successful reports have empty error arrays.

## Limits

No GPU clip evaluation, new animation API, dual-quaternion skinning, cloth,
ragdolls or new import format is introduced. Root-motion application, IK target
selection, retarget compatibility and game policy remain their existing portable
contracts. This native fixture validates clip/tree/layer palette rendering; it
does not independently exercise every IK/retargeting combination in direct pixels.
The shared IK/retargeting CPU tests passed separately.

Direct water/layered terrain, particles/ribbons, instancing/compressed uploads and
device recovery remain outside scope. HDR still presents an SDR swapchain.
Other GPU vendors, physical mobile devices, TDR recovery and sustained performance
were not tested. Desktop browser/emulation results are not physical mobile acceptance.
