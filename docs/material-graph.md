# Material graph editor

Open **Materials · live preview → Open material graph** in the browser inspector.
`?scene=materials&graph=1` opens the showcase and graph together. Desktop layouts
keep the 3D preview beside the graph; narrower layouts stack the workspace.
Object and material-slot selectors remain in the inspector.

The initial graph uses the selected slot's precise linear material factors.
Choose **Checker example** for a procedural surface, or add nodes and connect an
output button to an input button. An input accepts one connection; a new one
replaces it. Click a connected input without a pending output to disconnect it.
Port colors identify scalar, color and UV types. Scalars can feed color inputs.
Cycles and incompatible types are rejected without changing the graph.

## Authoring

- **Value / Color:** constant factors, including HDR colors through the Color
  node's expandable linear RGB fields. Color pickers convert sRGB to linear RGB.
- **Texture coordinates / UV transform:** repeatable coordinates, scale and offset.
- **Image texture:** a local PNG/JPEG/WebP, resized to at most 256 pixels per side.
  Enable **Linear data** for roughness, metallic, occlusion and normal maps.
  **Color channel** extracts R/G/B or image alpha as a scalar.
- **Checker / Noise:** UV patterns; Noise is deterministic cell noise.
- **Multiply / Add / Mix colors:** arithmetic and interpolation; Mix clamps its
  interpolation factor to 0–1. **Multiply / Add values / Clamp value** operate
  on scalar branches.
- **Material output:** base color, roughness, metallic, emissive, opacity,
  tangent-space normal and occlusion, plus PBR/unlit and alpha-mode selectors.
  Normals use encoded 0–1 RGB with +Y convention. Connect an alpha channel to
  opacity and select Cutout/Blend/Additive to use transparency.

Live preview debounces edits. Disable it to assemble a graph, then choose
**Apply graph**. Invalid or incomplete connected branches preserve the last valid
material. Disconnected image nodes may remain unfinished. Surface scalar factors
must be in 0–1; Clamp can constrain arithmetic results.

Drag a node header to arrange it, drag empty canvas to pan, and scroll or use
the +/− buttons to zoom. **Fit graph** frames all nodes. Touch supports header
dragging, panning and zoom buttons. Port buttons also support normal keyboard
focus and activation. Delete/Backspace removes a selected node; the output cannot
be removed. Undo/Redo and Ctrl/Cmd+Z (Shift+Z to redo) retain up to 50 changes.
Dragging and undoing layout alone do not upload new material textures.

Graphs stay separate for each selected object/slot during the current scene
revision. Switching/reimporting a scene starts fresh graphs. **Reset graph**
restores the starting factors and source maps on the next preview/apply.
Inspector changes and graph application both edit the same slot; applying the
graph uses its factors and the maps captured at its first successful application.

**Save JSON** downloads a version 1 graph including image pixels. **Load JSON**
validates the entire file before replacing the current graph. Files remain local.
Limits: 64 nodes, 256 links, 1 MiB of embedded RGBA pixels, 8 MiB JSON imports,
16 MiB image files and 8192 pixels per side before resizing. JSON round trips do
not depend on temporary image URLs or external file paths.

## Implementation

`web/material-graph.js` is a reusable DOM-free graph model and compiler, also
loadable from Node. It checks IDs, parameters, types, cycles, image payloads and
bounded sizes. It evaluates reachable nodes in topological order. Constant
branches produce precise material factors; UV-dependent branches bake to
64/128/256-square RGBA maps only when edited/applied. This is a texture-baking
graph, with no per-frame graph evaluation or dynamic WGSL generation.

Color maps store sRGB bytes; metallic/roughness, occlusion and normal maps store
linear data. Roughness uses G and metallic B. Opacity shares base-color alpha.
HDR constants stay material factors; baked RGB must be within 0–1. An image
alpha branch must explicitly feed opacity; the image's RGB output feeds color.

`web/material-graph-editor.js` owns selection, history, graph viewport, async file
reads and the Wasm upload. Selection changes invalidate pending file reads and
debounced updates. All imported graph content is treated as data.

`engine::bindMaterialGraph` (`material_graph.hpp/.cpp`) creates a complete
candidate material from 19 factors and packed map pixels, validating everything
before publication. Maps absent from the bake inherit the original source;
disconnecting a baked branch restores that source map. The demo's
`engine_material_graph` bridge retains source bindings per slot and scene
revision, then applies a single immutable material instance. Shared imported
assets and other objects remain unchanged. Existing renderer cache, mip,
skinning, lighting, alpha and shadow paths render the resulting maps.

## Verification

```powershell
node --test tests/material-graph-tests.cjs
cmake --build build/native
ctest --test-dir build/native -R material_graph --output-on-failure
# Use the matching MinGW compiler bin directory on PATH for native test DLLs.

$env:NODE_PATH = 'path/to/test/node_modules' # playwright and pngjs
$env:BROWSER_EXECUTABLE = 'C:/Program Files/Google/Chrome/Application/chrome.exe'
$env:DEMO_URL = 'http://localhost:8093'
node scripts/material-graph-smoke.cjs
node scripts/material-smoke.cjs
```

Local evidence, 2026-10-10: Release Emscripten browser build passed; graph binding
test and seven Node model tests passed. Chrome 154.0.8037.99 graph and existing
material browser suites passed with no page, console or WebGPU validation errors.
Graph screenshots, downloaded JSON and results: `build/evidence/material-graph/`.
Existing material regressions: `build/evidence/material-graph-regression/`.

The broader native run passed 22 of 23 tests; the separate `morph` test failed at
`Morph positions are skinned before bounds calculation`. This is outside the
material graph paths and is recorded rather than represented as a clean engine
suite. Chromium touch emulation does not establish physical-device acceptance.
