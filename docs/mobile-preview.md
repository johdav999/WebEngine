# Browser mobile preview

Click **Mobile preview** in the viewport footer of the playground or docked
browser scene editor. Choose a device, use **Rotate** to change orientation,
and select **Exit preview** to return to the full viewport.

| Preset | Portrait canvas | Landscape canvas | Requested DPR |
| --- | --- | --- | --- |
| Phone | 390 × 844 CSS px | 844 × 390 CSS px | 3× |
| Tablet | 768 × 1024 CSS px | 1024 × 768 CSS px | 2× |

The display shrinks to fit the available pane while the renderer and retained GUI
use the preset's logical dimensions. The active quality profile, resolution
scale, pixel ratio cap, pixel budget and backend limits determine the actual
backing resolution. See **Rendering quality** for the effective resolution.

The existing canvas stays live on the selected WebGPU or WebGL 2 backend.
Scene state, camera pose, pause and quality settings carry across the preview
switches. The preview cancels held input when changing device or orientation.
Mouse, keyboard and real touch contacts use the existing input bindings.
Preview settings are local to the page session.

This previews the renderer viewport and its in-canvas GUI. Browser page media
queries, mobile OS chrome, touch-device capabilities and GPU capabilities still
reflect the actual host. Use real devices for Android/iOS acceptance and
performance measurements. The native Windows renderer has its own window.

With a fresh `web_assets` build, the development server and the Playwright and
pngjs dependencies from the README, run `node scripts/mobile-preview-smoke.cjs`.
The smoke check uses both browser backends. Run the Windows renderer and
DesktopHost smoke checks separately for native regression coverage.

## Validation on 2026-11-05

- Chrome 156 on Windows passed the mobile preview smoke on WebGPU and WebGL 2.
  It checked both sizes and orientations, requested and capped backing
  resolutions, touch orbit on a scaled frame, retained GUI text entry and
  return to the original canvas and editor document. The editor layout smoke
  passed on both backends, and the input smoke passed on WebGPU.
- Native Windows passed the Dawn and direct D3D12 framebuffer smoke and
  DesktopHost API tests for both backends.

Local results and captures are under `build/evidence/mobile-preview/`.
