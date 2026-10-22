# Directional shadow maps: implementation and validation

Historical evidence below describes the original fixed-volume WebGPU path.
Current spot/cascade and WebGL2 contracts are in [lighting](lighting-post-processing.md).

Verified locally on 2026-10-10 in the existing checkout. Changes remain
uncommitted alongside the pre-existing untracked project. Concurrent physics,
camera culling and view-compositing work was preserved.

## Delivered

- Up to four directional shadow maps per view, 1024×1024 depth array, 3×3 PCF.
- Light-local volume center, world-space extent/depth, texel snapping, raster and
  receiver bias; validated light settings.
- Mesh cast/receive flags; independent shadow batches retain camera-culled
  casters and reuse the instance staging buffer.
- Static/skinned, mirrored/double-sided and alpha-masked caster pipelines.
- Per-view uniforms and visibility masks; shadow pass counts, depth memory and
  separate GPU timing included in performance totals.
- Demo sun enabled by default with a Sun shadows control and `?shadows=off`.

## Verification

Native build succeeded and all **19 CTest suites passed**. The new shadow suite
checks projection/depth conventions, axis-aligned light directions, texel
stability, invalid configuration, hierarchy, participation, batching, selective
uploads and GPU metric accounting. Existing warm allocation checks still pass.

Release browser build: `build/web-shadows-release`. Tests used its copied,
isolated site in `build/shadow-browser-site` on localhost:8095, avoiding concurrent
publication changes to `dist/`. Tested Wasm SHA256:
`89D77C7F0D5ADC9FD5159E900EF44C82CF16B90B95E67B1B63531D8BEACC414F`.

Real Chrome 154 WebGPU acceptance passed:

- `scripts/shadow-smoke.cjs`: visible Knarr shadow/toggle, cast/receive flags,
  cutout discard, blended exclusion, mirrored/double-sided geometry, skeletal
  deformation, multiple lights/four-map budget, disabled lights, off-camera
  casters, two independent camera masks, zero warm writes and GPU shadow timing.
- `scripts/material-smoke.cjs`: all material/IBL/edit/import/basis checks.
- `scripts/instancing-smoke.cjs`: exact matching instanced/individual pixels.
- `scripts/cache-upload-smoke.cjs`: static zero writes and selective uploads;
  animated Knarr 1,296 B/frame, rotating demo 2,832 B/frame.
- `scripts/performance-smoke.cjs`: timing, memory, saved JSON export, reset and
  unsupported/disabled timestamp feature paths.
- `scripts/browser-smoke.cjs`: startup, lighting/input, texture/FBX replacement,
  error recovery, restoration, resizing and unsupported-browser behavior.

Shadow screenshots and machine-readable acceptance results:
`build/evidence/shadows/`, particularly `knarr-shadows-on.png`,
`off-camera-caster.png`, `two-camera-masks.png` and `results.json`.
The expected four-map overflow warning is captured separately; no WebGPU errors.

## Scope

Volumes are configured orthographic boxes, not cascades or automatic camera fits.
Point-light shadow cubemaps are not implemented. Transparent materials receive
but do not cast shadows; particles and Background/Overlay meshes are excluded.
Desktop Chrome evidence does not establish physical mobile/Safari compatibility
or a comparative performance claim. The fixed depth array costs 16 MiB nominal
payload, and each active shadow light adds a depth pass per camera view.
