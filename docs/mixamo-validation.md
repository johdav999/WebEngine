# Mixamo validation — 2026-10-25

Original synthetic fixtures contain no Adobe/Mixamo character or animation data.
Unrelated checkout changes were preserved. No game dependency pin changed.

## Portable and browser checks

Built `animation_retarget_wasm_tests`, `webengine` and `web_assets` with repository
Emscripten **6.0.11**, existing Release `build/web-release`. Used the SDK's
`python/3.13.3_64bit` on process-local PATH and as EMSDK_PYTHON, and workspace
`build/mixamo-emcc-temp` as EMCC_TEMP_DIR. The system Python alias failed startup;
the separate embedded `.tools/python` could not resolve Emscripten modules.
Neither failure required global environment changes.

`node build/web-release/animation_retarget_wasm_tests.js` passed. It includes the
existing retargeting suite and original meshless FBX in `tests/mixamo_fixture.hpp`:
bone ancestors, sampled hip translation, 31 samples over one second, namespaced
Mixamo mapping, Hips translation beneath a synthetic root, duplicate alias and
invalid size/sample/budget rejection. Ordinary mesh import still rejects meshless
input. The animation-only path does not invoke the supplied texture resolver.

`scripts/mixamo-import-smoke.cjs` passed in Chrome **156.0.8078.47**, desktop
headless WebGPU and forced WebGL2, against the rebuilt app at localhost:8147:

- Import the GLB produced by the Blender helper; open its Rest clip in the editor.
- Load meshless FBX, apply Mixamo preset, verify Hips translation and two mappings.
- Corrupt replacement preserves the source/mapping; bake supports undo/redo.
- Preview the baked animation and apply it to the character.
- Actual canvas movement: **11,835 WebGPU** and **11,007 WebGL2** changed pixels.
  These counts are run evidence, not performance claims or fixed requirements.
- No page exceptions. Results/screenshots: `build/evidence/mixamo/`.

The existing `animation-retarget-smoke.cjs` also completed all behavioral checks
on both backends (477/2,513 changed destination pixels), including old FBX/GLB,
profiles, archives, history and previews. Its overall assertion failed on two
unrelated HTTP 404 console messages: the optional multiplayer launch probe
`HEAD /multiplayer/index.html` has no built sample in this dist. This suite is
therefore **not recorded as a clean pass**. Evidence is retained in
`build/evidence/mixamo-retarget-regression/`.

## Headless Blender

Blender **5.1.2** ran in separate background processes, outside the sandbox after
sandbox execution exited without output. The live Blender session was not used.
`tests/blender_mixamo_fixture.py` creates an original two-bone armature and a
242-vertex elongated sphere. Both `--aligned` and explicit `--fit` endpoint paths
exported GLB, editable blend and JSON diagnostics with no unweighted vertices.

Roundtrip GLB validation retained normalized weights, armature and Rest animation.
Of 992 exported seam vertices, **648 moved** under a 35-degree spine bend. This
caught and fixed a real dependency-graph issue: newly linked blend objects need
evaluation before capturing matrix_world, otherwise mesh placement is lost.
Earlier failed exports remain isolated under `build/mixamo-blender/prepared`;
the validated corrected export is `prepared-fixed`, and endpoint output is
`prepared-fit` under the same directory.

Reproduction (choose a fresh output directory):

```powershell
& $blender --background --factory-startup --python-exit-code 1 --python tests/blender_mixamo_fixture.py -- build/mixamo-blender
& $blender --background --factory-startup --python-exit-code 1 --python tools/mixamo-rigging/scripts/bind.py -- --mesh build/mixamo-blender/mesh.blend --rig build/mixamo-blender/rig.blend --aligned --output build/mixamo-blender/prepared-new
& $blender --background --factory-startup --python-exit-code 1 --python tests/blender_mixamo_fixture.py -- build/mixamo-blender prepared-new
```

Python syntax, CLI help, JavaScript syntax and skill-creator validation passed.
The installed rigging script matches the repository source by SHA-256. Required
Johan installed-engine and CatGame skill references were updated and hash-checked;
their existing project guidance was preserved. The active EJOHDAV engine-skill
and repository skill copy were updated too.

## Windows and asset limitations

The shared portable implementation and native `loadAnimationsFile` byte-reading
wrapper are delivered. Native Windows compile/tests/rendering **could not run**:
`cmake --build build/clothing-native --config Release --target
animation_retarget_tests animation_tests fbx_importer_tests -j 4` fails because
the configured Visual Studio 18 2026 generator cannot find any installed instance.
The same failure occurred outside the sandbox. Wasm/browser success is not native
Windows acceptance; rerun these targets and native rendered-character checks when
the toolchain is available.

No user character or downloaded Adobe animation was supplied. Synthetic coverage
does not establish production anatomical fit, finger/face quality or contact
correction. Real character poses and source rest transforms still need inspection.
Physical mobile-device acceptance was not performed.
