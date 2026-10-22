# Animation composition validation — 2026-10-20

Implemented contracts are described in [animation-layers.md](animation-layers.md).
Validation used the existing shared checkout without removing unrelated work.

## Native and Wasm

The configured WinLibs native Debug build passed all eight focused CTest suites:
`animation_layers`, `animation_control`, `animation_library`, `animation`,
`root_motion`, `scene_archive`, `persistence`, and `editor_content`.

The new composition suite covers adjacent 1D interpolation, clamping/extreme
inputs, unsorted samples, exact/diagonal/collinear 2D weights, normalization outside
the convex hull, phase synchronization and independent clip clocks, weighted
markers, root-motion intervals and anchoring, masked limbs, ordered override/
additive poses, additive rotation/scale/reference subtraction, explicit pause,
completed-base overlays, layer restart, clock overflow, skeleton replacement,
manual crossfades from the composed pose, invalid candidate preservation, same-rig
mask remapping, exact scene round trips, library metadata, legacy ANIM 1 defaults,
and scene Apply undo/redo.

Repository-local Emscripten **6.0.11** compiled Release `web_assets` and the explicit
`animation_layers_wasm_tests` target. The same composition suite passed in Node on
the Wasm ABI. `web_assets` staged both the main engine and matching asset decoder.

```powershell
cmake --build build/native --target animation_layers_tests animation_control_tests animation_library_tests animation_tests root_motion_tests scene_archive_tests persistence_tests editor_content_tests -j 8
ctest --test-dir build/native -R '^(animation_layers|animation_control|animation_library|animation|root_motion|scene_archive|persistence|editor_content)$' --output-on-failure
cmake --build build/web-release --target web_assets animation_layers_wasm_tests -j 8
node build/web-release/animation_layers_wasm_tests.js
```

Use the configured compiler's matching runtime directory on PATH for native tests.
Follow the README's local SDK configuration before building browser targets.

## Browser acceptance

Chrome **155.0.8059.85** passed `scripts/animation-layers-smoke.cjs` on forced
WebGPU and WebGL2 at DPR 1 and 2, including:

- 1D/2D sample creation and editing; rejection of duplicate coordinates.
- Layer names/modes, subtree masks, invalid weights/masks and numeric inputs.
- Visible additive pose changes in the scene region, excluding editor controls.
- Preview pause and isolated layer restart while retaining the base clock.
- Exact source-scene isolation, library save/open, Apply undo/redo and scene reopen.
- Scrollable controls and reachable Close at 900×760.

The final normal-scale run measured 4,013 changed scene pixels on WebGPU and 6,190
on WebGL2 when toggling the layer at a fixed base time. At DPR 2 the counts were
18,214 and 24,744. Counts vary with the sampled overlay clock; the test requires a
visible pixel change, not a specific count.

The existing `scripts/editor-animation-smoke.cjs` also passed both backends at
DPR 1 and 2, covering Boolean/Trigger control, marker editing, drag cancellation,
preview/source isolation, same-rig import and persistence. Final runs reported no
console errors. One earlier run completed its assertions but encountered Chrome
`ERR_NO_BUFFER_SPACE`; a fresh sequential run passed with the same strict error
checks. No resource errors were filtered.

Evidence:

- `build/evidence/animation-layers/results.json`
- `build/evidence/animation-layers-dpr2/results.json`
- `build/evidence/animation-layers-editor-regression/results.json`
- `build/evidence/animation-layers-editor-dpr2/results.json`

Screenshots beside these records cover authoring, preview and compact layouts.
The tested `dist/webengine.wasm` SHA-256 is
`EC1ADBF6324704DD6D58E6CB49DE185B6AFF6D03436534D9CA09D1AE2471B52A`.

These are synthetic skeletal-fixture and desktop Chromium checks. They do not
establish authored humanoid foot-contact quality, physical Android/iOS acceptance,
or sustained mobile performance. The full unrelated repository suite was not run.
