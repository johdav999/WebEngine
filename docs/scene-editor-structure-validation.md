# Scene editor structure and Play validation

Recorded 2026-10-15 against desktop Chrome 155.0.8059.79. This is the third editor
increment: supported component creation/removal, hierarchy reparenting, ordered
multi-selection, scale gizmos and isolated Play/Stop.

## Tested artifact

- Pinned Emscripten 6.0.11, Release `webengine` target in `build/web-editor-release`.
- Isolated site `build/editor-structure-browser-site`, served at `http://localhost:8165`.
- Wasm SHA256 `6648c1ecffe39cac7b37bddd261c7013a277a613b458c397391d4f14ec145b36`.
- `build/evidence/editor-structure/build-identity.json` records served HTML/scripts/
  styles/Wasm/worker and integration-source hashes. All served files were fetched
  over HTTP and matched byte-for-byte. Main/archive integration source snapshots
  are retained beside the identity record. Other preview ports were preserved.
- The native format-13 fixture opens in the matched format-14 browser/worker build.
  This editor increment adds no archive disk-format version.

## Results

| Check | Result | Evidence under `build/evidence/editor-structure/` |
| --- | --- | --- |
| Native portable suites | 5/5 pass: editor, editor_content, editor_structure, scene_archive, persistence | `native-tests.txt` |
| New native structure/runtime suite | 64 checks pass | `native-tests.txt` |
| New browser workflows | 9 checks each on forced WebGPU and WebGL 2, DPR1 | `results.json` |
| High-DPI workflows | Same 9 checks per backend, DPR2 | `dpr2/results.json` |
| Earlier transform/gizmo editor | 14 checks per backend | `transform-regression/results.json` |
| Earlier content/import/autosave editor | 14 checks per backend | `content-regression/results.json` |
| Ordinary archive workflow | 8 checks pass | `archive-regression/results.json` |
| Documentation/skill/source checks | Installed and CatGame skill validators, local links, JavaScript syntax and focused whitespace checks pass | Tool diagnostics |

The browser suite uses native fixtures and actual inspector controls, modifier/range
clicks, hierarchy drag/drop, parent chooser and pointer-driven scale/move handles.
It checks component creation/configuration/removal and identity restoration, batch
removal, grouped move/duplicate history, KeepWorld reparenting, cycle rejection,
local-axis and uniform group scale, illegal physics-scale/topology preservation,
and narrow-width accessibility of the tool controls. Source/runtime state is never
mocked. Every browser suite records no JavaScript or console errors.

Play runs copied physics while the source Application remains paused. Stop and
repeated Play/Stop preserve **byte-identical authoring scene files**, **zero differing
rendered canvas pixels**, transforms, selection, file dirty state and undo/redo
counts. Orbit navigation during Play affects only the copied camera. File capture,
imports and recovery controls are gated while playing. Native coverage additionally
proves independent animation playback, shared immutable assets, fresh scene-scoped
handles, host-suspension baseline handling, failed custom-node staging, physics
topology/scale rejection, and cancellation preserving later external writes.

The rendered-pixel helper hides only the SVG editor overlay during capture:
switching Move to Scale changes its handles even when scene pixels match. Initial
review also found and fixed uniform-handle pointer-event specificity and a browser
guard which had treated body transform locking as component-removal locking.
Raw canvas comparisons and the complete final suites were rerun after the fixes.

Desktop and narrow workspace screenshots, before/after/recovered canvas captures,
and DPR2 equivalents are retained in the evidence directories. Desktop and narrow
screenshots were visually inspected. The new local preview remains served on 8165.

## Scope and continuation

Component creation exposes nine built-in types with valid defaults and typed mesh
assets. Advanced asset/factory configuration remains future work. Removal currently
targets components, not arbitrary actors/nodes. Reparent preserves world TRS and
original sibling order through undo; a separate sibling-reorder tool is not exposed.
Selection uses hierarchy/viewport modifiers and hierarchy ranges, not marquee.
Physics keeps direct top-level actor topology and unit scale. Group nonuniform
scaling that would require shear retains the previous valid pose; uniform scaling
is available separately.

The isolated preview runs built-in physics/animation/effects and its own navigation/
sky/water clocks. Game/host executable callbacks, input bindings, joints, GUI, Web
Audio voices and demo-specific motion policies require explicit reconstruction.
Unsupported custom classes fail before publication. Stop discards runtime edits;
there is no apply-back action. Physical mobile devices, other browsers, production
hosting and sustained performance were not tested.

See [editor APIs and controls](scene-editor.md) and [architecture](../architecture.md).
