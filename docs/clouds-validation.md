# Volumetric cloud validation

Validated on 2026-11-07 using the repository-local Emscripten 6.0.11 browser
build and native Windows Release builds. Browser tests used desktop Chrome
with forced WebGPU and WebGL2; native tests selected Dawn and D3D12 explicitly.

The initial sky-only release passed 22 rendered scenarios per backend (88 total).
The depth/shadow extension now passes 34 scenarios per backend (136 total),
including receiver, foreground-depth and planar-reflection fixtures.
They cover the legacy layer, volumetric density, wind, camera translation,
inside/above-layer rays, zero extinction, sparse/overcast coverage, zero-budget
fallback, sphere/fullscreen equivalence, orthographic pixel origins, night and
sunset, distance clipping, per-view suppression, HDR, MSAA and foreground geometry.
Browser checks additionally passed pause, explicit wind stepping, inspector
controls against reported engine state, and device/context recovery.

Legacy translation, zero extinction, the 2D quality fallback and distance clipping
match their reference images within one display code. Sphere, HDR and MSAA
comparisons use bounded image tolerances; they are not claims of bit-identical
output across different APIs. Native captures are losslessly converted from PPM
to PNG after verification, preserving every RGB value.

Six focused native tests passed: `sky`, `render_quality`, `persistence`,
`physical_surface`, `region_stream` and `terrain`. New tests cover cloud validation
and atomic replacement, disabled defaults, explicit budgets, world-coordinate
sampling, large coordinates, orthographic offsets, immutable atlas identity and
wrapped borders, cloning, archive v41 round trips and v40/v5 compatibility.

Reproduce after building `web_assets` and `webengine_clouds_windows`:

```powershell
$env:NODE_PATH = (Resolve-Path 'build/detail-browser-tools/node_modules').Path
$env:BROWSER_EXECUTABLE = 'C:/Program Files/Google/Chrome/Application/chrome.exe'
$env:DEMO_URL = 'http://localhost:8176'
$env:EVIDENCE_DIR = 'build/evidence/cloud-depth'
node scripts/clouds-smoke.cjs browser
node scripts/clouds-smoke.cjs windows build/windows-desktop/Release/webengine_clouds_windows.exe
node scripts/clouds-fog-coexistence-smoke.cjs
ctest --test-dir build/windows-desktop -C Release -R '^(sky|render_quality|persistence|physical_surface|region_stream|terrain)$' --output-on-failure
```

Serve a matched copy of the selected browser JS, Wasm and host files. The
depth/shadow validation used the evidence path above and a server on port8176.
The tested snapshot is `build/cloud-depth-site/`; captures and reports are under
`build/evidence/cloud-depth/`. `CLOUDS_REUSE_NATIVE=1` permits rechecking retained native
PPMs or lossless PNGs after a harness-only assertion change; normal runs render
fresh images.

The depth/shadow extension has fresh 2026-11-07 pixel checks on all four backends.
A sunlit ground receiver darkens under clouds and changes with wind; point-only
lighting and receivers above the cloud layer stay pixel-identical. Three green
objects before, within and beyond the layer verify depth composition: the near
object is unchanged, the other objects receive scattering/attenuation, and zero
extinction restores all three references. Planar-water comparisons verify
reflected foreground attenuation and two cloud field passes (reflected then main). Browser cloud-field and terrain-shadow
recovery reproduce their images exactly. Actual grid/shadow pass counters verify
policy enablement, including sky/view opt-out. All six native contract tests were
rebuilt and passed again.

The dedicated browser coexistence test also passed on WebGPU and WebGL2:
expanding cloud storage while a fog grid and spotlight optical-depth cache are
active preserves the reference image within one display code. Its report is
`build/evidence/cloud-depth/cloud-fog-coexistence.json`. A held WebGPU cloud-shadow
view produced valid timestamp samples after the accounting fix; the short probe
is recorded in `webgpu-cloud-timing.json`, not treated as a sustained benchmark.

Cloud-prefix passes, including reflected fields, are included in the WebGPU/Dawn
World GPU timing bucket. Cloud transmission-map production is included in Shadow
GPU time, so total GPU time includes the new producer work. WebGL2's available
whole-frame timer and D3D12's pass timestamps also include this work. These short
functional captures do not establish sustained performance budgets.

No physical Android/iOS acceptance or temporal accumulation is established.
Planar reflection uses a virtual camera field; transport is approximate if the
real camera-to-water leg crosses clouds. See
[cloud contracts](sky.md#volumetric-cloud-layer).
