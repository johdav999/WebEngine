# Render surfaces

A render surface is a camera output that an opaque unlit material can display
on a mesh. Browser WebGPU, WebGL 2, Windows Dawn and direct D3D12 use the same
configuration. Each renderer owns its own GPU color and depth storage for the
handle. Use ordinary window views for viewport-only split screen or picture in
picture.

```cpp
auto feed = std::make_shared<engine::RenderSurface>(256, 128);
engine::MaterialInstance monitorMaterial;
monitorMaterial.shader = engine::MaterialShader::Unlit;
monitorMaterial.baseColorTexture = standbyImage;
monitorMaterial.baseColorSurface = feed;
monitorMesh.setMaterial(0, monitorMaterial);

std::array views = {engine::RenderView{&mainCamera}, engine::RenderView{&rearCamera}};
views[1].target = feed;
renderer.setViews(views);
```

On Windows, use `DesktopView` with the same `target` and camera component ID,
then call `DesktopHost::setViews(sceneId, views)`. Register views at an idle
boundary. Browser views borrow their cameras until replaced; remove the view
before destroying its camera. The desktop host resolves registered camera IDs
against the active scene each frame and skips removed cameras.

`RenderSurface` is immutable. Use a new handle to change the extent, then
replace the producer view and material binding at the same idle boundary. The
extent is independent of the window, DPR and main render scale. A plan allows
one producer per handle, at most four surfaces, dimensions from 1 through 2048
and at most 4,194,304 pixels in total. Invalid plans leave the previous plan in
place. The normalized camera viewport resolves against the surface's dimensions.

Surface producers render before window views. Each refresh clears the whole
surface to the default clear color, then applies the view's optional clear color
inside its own viewport. Each capture draws the ordinary opaque and masked mesh
layers using its camera's visibility mask and detail selection. Sky and analytic
fog follow the view flags. Surfaces use single-sample SDR color, private depth and
top-left UVs. Capture omits shadows, water, volumetric fog, transparent draws,
billboard and ribbon effects, post-processing and GUI. Window views keep their
normal features and draw GUI once after composition.

Set the producer's `refreshSurface` to `false` to hold the previous image while
it stays registered. Applications choose the refresh cadence and can skip a
capture when a feed is hidden. Disabled cameras or empty viewports also skip the
capture. The material uses its ordinary base-color texture (white if absent)
until the first complete capture, when no producer is registered, during every
offscreen surface capture, and after recovery until refreshed. A monitor seen by
its own camera therefore displays its static fallback in that feed. GPU images
are scoped to the renderer and scene binding. Archives save camera components,
window views, and the static material fallback. Recreate producer views and
surface bindings when restoring a scene.

`renderSurfacePasses` counts captures rendered this frame. `renderSurfaceBytes`
estimates allocated surface color and depth payload. A held feed keeps its
allocation. Render cost scales with the number of refreshed views, their size and
their visible content.

The shared rear-camera monitor fixture lives in `examples/render_surface_demo.hpp`.
Build the Windows demo with `scripts/build-windows.ps1` or the
`webengine_render_surface_windows` CMake target. Launch it with `--backend dawn` or
`--backend d3d12`. Press L to start the feed, F to hold it, R to refresh it,
S to replace its extent, or X to remove the producer. Open `?scene=render-surface`
in the browser playground to see the live monitor. Browser smoke forces each
renderer and checks both camera orientation and recovery. Native smoke compares actual framebuffer captures and
checks diagnostics. Run `render_view`, `scripts/render-surface-smoke.cjs`, and
`scripts/render-surface-windows-smoke.py <path-to-webengine_render_surface_windows.exe>`
with fresh builds.

## Validation

- Browser WebGPU and WebGL 2 passed nine pixel cases each in headless Edge with
  the repository's Emscripten 6.0.11. The cases include feed orientation, hold,
  refresh, surface and window resizing, removal, and graphics recovery.
- Windows Dawn and direct D3D12 passed ten framebuffer and counter cases each,
  including sampling on the first frame after refresh, removal of the producer
  camera, and graphics recovery. Native `render_view` also passed extent,
  validation, batching identity, and archive checks.
- WebGL 2 shader startup and restoration took longer than the smoke runner's
  usual timeout on this machine. The fixture allows a longer shader startup
  window. Physical mobile devices were outside this run.
