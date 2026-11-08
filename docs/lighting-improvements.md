# Light selection, filmic highlights and baked lighting zones

## Light and shadow budgets

Enable `RenderQuality::rankedLights` to select up to eight lights per view. The
renderer considers the enabled lights collected for the camera mask. It ranks
visible contributions by `LightComponent::priority` first (an integer from −100
to 100, default zero), then by brightness and range relative to the camera. Local
lights outside the view receive a lower score. Previously selected lights get a
small score boost to limit slot changes during camera motion. The editor exposes
**Priority** in the Light properties. Archived lights retain it from v38 onward.

`RenderQuality::localShadowReserve` sets how many of the existing zero to four
maps may be reserved for selected shadow-casting spotlights. Unused reservations
return to the shared budget. For example, set `maxShadowMaps=4`,
`directionalCascades=2` and `localShadowReserve=2` to fit two sun cascades and two
headlights. Point lights still have no shadow maps. Ranking defaults off and the
reservation defaults to zero, preserving scene-order behavior. Selection history
and the map assignment are presentation state. Performance reports expose
`lightingSlots` for the first canvas view, with eligible count, selected handles
and map-to-slot assignments.

Open `?scene=lighting` and choose **Light budget**. On Windows, launch
`webengine_lighting_windows.exe --backend dawn --ranked-lighting` or use
`--backend d3d12` for the direct renderer.

## Filmic highlights

`PostProcessing::toneMapper` defaults to `ToneMapper::Reinhard`. Select
`ToneMapper::PbrNeutral` for the Khronos neutral curve. The curve runs in linear
HDR after exposure and bloom and before saturation, contrast, and SDR encoding.
It preserves color through much of the exposure range and compresses bright
highlights. The existing HDR capability and fallback rules still apply. Change
the **Tone mapper** control under browser lighting quality, or the **PBR Neutral**
toggle in the Windows lighting gallery. Renderer quality remains transient.
[Khronos PBR Neutral specification](https://github.com/KhronosGroup/ToneMapping/tree/main/PBR_Neutral)

## Baked lighting zones

Use `Scene::setLightingZones` from the idle scene owner to replace up to two
world-space AABBs. A `LightingZone` provides minimum and maximum bounds, a
positive blend distance, a shared immutable baked `Environment`, intensity and
rotation in radians. The blend distance may reach half the shortest box extent.
Validation completes before publication. Empty zones retain the scene-wide
environment. Zone bounds and shared assets survive v38 and newer archives.

At each surface position, a smooth weight rises from zero at the box edge to
one at the blend distance. Overlapping weights are normalized if their sum
exceeds one. The scene-wide environment fills any remaining weight. A zone with
zero intensity can represent a dark interior. Probes affect diffuse irradiance
and roughness-dependent specular reflections on PBR meshes. Direct lights, water
shading and flat ambient retain their existing inputs. The probe bakes are
authored assets and reflect their original capture directions. The optional
[dynamic probe system](dynamic-lighting.md) adds diffuse bounce from live proxy
geometry. Baked-zone cubemaps keep their original capture directions.

The renderers pack diffuse and specular cubemaps using the global environment's
size and mip levels. The current baked environment resources are uploaded again
only when their identities or ordering change. Moving an object or adjusting
zone bounds, intensity or rotation only changes the sampled blend. Enable the
compact tier below to display zones on WebGL2.

Open `?scene=lighting-zone` to move the same ceramic and polished metal samples
outside, through the threshold and into the workshop. Toggle local lighting to
compare with the global bake. On Windows, use `--lighting-zone outside`,
`--lighting-zone threshold` or `--lighting-zone inside`, and add
`--global-lighting` to disable the local bake.

## WebGL2 compact PBR

`RenderQuality::webGlPbr` opts in when `RenderCapabilities::compactPbr` is
available. The basic path remains the default for existing scenes. The compact
tier evaluates roughness and metallic factors and the packed metallic/roughness
texture, and uses a prefiltered environment with an approximate specular BRDF.
It shares the same light budgets, shadow visibility and authored zone blend. The
compact atlas uses independently clamped cube faces, so very glossy surfaces can
show seams at face boundaries. WebGPU and Windows retain the full PBR shader.
Browser quality reports
`webGlPbr`, `compactPbr` and `effectiveCompactPbr` independently. The doorway
example requests compact PBR on WebGL2. Add `&webglPbr=off` to compare basic
lighting, or toggle it in the quality panel. The renderer links the compact
shader on first use and keeps the basic shader available.

## Verification

Build `web_assets` with the repository-local SDK pinned in `.emscripten-version`.
Run `scripts/lighting-showcase-smoke.cjs`, `scripts/lighting-post-smoke.cjs` and
`scripts/lighting-zone-smoke.cjs` on forced WebGPU and WebGL2. On Windows, build
`webengine_lighting_windows` and `windows_environment_smoke`, run
`scripts/windows-environment-smoke.ps1` and
`scripts/lighting-zone-windows-smoke.py`, and run the focused `shadow`,
`environment_lighting`, `render_quality` and `persistence` CTests. Use the
gallery to inspect the assigned maps and the look of each backend.

The implementation was verified in desktop Chromium on WebGPU and WebGL2 with
a ranked camera sweep, the doorway blend and global restoration, and the HDR
pixel suite. WebGL2 also passed the basic and compact tier comparison. Windows
Dawn and direct D3D12 each submitted two sun cascades and two spotlight maps in
the fixed ranked capture, passed the doorway pixel checks, and passed 39 HDR and
environment captures. The four focused Windows CTests passed. Physical mobile
devices and sustained mobile performance remain outside this validation.
