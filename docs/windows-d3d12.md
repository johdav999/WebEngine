# Selectable native graphics backends

Direct D3D12 milestones 1–8 run the shared vehicle, retained native GUI and
material/lighting/environment/animation/terrain/water/FX scenes through Dawn or direct D3D12. Both use DesktopHost,
Application scheduling, SDL window/input/audio, Scene resources, vehicle physics,
camera and GuiSystem. There is no browser or Windows Forms layer in either path.

## Select and build

For explicit software-renderer diagnostics, set `WEBENGINE_D3D12_WARP=1` before
launching a direct-D3D12 host. It selects WARP even when hardware exists and reports
`adapter.software:true`; it is never an automatic fallback. Leave it unset for
hardware acceptance and normal play, and keep the selection unchanged throughout
the host lifetime. The RPG CI smoke verifies the reported selection. See
[library stabilization evidence](rpg-l1-validation.md).

```powershell
./scripts/setup-windows.ps1
./scripts/build-windows.ps1 # Both renderer implementations, Automatic default
./build/windows/Release/webengine_windows.exe --backend dawn
./build/windows/Release/webengine_windows.exe --backend d3d12 --fullscreen
./build/windows/Release/webengine_lighting_windows.exe --backend d3d12
./build/windows/Release/webengine_animation_windows.exe --backend d3d12
./build/windows/Release/webengine_terrain_windows.exe --backend d3d12
./build/windows/Release/webengine_water_windows.exe --backend d3d12
./build/windows/Release/webengine_fx_windows.exe --backend d3d12
# Optional: exclude Dawn from the native renderer dependency graph
./scripts/build-windows.ps1 -Backend D3D12 -BuildDir build/windows-direct
./build/windows-direct/Release/webengine_windows.exe --backend d3d12
```

`DesktopSettings::backend` defaults to Automatic. Dawn-enabled builds try
Dawn/D3D12 → Dawn/Vulkan → Dawn/D3D11; explicit selections stay strict. Set
`DesktopBackend::Direct3D12` to require the independent direct renderer.
`host.backend()` returns the current candidate; diagnostics report readiness and
failed attempts. See [fallback contracts](windows-graphics-fallback.md). Unknown or
compiled-out explicit choices fail clearly. Browser
`RenderBackendPreference` remains Automatic/WebGpu/WebGl2.

`WEBENGINE_WINDOWS_DAWN` and `WEBENGINE_WINDOWS_D3D12` default ON inside the opt-in
Windows build. At least one must be enabled. `-Backend Dawn` excludes direct D3D12;
`-Backend D3D12` excludes Dawn's package lookup and link dependency. The setup
script still stages all pinned SDKs. Windows x64 MSVC Release, Windows SDK and
Python remain required. DXC compiles HLSL into embedded shader binaries at build
time. Direct drawing does not translate WGSL or call Dawn. Keep the existing
staged fonts, assets and runtime layout. The direct-only build script builds the
vehicle, gallery and focused tests; pass `--backend d3d12` in a direct-only build.
The editor uses Automatic by default.

## Delivered rendering scope

| Contract | Dawn | Direct D3D12 milestone 8 |
| --- | --- | --- |
| Native vehicle physics, cameras, input, menus and text | Existing shared application | Same shared application |
| Static materials | Existing WebGPU renderer | Metallic/roughness PBR, five conventional maps, semantic mips, unlit/emission, alpha modes, mirrored/double-sided winding |
| Animated meshes | Existing WebGPU renderer | GPU linear skinning with four influences/up to 1024 palette entries; cached CPU morph vertices before skinning in color and shadows |
| Layered materials/terrain | Existing WebGPU renderer | Four albedos, RGBA weights, packed normal/height maps, height blending and world triplanar projection; shared terrain chunk/camera/shadow LOD |
| Water | Existing shared water settings | Gerstner waves/ripples, Fresnel/IBL, depth refraction/foam, planar opaque reflections, underwater fog/absorption/caustics, transient contact ripples/wakes |
| Particle FX | Existing shared FX | Billboards, velocity stretch, planes, flipbooks, ribbons/surface trails, mesh particles and soft opaque-depth fading |
| Lighting | Existing WebGPU renderer | Ambient, first eight directional/point/spot lights, baked diffuse/specular IBL and BRDF LUT |
| Shadows | Existing WebGPU renderer | Up to four directional/cascade/spot maps per view, shared planner, 1/9-tap PCF, caster/receiver flags, alpha cutouts and independent caster LOD |
| MSAA | Existing renderer | Hardware-checked 1x/4x scene color/depth, resolve before single-sample GUI |
| Sky | Existing shared settings | Gradient, sRGB panorama, atmosphere/clouds/day-night payloads; fullscreen or sphere; per-view opt-out |
| Fog | Existing shared settings | Analytic height/start-distance fog on World meshes and sky; additive extinction; orthographic origins |
| HDR post | Existing renderer | Checked RGBA16F scene targets, quarter-size bloom, exposure/saturation/contrast and SDR composite before GUI |
| Retained GUI | Shared WGSL GUI renderer | Same GuiDrawList/font atlas, native HLSL pipeline, clip rectangles and alpha |
| Views, masks, culling and camera LOD | Existing renderer | Shared RenderViews/RenderFrame collection |
| Resize/resolution budget | Existing renderer | Fence-safe swapchain/depth replacement, same resolution budget |
| Fullscreen | SDL borderless fullscreen | SDL borderless fullscreen |
| Capture | Existing asynchronous PPM readback | Fenced asynchronous PPM readback including GUI |
| Mesh instancing | Existing shared batches | Shared camera/shadow/reflection batches with per-instance transforms/tint/flags and cached object uploads |
| Compressed textures | Supported semantic variants | Queried semantic BC7 sRGB/linear variants, complete mip uploads and retained RGBA/GUI fallback |
| Device recovery | Existing bounded recovery | Bounded device recreation with retained scene/GUI/policy, capture cancellation and explicit retry |
| GPU profiling | Existing asynchronous pass summaries | Nested direct-queue timestamps, rolling inclusive/self statistics, draw counts and Perfetto/Chrome trace export |

See [detailed GPU profiling](windows-gpu-profiling.md) for timing boundaries,
controls, exports and resource lifetimes.

Milestone 8 extends recovery, batching and compressed uploads; it does not establish full renderer or
performance parity. Direct capabilities now report PBR, shadows, spotlights and
spot shadows available, with hardware-checked `msaa4x`. The existing vehicle menu
enables shadows and supported MSAA automatically. `hdrPostProcessing` reflects
FP16 render/blend/sample/load support. `instancing` is supported and `compressionBC`
reflects queried BC7 linear/sRGB support.
Invalid scene/view/texture and oversized FX candidates reject before GPU recording,
retaining a valid binding. See [recovery, batching and compressed uploads](windows-d3d12-recovery-performance.md)
for supported formats, bounded retry, resource lifetimes and remaining limits.

The static shader follows the existing glTF-style material slots: sRGB base and
emissive maps; linear metallic/roughness (B/G), normal (+Y tangent) and occlusion
(R). `Texture::mipChain` builds semantic mips; per-slot Repeat/Clamp and negative
UV scale follow the shared material settings. Baked Environment cube maps and
the BRDF lookup use linear float16 resources, intensity and Y rotation. PBR uses
the same GGX/Fresnel/geometry terms and standard Reinhard-to-sRGB response as Dawn;
unlit surfaces retain the existing direct display response. Conventional Blend
and Additive meshes use the portable camera-sorted FxFrame order. Dedicated quad/ribbon pipelines now consume that same sorted queue; see
[native FX](windows-fx-rendering.md). No scene archive or public material API changes.

Skeletal and morph draws use these same materials, including alpha cutouts,
normal maps, mirrored/double-sided winding, shadows, fog, HDR and MSAA. Shared
animation/layer/tree/IK/retargeting evaluation supplies ordinary pose palettes;
the renderer never samples clips or advances clocks. Bone and morph buffers upload
only on revision changes, with independent per-instance identities. Four-influence
linear skinning uses inverse-transpose bone normals and deformed tangent axes;
morphing precedes skinning in both color and shadow passes. See
[animated rendering](windows-animation-rendering.md) for APIs, lifetimes and limits.

Layered materials use the portable `MaterialLayers` contract on static, morphed and
skinned meshes, including `TerrainComponent` chunks. Four sRGB albedos, one linear
RGBA weight map and four linear RGB-normal/alpha-height maps use semantic mip
generation and independent Repeat/Clamp wrapping. Missing weights and zero weight
sums select layer zero. Missing albedos are white; missing relief maps produce
flat normals and zero height. Weighted color/alpha/roughness, active-layer height
blending, global/per-layer UV transforms and signed world triplanar projection
follow [terrain materials](terrain.md). Conventional normal/metallic-roughness/AO/
emissive maps are inactive while layering is enabled; scalar metallic/emissive,
tint, opacity, alpha modes, lighting/IBL, fog/HDR/MSAA remain composable. Cutout
shadows use the same layered alpha and deformed world projection as color.

The renderer consumes existing immutable terrain meshes and RenderFrame policies;
it does not generate terrain, paint weights, stream pages or alter simulation.
Camera and shadow LOD remain independent and support automatic projected-size
selection. Rendering does not change collision, ground queries or source data.
Material tables hold nine views, keyed by texture identities and layered semantic
roles. Layer and water settings use 560-byte object constants inside a 768-byte
upload slot aligned to 256 bytes; existing frame/upload/descriptor bounds apply.

Water reuses `MaterialShader::Water`, `WaterSettings`, `waterParameters`,
`waterReflection`, `underwaterView` and `prepareUnderwater`. Three bounded Gerstner
waves displace local XZ geometry after morphing/skinning and derive its tangent
basis; conventional alpha/winding/lighting/shadow settings remain composable.
The application supplies sufficiently subdivided geometry. Double-sided pipelines
preserve mirrored front-face orientation so underside lighting remains correct.

Per-view opaque World snapshots exclude water and transparencies. Color is averaged
and minimum depth selected at 4x MSAA; 1x loads both directly. Typed R32Float depth
snapshots support depth reconstruction, foreground/clear rejection, refraction,
absorption and shoreline foam. Viewport clamps prevent another camera's pixels from
entering the lookup. Planar reflection mirrors the camera, reverses winding and
clips non-water opaque/cutout World geometry to the selected plane, including
off-camera and animated sources. Fog integrates the reflected two-leg path.
Underwater selection copies the existing transformed rectangular footprint;
a completed World snapshot feeds bounded absorption/scattering and optional
procedural caustics before Overlay/GUI. All effects compose with HDR and 1x/4x MSAA.

`DesktopHost::setWaterInteractions(span)` validates and copies up to 16 transient
`WaterInteraction` values for both native renderers. Use Application simulation
seconds for `startTime`; water clocks now use `frame.simulation.seconds`, independently
of GUI presentation. Pause/suspension freeze waves and contact ages. Applications
own contact detection, emission and rebuilding inputs after scene changes. Invalid
snapshots preserve previous inputs; view edits/recovery retain them, and successful
scene handoff clears them. There are no node borrows or callbacks in the snapshot.
See [water contracts](water.md) for defaults, ranges and approximation limits.

Private `d3d12_water.inc` owns snapshot/reflection targets and shader passes. Size,
sample, format or effect changes fence old resources before replacement. Disabled
effects shrink scratch targets to 1x1 and skip their passes. Main depth is a typeless
R32 resource with typed D32 depth and R32 sampling views. Transitions order all
sampling/attachments; snapshots never read their current render target. At non-HDR
1x, water accumulates into an offscreen target and copies to the swapchain before
GUI; HDR uses its existing scene target, and 4x resolves after ordered views.
Underwater and snapshots use full-canvas coordinates with per-view scissors.

`planShadows` supplies fixed directional volumes, 2–4 sun cascades and spot cones,
sharing the existing four-map budget and light order. Point lights have no shadow
cube maps. Off-camera opaque/cutout casters, visibility masks, mirrored winding,
cast/receive flags, biases, finite coverage and independent shadow LOD retain
their shared contracts. See [lighting](lighting-post-processing.md) for planner
defaults and limitations. Every camera owns independent RenderFrame LOD history.

Sky and fog use the existing [sky](sky.md) and [fog](fog.md) contracts. Applications
publish settings through `Scene::setSky`/`setFog`; sky clocks advance only through
explicit application calls to `advanceSky`. Rendering never changes simulation or
day/night/cloud phase. Panorama color uses sRGB decoding with U repeat/V clamp.
Sky draws before Background geometry; fog applies to World geometry and sky,
never Background/Overlay geometry or GUI. `DesktopView::skyEnabled/fogEnabled`
independently control each camera, including views preserving earlier color.

`RenderQuality::post` remains off by default. With supported hardware it enables
linear RGBA16F scene rendering, optional quarter-size bloom extraction, then
exposure, Reinhard, saturation, contrast and sRGB output. These settings follow
[lighting/post-processing](lighting-post-processing.md), including zero-bloom pass
elision. All ordered views/layers share the HDR canvas and bloom may cross their
boundaries. GUI draws after composition. This is SDR output, not HDR10/display
color-space switching. HDR MSAA requires separate FP16 4x/resolve support; an
unsupported request retains the requested settings and reports effective 1x.

## Ownership, presentation and captures

The private DesktopGraphics interface contains no public API dependency. Dawn
owns WebGpuRenderer/SdlGpuHost; direct D3D12 owns its DXGI swapchain, device,
queue, PSOs, descriptors, resources and fences. DesktopHost retains window/GUI and
releases scene/view borrows before Application destroys scenes. GPU caches hold
weak CPU identities and retain retired GPU allocations until submission fences
complete. Frame contexts reuse mapped uploads/descriptors only after their fence.
The GUI builds one draw list per presentation. Simulation never reads graphics
quality, culling, interpolation, capture or fullscreen state.

Use `host.setWindowSize`, `host.setFullscreen`, F11 in the vehicle, or the existing
settings menu. Fullscreen is borderless desktop fullscreen, not exclusive display
mode switching. Resize preserves application/GUI state and uses physical display
density with shared resolution limits. Direct D3D12 currently uses two flip
buffers, vsynced presentation and conventional resource barriers. Requested 4x
MSAA requires both color/depth support; otherwise effective samples remain 1.
MSAA resolves all ordered views/layers before post, GUI and framebuffer capture.
Post enable/disable recreates compatible attachments only after queue completion.

`host.captureFrame(path)` queues one capture; duplicate requests reject without
replacing it. Step the host until `capturePending()` clears, then check
`captureError()`. PPM is top-down RGB at the actual render extent with the GUI.
Readback owns its buffer until the queue fence completes. Resize completes old
readback before reallocating; shutdown cancels pending work and releases all
scene borrows, including after graphics failure. Paths/directories remain caller
owned. Limits include 32 views, 40960 combined mesh/FX/shadow/reflection draws per frame,
4096 cached meshes and semantic texture representations, 65536 descriptors per
frame, 64 MiB frame uploads, 256 MiB per mesh buffer, texture mip upload or capture,
and bounded 10-second fence waits. These are
failure limits, not throughput guarantees or a total GPU memory budget.

`diagnostics().renderState` identifies `backend=d3d12`,
`desktopBackend=direct-d3d12`, adapter, extent, validation and milestone flags;
`performance` reports mesh/GUI/sky draws, post/bloom passes, triangles and cache counts.
Milestone 5 adds `layeredDrawCalls`, `colorTriangles`, `shadowTriangles`,
`cameraLodParts`, `shadowLodParts` and `shadowCulledParts`. Layered draws count color
and shadow submissions; color triangles exclude GUI. LOD counters come from the
shared per-view collectors, not terrain simulation.
Milestone 6 adds `waterDrawCalls`, `waterSnapshots`, `reflectionPasses`,
`reflectionDrawCalls`, `reflectionTriangles`, `underwaterPasses` and
`waterPresentCopies`. `colorTriangles` excludes reflection and shadow submissions.
Milestone 7 adds particle/mesh-particle counts, quad/ribbon draw counts, segment
counts and changed-stream upload bytes; see [FX contracts](windows-fx-rendering.md).
State reports `waterCapture`, `waterTargetWidth` and `reflectionTargetWidth`.
State also reports requested post controls, `effectivePost` and `hdrMsaa4x`. These counters
are submission counts; separate `gpu` and `gpuProfile` reports and
`gpuProfileTrace()` provide [GPU timings](windows-gpu-profiling.md).
Device loss now queues bounded
reacquisition; `retryGraphics()` resets exhausted recovery. Opted-in
`simulateGraphicsLoss()` removes the real device when supported. See
[recovery contracts](windows-d3d12-recovery-performance.md). Opt-in
`WEBENGINE_D3D12_VALIDATION=1` requires Windows Graphics Tools, enables the D3D12
debug layer and surfaces ERROR/CORRUPTION messages as host errors. It is off by
default; requesting an unavailable debug layer fails explicitly.

## Validation

```powershell
$env:WEBENGINE_D3D12_VALIDATION='1'
./scripts/windows-backend-smoke.ps1 -EvidenceDir build/evidence/windows-backends-new
./scripts/windows-lighting-smoke.ps1 -EvidenceDir build/evidence/windows-lighting-new
./scripts/windows-environment-smoke.ps1 -EvidenceDir build/evidence/windows-environment-new
./scripts/windows-animation-smoke.ps1 -EvidenceDir build/evidence/windows-animation-new
./scripts/windows-terrain-smoke.ps1 -EvidenceDir build/evidence/windows-terrain-new
./scripts/windows-water-smoke.ps1 -EvidenceDir build/evidence/windows-water-new
./scripts/windows-fx-smoke.ps1 -EvidenceDir build/evidence/windows-fx-new
./scripts/windows-performance-smoke.ps1 -EvidenceDir build/evidence/windows-performance-new
ctest --test-dir build/windows -C Release -R '^(desktop_api|desktop_api_d3d12|render_quality|adaptive_quality)$' --output-on-failure
```

The opt-in runner briefly shows the native fullscreen window; it is not a hidden
CTest. For a direct-only build pass `-Backend D3D12 -BuildDir build/windows-direct`.
It drives the shared vehicle, edits native GUI text, clicks Resume, resizes,
scales resolution, enters/exits fullscreen, captures each state, rejects duplicate
captures, cancels shutdown work and checks paused simulation. The Python verifier
checks dimensions, nonempty world pixels and changed GUI pixels, creates PNG
previews and records `verified.json`. API tests independently cover transitions,
camera removal, invalid candidates, reentry, callback exceptions and close.
Run fresh browser `vehicle-smoke.cjs` on WebGPU/WebGL2 plus `gui-smoke.cjs` with
each explicit renderer against the pinned Emscripten build. See the separate
[milestone 1 evidence](windows-d3d12-validation.md) and
[milestone 2 evidence](windows-d3d12-milestone2-validation.md) and
[milestone 3 evidence](windows-d3d12-milestone3-validation.md) and
[milestone 4 evidence](windows-d3d12-milestone4-validation.md) and
[milestone 5 evidence](windows-d3d12-milestone5-validation.md) and
[milestone 6 evidence](windows-d3d12-milestone6-validation.md) and
[milestone 7 evidence](windows-d3d12-milestone7-validation.md) and
[milestone 8 evidence](windows-d3d12-milestone8-validation.md).

The native `webengine_lighting_windows` gallery uses the same portable example
scene as lighting acceptance. Its retained GUI toggles shadows, MSAA, cascades,
lights, environment and normal mapping; a slider changes gold roughness.
A second panel controls sky mode/geometry/clouds, fog, HDR, bloom, grayscale
and exposure. Atmosphere/HDR/bloom start enabled in this example only. F11
toggles fullscreen and Escape exits. `--backend dawn|d3d12` selects rendering;
`--capture image.ppm` captures the gallery with GUI and exits. The capture's
parent directory must exist. The lighting smoke isolates map/lighting changes,
cast/receive flags, alpha modes, mirrored/backface winding, budgets, cascades,
split views and MSAA resize/restore. Reports retain per-case graphics state and
counters; its standard-library verifier writes PNGs and checks changed pixels.

`webengine_terrain_windows` uses portable `examples/terrain_gallery.hpp` content,
with retained controls for layer isolation, normal/height strength, world projection,
camera/shadow detail, overhead view, shadows/fog/HDR/MSAA. HDR starts on in this
example only. F11 toggles fullscreen, Escape exits, and `--capture image.ppm`
captures the paused gallery including GUI and exits (parent directory must exist).
`windows-terrain-smoke.ps1` checks 46 captures per backend, cutout/animated layers,
fallbacks, independent LOD submission counts, mixed-neighbor coverage, unchanged
ground queries, resize, split views and HDR. Run `terrain-surface-smoke.cjs` against
a fresh browser build for explicit WebGPU/WebGL2 surface/LOD/archive/recovery tests.

`webengine_water_windows --backend dawn|d3d12` adds retained wave/refraction/foam/
reflection/underwater/caustic/contact controls, pause, fog, HDR and MSAA. The example
owns the moving object's wake emission; no physics feature is added. F11 toggles
fullscreen, Escape exits and `--capture image.ppm` captures a paused GUI frame
and exits (parent must exist). `windows-water-smoke.ps1` verifies 63 matched
captures per backend, including transformed/animated surfaces, masks, optional
targets/pass costs, invalid-state retention, pause, resize, split views and contact
clearing across scene handoff. Run all six `water-*-smoke.cjs` browser suites
against a fresh build on WebGPU/WebGL2, including archive and recovery coverage.
