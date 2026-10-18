# Terrain paint layer texture validation

Recorded 2026-10-17. Scope: assigning/clearing imported albedos for the selected
terrain's four paint layers, with history, rendering and scene persistence.

## Build and environment

- Local pinned Emscripten 6.0.11, Release `build/web-editor-release`, `webengine`
  and `web_assets` targets publishing matching artifacts to `dist`.
- Isolated matching preview: `build/editor-terrain-textures-site`, port 8170.
  The existing editor tab was not reloaded.
- Chrome 155.0.8059.80, real WebGPU and forced WebGL2 at DPR1 and DPR2.
- Native MinGW `build/native-editor-content`, using the cached pinned Jolt source.
- Native `editor_terrain`, `terrain`, `editor`, `editor_content`, `editor_structure`:
  **5/5 passing**. New cases verify geometry/LOD sharing, unchanged collider/handles,
  updates across all chunks, another instance retaining the old shared source,
  no-op history, invalid textures/layers, clear/undo/redo, preservation through
  later paint and native scene archives. Core tests also cover unchanged layer
  definitions and source-only assets retaining no eager geometry.
- Evidence: `build/evidence/editor-terrain-textures/final/dpr{1,2}/`, organized by
  suite, with screenshots, generated checker textures and actual downloaded scenes.
  Final host/Wasm hashes are recorded in `final/identity.json`.

## Browser acceptance

Final results: **164 checks across eight suite/DPR reports**, all passing with
empty recorded error lists (28 layer-texture checks and 136 regressions).
`final/summary.json` records the aggregate.

`scripts/editor-terrain-textures-smoke.cjs` uses actual imported PNGs, native Wasm
commands, DOM selectors, brush pointer events, rendered pixels and scene downloads.
Each backend/density run checks:

1. Four selectors use the current imported Assets catalog and require selected terrain.
2. Assignment renders immediately as one history command; repeat assignment is a
   no-op; undo/redo restores the binding.
3. A selected chunk resolves its complete terrain; actor selection shows the same
   four assignments.
4. None restores the layer color, with reversible clearing.
5. Painting retains images; reassigning a painted layer retains its weights and
   undo restores the exact terrain pixels.
6. Play gates texture editing; Stop and downloaded/reopened scenes retain images,
   painted weights and exact terrain pixels.
7. Controls work in floating Details and compact layouts without horizontal overflow.

The terrain, content and structure browser suites are regressions at both densities
and backends, covering sculpt/paint, touch-owner cancellation, heightmaps, history,
assets/materials, scene files/autosave, hierarchy, gizmos and Play restoration.

## Contracts and limits

- `TerrainAsset::withLayers` stages an immutable appearance-only edit with shared
  geometry/LOD and preserved source/patch metadata. `applyTerrainEdit` updates all
  chunk layer bindings; it rebuilds no geometry/collision. Stream owners use their
  existing publication paths rather than direct editor writes.
- `EditorSession::setTerrainLayerTexture(0..3,texture)` uses complete eager-instance
  guards, rejects active gestures, treats null as clear, and adds one ordinary
  terrain history command. The source owns the binding so later painting retains it.
- Tint, roughness, UV tiling/offset, paint weights, other layers and unrelated
  material settings are retained. Textures multiply existing layer colors.
- Existing texture validation applies: RGBA8 fallback, at most 4096px per axis.
  These are albedos; no per-layer normal map controls were added. Plain non-layered,
  streamed/paged, busy and Play targets retain their existing read-only gates.
- Exact pixel comparisons exclude pointer-focus outlines, gizmos/brush cursors and
  the playground's DOM scene-title metadata. Initial comparison mismatches came
  from these UI overlays, not terrain image/weight restoration.
- Native/desktop/DPR checks do not establish physical Android/iOS acceptance.

Both installed and CatGame engine-skill copies include the API/control/validation
updates. Their existing terrain guide was corrected to describe texture assignment
as delivered; both skill validators pass.
