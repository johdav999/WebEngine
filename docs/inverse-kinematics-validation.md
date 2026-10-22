# Inverse kinematics validation — 2026-10-20

## Verified build

Windows native Debug with the repository's configured WinLibs compiler and its
matching runtime directory on PATH. Browser Release used the repository-local
Emscripten **6.0.11** and `web_assets`; the exact staged `dist` was served on
`http://localhost:8137`. Chrome **155.0.8059.85**, sequential browser runs.

Final staged SHA-256:

- `webengine.wasm`: `b7cca086fe23a3cb9ef0673cb1a2d7c0061572b45daa04b70ae6fe4478649a89`
- `asset_decode.wasm`: `d87b73bfaaeff73e17f71e9fc33167ff2666810f0af9185f19165b21a2b16d8e`

## Native and Wasm

Nine focused CTest suites passed: `ik`, `animation_layers`, `animation_control`,
`animation_library`, `animation`, `root_motion`, `scene_archive`, `persistence` and
`editor_content`. The IK suite also passed compiled as `ik_wasm_tests.js` in Node.

Coverage includes reachable limbs, weighted poses, tip orientation, unreachable
inner/outer limits, coincident targets/poles, rotated/scaled parents, bounded and
antiparallel look-at, invalid input atomicity, zero-length segments, nonuniform
scale rejection, world/model conversion, pre-IK pose inspection, independent
live targets, paused edits preserving clocks/output, root-motion isolation,
manual fade continuity, skeleton replacement, scene/library roundtrips, reordered
joint mapping, ANIM2 compatibility, editor apply/undo/redo, real sloped-collider
raycasts/filtering and the full demo's physics registration/archive support.

The broader persistence suite required updating its expected current archive byte
from 32 to 33. An initial browser attempt caught the demo's pads nested beneath
an actor, violating the existing scene-root physics-body contract; pads now use
scene-root actors, with a native demo regression. Review also caught and fixed
manual crossfade double root stripping of the outgoing IK pose.

## Browser acceptance

`ik-smoke.cjs` passed WebGPU and forced WebGL2 at DPR1 and DPR2, including a
1500x1050 viewport and compact 900x760 authoring layout. It checks the actual
reported backend, both ground elevations, hand/look authoring, invalid edit
rejection, library history, paused live targets, reachable residuals, unreachable
limit reporting, source isolation, library save/reopen, scene apply/undo/redo/reopen
and fixed Close accessibility. Rendered scene pixels changed when IK weight was
toggled on the paused rig, proving that target controls affect skinning.

- DPR1: webgpu: 8967 changed scene pixels; grounded feet, reach/look controls, validation, paused targets, residuals, source isolation, library and scene roundtrips, undo/redo, compact layout
- DPR1: webgl2: 11747 changed scene pixels; grounded feet, reach/look controls, validation, paused targets, residuals, source isolation, library and scene roundtrips, undo/redo, compact layout
- DPR2: webgpu: 35647 changed scene pixels; grounded feet, reach/look controls, validation, paused targets, residuals, source isolation, library and scene roundtrips, undo/redo, compact layout
- DPR2: webgl2: 46925 changed scene pixels; grounded feet, reach/look controls, validation, paused targets, residuals, source isolation, library and scene roundtrips, undo/redo, compact layout

Existing `editor-animation-smoke.cjs` and `animation-layers-smoke.cjs` also passed
on both backends at DPR1. These cover markers/transitions, clip import/scrub,
blend-tree and masked/additive-layer controls, rendered layer differences, preview
isolation, archives and history. The first regression launch used that older
script's default port 8179; it was rerun against the explicit served port 8137.
All final runs reported zero page/console errors.

Evidence: `build/evidence/ik`, `build/evidence/ik-dpr2`,
`build/evidence/ik-animation-regression`, `build/evidence/ik-layers-regression`.
Native log: `build/ik-native-results.log`. Preview and compact screenshots were
visually inspected; live numbers are rounded for display, controls remain readable
and the scene shows both grounded feet, reaching arm and oriented head.

## Documentation and boundaries

Architecture, feature/persistence contracts, README, the installed engine-skill
and the CatGame copy were updated. Both copies contain the IK reference and retain
CatGame's pinned dependency guidance. No consumer engine pin was advanced.

These are desktop Chromium and Wasm checks, not physical Android/iOS acceptance.
The fixture is a block rig, not production-character gait-quality validation.
Ground targets are probed when the demo is built; moving-platform anchoring,
contact phase/locking, pelvis offsets and smoothing remain explicit application
policy. Anatomical joint limits, full-body solving and retargeting are not supplied.
Only positive uniform scales are supported. Existing unrelated checkout changes
were preserved; no commit or deployment was made.
