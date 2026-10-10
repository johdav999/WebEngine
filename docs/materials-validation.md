# Material implementation and validation

Validated locally on 2026-10-09 in the existing WebEngine checkout. Changes are
uncommitted; the checkout already contained untracked project files before this
work. The published `dist/` is the Release build, using pinned Emscripten 4.3.2.

Release `dist/webengine.wasm` SHA256:
`91C3951801F4C094B4DA9D067F7B741A5D747ED23088EDAA1CE7D8522EE7B6A1`.

## Delivered stages

| Stage | Result | Main implementation |
|---|---|---|
| 1 | Shared material resources and isolated per-slot instances; legacy texture/tint support | `material.hpp`, `material.cpp`, `mesh_component.cpp`, `scene.cpp` |
| 2 | Cached material bindings/pipelines; opaque, mask, blend, additive; double-sided and mirrored winding | `renderer.cpp`, `render_frame.cpp`, `mesh.wgsl` |
| 3 | Metallic–roughness PBR, semantic texture uploads/mips, static/skinned tangents, FBX mapping | `texture.cpp`, `mesh.cpp`, `fbx_importer.cpp`, `mesh.wgsl` |
| 4 | Baked specular/diffuse environment and BRDF lookup, live per-slot inspector and showcase | `environment.cpp`, `material_showcase.cpp`, `main.cpp`, `web/materials.js` |

Material channels and texture interpretation follow the
[glTF material conventions](https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html#materials),
with conservative engine defaults for manually created materials. Existing FBX
loading remains the import path; this work does not add a glTF file loader.

## Evidence packet

Environment: local Windows, headless Chrome **154.0.8037.99**, real WebGPU renderer.
Desktop viewport: 1440 × 1040. Responsive checks also cover the mobile layout.
No credentials or remote services are involved. Browser tests used a temporary
server on port 8087; the existing server on port 8080 serves the same `dist/`.

Open `http://localhost:8080/?scene=materials` for the eight-object material
showcase, or use **Material showcase** in the inspector on the normal scene.
The showcase includes shared oak variants, polished and rough copper, cutout
cloth, transparency, animated normal-mapped weave, and an unlit surface.

Both Debug (assertions and SAFE_HEAP enabled) and Release builds passed the full
material browser suite. Its 13 checks include pixel comparisons, isolated edits,
invalid input handling and resource cache assertions. No page, console or GPU
errors were reported.

| Flow | Expected and observed result | Evidence |
|---|---|---|
| MAT-001: shared assets and overrides | Editing one oak instance leaves the second unchanged; reset restores the original pixels; invalid native edits preserve state | [Release results](../build/evidence/materials/results.json), [Debug results](../build/evidence/materials-debug/results.json) |
| MAT-002: alpha and rendering state | Opaque ignores alpha; mask cuts out cloth; blend/additive render through their pipelines; backface lighting matches front lighting when double-sided, and single-sided backfaces are culled | `opaque-sail.png`, `mask-sail.png`, `blend-sail.png`, `additive-sail.png`, `back-double-sided.png`, `back-culled.png` in `build/evidence/materials/` |
| MAT-003: PBR and tangent bases | Roughness visibly changes copper reflections and resets exactly; normal strength changes pixels; static, skinned and mirrored test geometry produce matching pixels | `roughness-edit.png`, `reset.png`, `normal-off.png`, `tangent-*.png` in `build/evidence/materials/`; native `material` test |
| MAT-004: environment and live inspector | Intensity/rotation change lighting and restore exactly; local panorama bakes reflections; studio reset restores pixels; image edits affect only the selected slot; replacing a same-shaped FBX cancels pending stale image edits | `environment-*.png`, `panorama.png`, `studio-restored.png`, `async-reimport-preserved.png` in `build/evidence/materials/` |
| MAT-005: animation and return navigation | Skeletal normals render while animation changes pixels; returning from the showcase preserves the imported scene; mobile controls remain usable | `animated.png`, `mobile.png`, `mobile-showcase.png` in `build/evidence/materials/` |
| MAT-006: material caching | Warm stationary scenes perform no GPU writes; roughness edits reuse cached pipelines | Release and Debug material results |

Visual captures: [showcase](../build/evidence/materials/showcase.png),
[desktop inspector](../build/evidence/materials/desktop-inspector.png),
[alpha controls](../build/evidence/materials/desktop-alpha-controls.png),
[environment controls](../build/evidence/materials/desktop-environment-controls.png).
JSON files and screenshots live under ignored `build/evidence/`; run the scripts
below to regenerate them. This document and the test scripts are durable.

## Native and regression checks

All **13 native tests pass**: material, effects, component, actor, camera,
animation, instancing, transform cache, render frame, mesh optimizer, performance,
engine core and FBX importer.

The new material tests cover atomic overrides and whole-mesh image replacement,
image meaning and odd-sized mip levels, normal/tangent handedness and skin seam
splitting, half-float encoding, constant-HDR environment convolution, showcase
configuration and actual imported Knarr material maps. An in-memory FBX fixture
also verifies HDR emission, legacy opacity and shared base/normal image decoding.

Existing browser regression suites also pass:

| Suite | Evidence and observed result |
|---|---|
| General browser | [14 flows](../build/evidence/material-browser/browser-smoke.json), no errors |
| Effects | [10 flows](../build/evidence/material-fx/results.json), no errors |
| Skeletal animation | [7 flows](../build/evidence/material-skeleton/skeletal-animation-results.json), no errors |
| Instancing | [Pixel equivalence](../build/evidence/material-instancing/instancing-results.json); Knarr 46 to 11 draw calls, cubes 45 to 3 |
| Upload caching | [120 static frames without writes](../build/evidence/material-cache/cache-upload-results.json); moving single instance 640 bytes, cube demo 2,176 bytes |
| Frame reuse | [5 flows](../build/evidence/material-frame-reuse/frame-reuse-results.json); retained capacities and released obsolete resources |
| Performance reporting | [8 flows](../build/evidence/material-performance/performance-smoke.json), GPU timestamps and downloaded performance JSON |
| Mesh optimization | [Matching optimized/unoptimized pixels](../build/evidence/material-optimization/mesh-optimization-results.json); 86,544 to 38,621 vertices, 4,645,584 to 2,099,544 geometry bytes |

These are correctness and resource-accounting observations on this environment,
not a browser compatibility matrix or a comparative FPS benchmark.

## Fixed issue ledger

| ID | Severity | Flow | Issue | Acceptance / regression | Status |
|---|---|---|---|---|---|
| MAT-F01 | P2 | MAT-001 | Changing one control quantized unrelated imported factors through UI values | Edit roughness with precise HDR base/emission factors; every unrelated parameter remains exactly equal | Fixed; explicit Release browser regression passes |
| MAT-F02 | P2 | MAT-004 | Choosing an emission hue at zero intensity lost the hue before intensity was raised | Select red at zero, then intensity 2; native emission becomes `(2,0,0)` | Fixed; Debug and Release browser checks pass |
| MAT-F03 | P1 | MAT-004 | A delayed image read could target an identically shaped replacement scene | Scene revision changes invalidate pending reads; replacement pixels remain unchanged | Fixed; Debug and Release browser checks pass |

## Reproduction

Build the core/tests with the existing native CMake setup and run
`ctest --test-dir build/native --output-on-failure`. On Windows, the configured
compiler's DLL directory must be on PATH for its native executables.

```powershell
./scripts/build.ps1 -Configuration Debug
python scripts/serve.py --port 8087
```

In another terminal with Playwright and pngjs available:

```powershell
$env:DEMO_URL = 'http://localhost:8087'
$env:BROWSER_EXECUTABLE = 'C:\Program Files\Google\Chrome\Application\chrome.exe'
$env:EVIDENCE_DIR = 'build/evidence/materials-debug'
node scripts/material-smoke.cjs
```

Repeat with `./scripts/build.ps1 -Configuration Release` and
`EVIDENCE_DIR=build/evidence/materials`. Existing regression scripts retain their
normal invocation; the changed expectations account for tangents, PBR maps and
the 496-byte global uniform.

## Current scope

- Inspector edits are local to the running session; there is no saved material
  library or persistent asset authoring workflow.
- Tangents handle static/skinned/mirrored geometry consistently but are not
  generated with MikkTSpace. Normal maps baked against other bases need visual QA.
- Blend sorting remains based on mesh origin; refractive transmission and
  order-independent transparency are not implemented.
- Local environment files use the existing PNG/JPEG/TGA/BMP RGBA8 decoder.
  Linear HDR environments are supported through `Environment::bake`'s radiance
  callback; HDR/EXR file decoding is not implemented.
- Unsupported alternate texture UV sets/transforms, layered/procedural FBX maps,
  separate opacity textures and PBR transmission produce import warnings.
- The environment defaults to a 32-pixel cube; callers can bake powers of two
  from 4 to 128. The showcase demonstrates lighting behavior rather than finished
  game art.
