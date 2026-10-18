# Scene editor content-authoring validation

Recorded 2026-10-15. This record covers the second editor increment on desktop Chrome.

## Selected snapshot

- Pinned Emscripten `6.0.11`; Release target `webengine` in
  `build/web-editor-release`.
- Isolated site: `build/editor-content-browser-site`, served on `http://localhost:8164`.
- Browser: Chrome `155.0.8059.79`. Both forced backends were confirmed from
  the initialized renderer.
- Wasm SHA256: `17858622aa9c96479238f625922de5d32fee03dfb079c495a9ec869a3cb74760`.
- `build/evidence/editor-content/build-identity.json` records all host/script/Wasm/
  worker and integration-source hashes. Every served file was fetched over HTTP and
  checked against that identity. `verified-main.cpp` and `verified-scene-archive.cpp`
  retain integration snapshots. No unrelated shared-checkout work was reset.
- Native scene fixture format: 13; browser-authored scene format:
  14. These versions came from concurrent engine changes; the editor
  itself adds no file-format version.

## Checks completed

| Coverage | Result | Evidence |
| --- | --- | --- |
| Native portable logic | 4/4 CTest suites passed: editor, editor_content, scene_archive, persistence | `build/evidence/editor-content/native-tests.txt` |
| Content browser flows | 14 checks on WebGPU and 14 on WebGL 2 | `build/evidence/editor-content/results.json` |
| High-DPI content flows | Same 14 checks per backend at device pixel ratio 2 | `build/evidence/editor-content/dpr2/results.json` |
| First-increment input/transform regression | 14 checks per backend | `build/evidence/editor-content/transform-regression/results.json` |
| Existing archive/browser regression | 8 checks passed outside editor mode | `build/evidence/editor-content/archive-regression/results.json` |
| Skill and syntax | Installed engine-skill validator, JavaScript syntax and focused whitespace checks passed | Tool diagnostics |

Native tests cover typed property validation/undo and external-write rejection,
per-instance material/texture history and source immutability, fresh duplicate
saved identities and shared immutable rigs/resources, copied independent playback
state, creation detach/reattach and runtime handle renewal, clone budgets/protected
roots, library import history, invalid import preservation, grid/angle snapping,
placement under transformed scene roots, and independent duplicated physics bodies.

Browser tests use actual native scene/library fixtures, real GLB/FBX/image decoders,
actual inspector controls, Ctrl+D and real pointer gestures. They verify source
import undo/redo, malformed-file preservation, asset placement/drop and grounding,
rigid-body/light/independent-camera fields, protected navigation camera settings,
world-grid and angle snapping, per-slot material/UV/shader/texture changes, real
rendered-pixel changes, and exact reset/undo restoration. The test does not mock
scene state or rendering.

Autosave is verified through real IndexedDB transactions and a fresh page reload.
A simulated storage-quota rejection leaves the dirty edited scene and previous
committed recovery intact. The recovered scene produces **zero differing canvas
pixels**, retains component/material data, source labels and snap settings, and is
marked unsaved to a separate file. A malformed recovery record preserves the active
scene. Separately downloaded scene files retain their older explicit checkpoint
independently from the newer browser recovery snapshot.

The old transform suite disables autosave while isolating gesture assertions;
autosave is covered by the content suite. The existing archive suite also passed
its physical scene/library downloads, embedded WAV, exact material/camera/animation
pixels, malformed data, fresh-page reopen and narrow layout checks.

## Review artifacts and operational notes

Screenshots include material inspectors, edited/recovered canvas frames, and
workspace views for both backends and pixel ratios. The desktop workspace screenshot
was visually inspected. All browser runs recorded no JavaScript/console errors.

Port 8163 was found serving the prior editor site. Acceptance moved to port 8164
and checked actual served identities before continuing. Concurrent terrain work
advanced native/archive contracts; browser executable and worker were rebuilt
and staged together before interchange validation. The selected snapshot is pinned
above; unrelated concurrent source changes after it were: none in the recorded integration sources.

## Scope

The asset catalog exposes meshes/textures; other imported library resources are
retained without specialized placement controls. Placement uses world Y=0 rather
than arbitrary surface raycasts. Supported component inspectors are listed in
[scene-editor.md](scene-editor.md); advanced animation/particle/terrain/water/shape
inspectors, arbitrary component/hierarchy mutation, multi-selection, scale gizmos,
prefab definition authoring and isolated Play/Stop remain future work.

Autosave is one origin-local recovery slot, with explicit recovery and successful
transaction acknowledgement. Interrupted/uncommitted writes, storage clearing and
quotas limit recovery. Existing WebGL 2 lighting/shadow capability reductions still
apply. Physical mobile devices, other browsers, production hosting and sustained
performance were not tested.
