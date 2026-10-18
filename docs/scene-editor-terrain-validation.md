# Terrain editor validation

Recorded 2026-10-17. Scope: optional EditorSession terrain transactions and browser
editor terrain creation/sculpt/paint, persistence, input and existing workflows.

## Build and environment

- Pinned Emscripten 6.0.11, Release `build/web-editor-release`, `webengine` and
  `web_assets` targets, matching host/Wasm publication to `dist`.
- Isolated matching site `build/editor-terrain-site`, localhost port 8169.
  The user's existing editor tab was not reloaded.
- Chrome 155.0.8059.80, real WebGPU and forced WebGL2, DPR1 and DPR2.
- Native MinGW `build/native-editor-content`, cached pinned Jolt dependency.
- Native `editor_terrain`, `editor`, `editor_content`, `editor_structure`, `terrain`:
  **5/5 passing**. This includes exact source/collision height agreement, source
  version/handle preservation, one history command per stroke, painting with
  unchanged geometry/collider, cancellation/external-write guards, archives,
  source-only ownership rejection and creation undo/redo.
- Browser evidence: `build/evidence/editor-terrain/final/dpr{1,2}/`, organized by
  suite, plus screenshots and actual downloaded terrain scene files.

## Terrain acceptance

`scripts/editor-terrain-smoke.cjs` uses actual DOM pointer/keyboard events,
Wasm authoring commands, real canvas pixels and downloaded/reopened scene files.
Each renderer/density run covers ten flows:

1. Quick Add creates/selects/frames a complete terrain with four stable chunks,
   layered material and static collision.
2. Brush gestures change the actual rendered terrain and increment history/revision
   once while retaining chunk saved identities.
3. Undo/redo restores source heights; Escape cancels the whole preview.
4. Window blur and busy transitions roll back without adding history.
5. Lower, Flatten and Smooth apply and undo correctly.
6. Surface painting changes pixels without changing source heights; undo/redo works.
7. Play disables terrain authoring; Stop retains source/history.
8. Downloaded/reopened scene files preserve heights, layers and editability.
9. Floating Details and the 390px compact layout preserve controls without
   horizontal overflow.
10. Invalid creation preserves the previous scene; an imported red-channel image
    produces the requested height range/resolution and reversible creation.

An eleventh flow verifies that a second emulated touch cannot replace the active
stroke owner or leave a preview stuck. Final editor results: **272 checks across
12 suite/DPR reports**, all passing with empty recorded error lists (44 terrain
checks and 228 existing editor checks). `final/summary.json` records the total.

The standalone terrain-edit suite also passed on both renderers. A focused
editor-to-demo return check in `final/demo-editor-return.json` verified the source
handoff: editor sculpting increased the center height from 0.02378m to 0.52378m;
the resumed demo accepted its own brush and reached 0.77378m on both backends.
It issued no stale-source write. Final host/Wasm hashes are in `final/identity.json`.

The five existing editor browser scripts remain the regression baseline for camera
navigation/framing, docking, transforms, numeric/gizmo input, component/material
editing, imports, save/load, autosave, hierarchy/physics constraints and isolated
Play. The terrain demo's existing `terrain-edit-smoke.cjs` checks its standalone
sculpt/paint/streaming/archive/recovery path separately.

## Issues and limits

- Browser tests originally transported a large default imported-model scene as a
  numeric array, exhausting the test runner's heap. The accepted test uses the
  default primitive fixture and actual browser download/file-open paths; no engine
  archive limits were increased.
- Brush sampling uses source-local spacing and a maximum one stamp per animation
  frame, following movement by 20% radius or repeating after 100ms. The first
  pointerdown stamps immediately. It does not interpolate an unlimited queued
  pointer path. Terrain rebuild/publication is synchronous with no worker budget.
- Complete eager sources are supported. Stream-owned source-only assets, partial
  instances and paged patches reject editor strokes; their existing persistence,
  IO, generation and dirty/writeback owners remain authoritative.
- This initial terrain increment used four colored surface presets and retained
  existing authored layer textures. Texture assignment was added afterward; see
  [layer texture validation](scene-editor-terrain-textures-validation.md).
- Creation is bounded to 33/65/129-square samples and 1–512m width/depth. Imported
  heightmaps use decoded 8-bit red with nearest sampling, no gamma/flip. Source
  terrain API limits still apply to existing scenes.
- History retains initial/final immutable source versions in the existing bounded
  command list. Brush preview does not advance the committed dirty revision or
  autosave; cancellation refuses conflicting external writes rather than overwriting
  them. These are desktop/native checks, not physical mobile acceptance.

Both engine-skill copies were updated in the same task, preserving their existing
guidance. Both skill validators pass.
