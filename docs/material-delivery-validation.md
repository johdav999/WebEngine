# Material delivery validation

Validation date: 2026-11-07, Windows x64. Current source, not historical evidence,
was built with pinned Emscripten 6.0.11 and the native Windows toolchain.

| Surface | Result |
| --- | --- |
| Browser WebGPU, Chrome 156 | Eight scalar/effect regions and six packed-map regions passed; restoring the original parameters reproduced the original pixels |
| Browser WebGL2, Chrome 156 | Same eight/six comparisons passed, including the default basic-tier anisotropic highlight, coat normal and specular masks |
| Windows Dawn | Eight/six comparisons passed; final integrated map capture also passed |
| Windows direct D3D12 | Eight/six comparisons passed; all changed DXC entry points compiled with warnings as errors; final integrated map capture passed |
| Portable native import/archive | Five focused CTests passed: material_delivery, material_delivery_archive, material, glb_importer, scene_archive |
| Broader native regressions | Twelve CTests passed: terrain, terrain_paging, water, persistence, spline_route, mesh_detail, physical_surface, region_stream, spotlight, instancing, render_frame, camera_follow |
| Node/Wasm | 42 importer/material checks and 11 archive checks passed |
| Actual Unreal static bundle | LoadFile and decode/archive/hydrate passed; 683 vertices, one material |
| Actual Unreal skeletal bundle | LoadFile and worker transport passed; 21,585 vertices/skin entries, two materials, six advanced maps within existing importer limits |
| Browser ordinary file import | Both actual bundles passed on both backends |
| Browser real Web Worker decode | Both actual bundles passed with validated v40 archive transports |
| Browser scene save/open | Both actual bundles saved and reopened on both backends, with no page errors |

The real bundles came from AssetManager's normal browser and fresh STDIO MCP
submission paths, using Unreal 5.8.4 on a disposable ScfiAssets2 copy. Bundle
checksums, downloaded bytes and source integrity were verified by AssetManager.
Private identifiers, absolute output locations and asset metadata remain in local
evidence, not this repository. Native reports include post-integration source
hashes. Build output contains pre-existing compiler warnings; builds passed.

## Reproduce

```powershell
# Follow README/toolchain setup first. Use the existing native test build.
cmake --build build/native --target material_delivery_tests material_delivery_archive_tests
ctest --test-dir build/native -R '^(material_delivery|material_delivery_archive|material|glb_importer|scene_archive)$' --output-on-failure

# Use the pinned local Emscripten SDK to configure the browser build.
cmake --build build/web-release --target webengine asset_decode_worker material_delivery_wasm material_delivery_archive_wasm
node build/web-release/material_delivery_wasm.js
node build/web-release/material_delivery_archive_wasm.js

# Start the browser server using README instructions, then:
python tools/verify_browser_material_delivery.py --url http://127.0.0.1:8080 --evidence build/evidence/material-delivery-browser
python tools/verify_windows_material_delivery.py build/windows/Release/webengine_lighting_windows.exe build/evidence/material-delivery-windows
```

Browser image verification needs Playwright, Chrome and Pillow; Windows image
verification needs Pillow. Set the matching native compiler DLL directory on PATH
when using MinGW CTests. Actual Unreal files stay out of Git; pass their paths to
`material_delivery_tests` to repeat native import/worker acceptance, then use the
ordinary browser file input and scene save/open controls for the browser checks.

Low C: disk space required moving new validation builds/project copies to a
separate drive. This did not change the source library, engine dependencies or
consumer pins. Existing build directories and unrelated work were preserved.

See [delivery contracts](material-delivery.md) for shading, atlas, dynamic effect,
color-band filtering and unsupported-graph limits. Results do not establish exact
Unreal shader equivalence, physical mobile performance or Safari support.
