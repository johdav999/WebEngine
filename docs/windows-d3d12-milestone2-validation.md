# Direct D3D12 milestone 2 validation

Milestone 2 adds static conventional materials and lighting to the selectable
native backend: PBR/IBL, five semantic material maps/mips, point/spot lights,
directional/cascaded/spot shadows, alpha/winding rules and checked 4x MSAA.
The public scene/material/quality interfaces and archive format are unchanged.
See [scope and reproduction](windows-d3d12.md); milestone 1 evidence is historical.

## Native Windows

Validated on 2026-10-22, Windows x64 MSVC Release, Visual Studio 18 2026,
Windows SDK 10.0.26100.0, hardware NVIDIA RTX 5060 Ti, SDL 3.4.18 and pinned DXC.
`WEBENGINE_D3D12_VALIDATION=1` enabled the actual D3D12 debug layer. Final native
lighting evidence is `build/evidence/d3d12-m2/final-dual/{dawn,d3d12}/`, with per-case
JSON state/counters, PPM captures, PNG previews, pixel verification and binary hashes.

The same portable gallery ran through both renderers. Pixel comparisons isolate
roughness, tangent normals, metallic/roughness, occlusion, emission, environment
intensity/rotation, point/spot lighting, caster/receiver flags, alpha cutouts and
blended meshes. Winding tests require the same unlit surface after mirroring and
after enabling double-sided backfaces; single-sided backfaces disappear. Three
sun cascades leave one spotlight map; zero-map and shadows-off captures agree.
Split-view MSAA submits eight shadow passes (four per view). Actual 4x captures,
800x600 resize, 1x switching and restored 960x640 pixels pass. Restoring state
restores baseline pixels, including after rejecting unsupported water content.
Direct captures have no reported debug ERROR/CORRUPTION messages.

Thirty matched captures passed the cross-backend comparison threshold (mean
absolute RGB channel difference below .75 on the 0–255 scale). The largest mean
difference was .213; the baseline mean was .00261. This establishes close local
agreement for these fixtures, not bit-identical output or full renderer parity.
The adjacent `comparison.json` retains per-capture differences.

`webengine_lighting_windows --backend d3d12 --capture ...` additionally rendered
the actual retained GUI, bundled font and static scene together; the PNG preview
was visually inspected. The vehicle/backend suite separately covers driving,
native text/mouse GUI, resolution budgets, borderless fullscreen, captures and
shutdown cancellation on Dawn and direct D3D12. Focused CTest runs `desktop_api`,
`desktop_api_d3d12`, `render_quality` and `adaptive_quality`; public lifecycle,
invalid candidates, transitions, camera removal and pause contracts pass.

A separate configuration disabled `WEBENGINE_WINDOWS_DAWN` and enabled only
`WEBENGINE_WINDOWS_D3D12`; its lighting gallery/pixel checks and direct API/quality
tests passed. Configuration and project dependency evidence are retained in
`build/evidence/d3d12-m2/direct-only/`. This verifies Dawn is absent from the native
link graph; installed SDKs were not removed. The normal dual configuration was
restored and rebuilt for handoff.

## Browser

Repository-local Emscripten 6.0.11 reconfigured/built `webengine` and `web_assets`,
then dist was frozen into `build/d3d12-m2-browser-site`, served on port 8348.
No browser renderer source changed; the published Wasm remains SHA256
`EA33075374476F5AEFD8F3C0F52FA58D1090AF3E54AD76D2DDB08AC3F319EC9F`.
Chrome ran the following current-turn regressions:

- `material-smoke.cjs`: WebGPU material editing, alpha modes, tangents, maps, IBL,
  live edits/restoration and resource checks. Evidence: `browser-material/results.json`.
- `lighting-post-smoke.cjs`: explicitly forced WebGPU and WebGL2; spot shadows,
  cascades, quality validation, HDR regressions, MSAA/resize and recovery.
  Evidence: `browser-lighting/results.json`.
- `msaa-smoke.cjs`: explicitly forced WebGPU and WebGL2; real 1x/4x pixels,
  composition, resize and recovery. Evidence: `browser-msaa/results.json`.

All browser evidence paths above are beneath `build/evidence/d3d12-m2/`, with
adjacent screenshots. All suites passed. WebGL2 retains its existing basic-lighting
scope; this change does not add browser PBR/IBL to that fallback.

## Corrections and limits

The debug layer caught an initial attempt to copy descriptors out of a
shader-visible heap; material SRVs now are created directly in their frame-owned
table. Capture comparisons also identified the missing standard Reinhard display
mapping, now matched to the existing WGSL material response. Passing evidence
uses the corrected renderer. These are correctness checks, not a speed benchmark.

No full renderer parity, sustained performance, second GPU/vendor, WARP, physical
mobile, exclusive fullscreen, HDR display, multi-monitor/DPI matrix, device-loss
recovery or installer acceptance is established here. Sky/fog, skin/morph, terrain
layers, water, all particles/ribbons, instancing, compressed uploads, HDR post and
GPU profiling remain outside the direct milestone. Native automation uses actual
windows and synthetic input; it does not replace physical-device/human acceptance.
