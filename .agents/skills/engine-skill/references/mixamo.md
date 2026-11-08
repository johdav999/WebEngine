# Mixamo character preparation and clips

Read the selected engine revision's `docs/mixamo.md` and
`docs/mixamo-validation.md`; verify the consumer pin before using these APIs.

Rigged FBX and Blender GLB use normal mesh import. `FbxImporter::loadAnimationsMemory`
and Windows `loadAnimationsFile` decode bone hierarchy and clips without requiring
geometry or reading textures. Meshless sources use authored default transforms as
rest, not an inferred T-pose. Defaults: 32 MiB file/poses, 128 MiB per parser
allocator, 30 Hz inclusive endpoints; limits 256 joints, 64 clips, 1–120 Hz.

`matchMixamoJoints(source,target)` is an explicit naming preset for canonical body
and finger names, optional namespace and mixamorig prefix. It rejects duplicate
aliases, absent Hips and incompatible mapped ancestry. Hips uses ScaledDelta;
other mapped joints retain bind offsets. Review `estimateRetargetScale` and then
use `AnimationLibrary::appendRetargeted` and idle-owner apply. Destination mesh and
inverse binds remain unchanged. Exact-name matching and strict append are unchanged.
Positive uniform static scales remain required by retargeting.

Browser authoring: Retarget clips → source FBX → Match Mixamo joints → review →
Bake → preview → Apply. Windows consumers use the same APIs and existing editor
assignment; no specialized native graphical panel was added. Profiles, libraries
and scenes retain existing storage formats. No callback or frame ownership changes.

The separately installed `mixamo-rigging` skill (source in engine
`tools/mixamo-rigging`) uses isolated headless Blender to bind a supplied fitted
Mixamo armature to an unrigged mesh. It accepts explicit world-space bone endpoints,
generates automatic weights, normalizes four influences, rejects unweighted
vertices, and stages GLB/blend/report without replacing existing outputs. A Rest
clip enables initial animation-library authoring. Anatomical placement and weight
cleanup need visual review; this is not Adobe's auto-rigging service.

Validation: existing `animation_retarget_tests` and `animation_retarget_wasm_tests`
include the original synthetic meshless FBX. Use repository-local Emscripten pinned
in `.emscripten-version`; see README for native builds. Blender fixture generation
and roundtrip use `tests/blender_mixamo_fixture.py`. Distinguish synthetic coverage,
real character acceptance and missing platform/toolchain evidence.
