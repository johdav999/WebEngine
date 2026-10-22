# Animation authoring validation

Focused commands:

```powershell
cmake --build build/native-animation-library --target animation_library_tests animation_control_tests animation_tests root_motion_tests editor_content_tests scene_archive_tests
ctest --test-dir build/native-animation-library -R '^(animation_library|animation_control|animation|root_motion|editor_content|scene_archive)$' --output-on-failure
node scripts/editor-animation-smoke.cjs
```

Use the repository-local Emscripten version in `.emscripten-version` to build the
browser target. Serve the exact selected build with current `web/` adapters and
fixture/font assets. The browser script uses `DEMO_URL`, `BROWSER_EXECUTABLE`,
`NODE_PATH`, `EVIDENCE_DIR` and optional `DEVICE_SCALE_FACTOR` like other smoke scripts.

Native contracts cover metadata/sample round trips, checksummed corruption,
invalid references/IDs, rig rejection, sibling joint remapping, append-only stable
IDs, library import history, failure preservation, scene application/undo/redo,
trigger consumption/completion, typed inputs, saved playback and external-state guards.

Browser acceptance covers both WebGPU and WebGL2: actual forms, folder/tag search,
marker authoring, invalid edits, state dragging/cancellation, connections/history,
Attack trigger preview and exact winning edge, live Number/Boolean values, clip
scrubbing, unchanged source scene hashes/history, same-rig FBX clip import,
library download/reopen/malformed rejection, scene apply/undo/redo/reopen and
compact viewport layout. Evidence/results are written under `build/evidence/editor-animation`.

Verified on 2026-10-19:

- All six focused native suites passed in the isolated `build/native-animation-library`
  build: animation_library, animation_control, animation, root_motion, editor_content,
  scene_archive. Tests include in-place mesh/skeleton/player identity preservation,
  guarded traversal rejection, per-character isolation, Boolean Attack and diagnostic reset.
- The repository-local Emscripten 6.0.11 Release target built in
  `build/web-animation-library`. The exact site was served on loopback port 8179.
- Chrome 155.0.8059.80 passed the complete script on WebGPU and WebGL2 at DPR 1
  ([results](../build/evidence/editor-animation/results.json)) and DPR 2
  ([results](../build/evidence/editor-animation-dpr2/results.json)), with no page or
  console errors. Both Boolean `Attack=true` and consumed one-shot triggers were
  exercised through UI controls and the actual C++ player. Screenshots were inspected
  for graph labels, visible posed geometry, fixed preview controls and compact layout.
- JavaScript syntax, changed-file whitespace, documentation links and both required
  engine-skill copies were checked. CatGame-specific dependency guidance was retained.

The shared `build/native` encountered concurrent output-file collisions; the accepted
native evidence comes from the separate build above. Desktop Chromium and DPR checks
do not establish physical Android/iOS behavior or mobile performance. Arbitrary external
rig compatibility and retargeting are not claimed.
