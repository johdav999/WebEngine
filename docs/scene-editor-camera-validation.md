# Editor camera navigation validation

Recorded 2026-10-16. Scope: Scene mouse navigation, held-key flight, outline
framing, camera persistence, and preservation of existing editor workflows.

## Build and environment

- Repository-pinned Emscripten 6.0.11, Release `build/web-editor-release`;
  `webengine` and `web_assets` targets publish matching artifacts to `dist`.
- Isolated preview `build/editor-camera-site`, localhost port 8168. The existing
  editor at port 8167 was left running with its in-memory scene untouched.
- Chrome 155.0.8059.80, real WebGPU and forced WebGL2, DPR1 and DPR2.
- Native MinGW build `build/native-editor-content`, using the existing pinned
  Jolt source at `build/dependencies/JoltPhysics-5.3.0`.
- Native `input`, `camera`, `camera_controller`: 3/3 passing. Tests cover eye-fixed
  look, looking above the horizon, local-axis flight, focus projection, orbit
  continuity, invalid-input rejection, unchanged legacy limits and wider editor
  limits, plus existing camera/input contracts.
- Evidence: `build/evidence/editor-camera/final/`, with per-suite results and
  screenshots under each DPR directory. `identity.json` records matching source
  host scripts, preview and `dist` hashes, including the exact Wasm binary.

## Acceptance

Final results: **228 browser checks passed** across ten suite/DPR reports, with
empty recorded error lists. This includes 48 camera checks and 180 existing-editor
regression checks. The aggregate is recorded in `final/summary.json`.

`scripts/editor-camera-smoke.cjs` exercises actual DOM pointer/keyboard events and
the running Wasm editor on both renderers at both densities. Each run checks:

1. Disabled outline nodes focus their position.
2. Right-drag rotates around the current eye without object/history changes.
3. RMB + W flies while simulation is paused, preserves the selected transform
   tool, and stops on release.
4. S/A/D and Q/E move backward, sideways and vertically.
5. Shift accelerates flight.
6. Escape, window blur, text-field focus and busy transitions release held input.
7. Middle mouse pans without rotating.
8. Double-click frames the selected subtree visibly within Scene; Alt-drag orbits
   that new pivot. Focus screenshots were visually inspected.
9. Meshless nodes frame their position, F frames selection, and typing cannot
   trigger navigation.
10. Play navigation leaves the authoring camera unchanged; Stop restores exact
    authoring scene bytes.
11. Navigation works after floating and redocking Scene.
12. Save/reopen restores exact camera and orbit state.

The four existing editor suites remain regression coverage: `editor-layout-smoke`
(8 checks), `editor-smoke` (14), `editor-content-smoke` (14) and
`editor-structure-smoke` (9), per renderer/density. They exercise live transforms,
gizmos, selection, undo/redo, assets, save/load, component editing, reparenting,
Play/Stop, docking, layout persistence and narrow-screen controls.

## Issues resolved and limits

- Active traversal initially omitted disabled outline nodes. Saved-ID lookup now
  resolves those nodes; disabled/meshless targets frame their position.
- Gizmo hit surfaces can cover the canvas. Navigation capture is installed on
  their common parent and filters to canvas/gizmo targets, preserving left-mouse
  editing while allowing right/middle navigation there.
- A high-DPI suite startup stalled on missing asset delivery from the default
  Python test server. The isolated server was restarted with a larger request
  queue and bounded logging; failed startup output was not treated as acceptance.
- Navigation uses pointer capture, not pointer lock; movement is bounded by the
  browser screen. Q/E follows camera up. Flight speed uses parent-space units.
- Framing uses active descendant bounds; inactive targets use a small point
  radius. It retains viewing direction and may expand far clipping. It does not
  change object history/dirty revision; saved camera/orbit state still changes.
- These are desktop browser/native checks, not physical Android/iOS acceptance.

Both installed and CatGame engine-skill copies have matching camera contract and
validation additions, preserving their existing guidance; both skill validators
pass.
