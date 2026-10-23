# Native animated mesh rendering

Native Dawn and direct D3D12 use the same `SkeletalMeshComponent`,
`SkeletonComponent`, `AnimationComponent` and `MeshComponent` contracts. Direct
D3D12 milestone 4 adds GPU linear skinning and cached morph vertex streams to
its conventional material/color/shadow paths. There is no second scheduler or
renderer-specific animation API. Browser WebGPU retains GPU skinning; WebGL2
retains its existing CPU deformation and per-draw dynamic vertex uploads.

## Use existing resources and playback

```cpp
#include "engine/skeletal_mesh_component.hpp"

auto& character=scene.spawn("Character").addComponent<engine::SkeletalMeshComponent>("Mesh",asset);
character.animation().play(0);         // The Application advances Scene once.
character.setMorphWeight("Stretch",.5f);
character.animation().pause();
character.animation().seek(.8);        // Pose controls remain usable while paused.
```

`asset` is a shared immutable Mesh from FBX/GLB import or validated authoring.
Each component owns its playback, palette, morph weights and overrides. Sharing
an asset does not share those instance values. Clip transitions, root motion,
[layers/blend trees](animation-layers.md), [IK](inverse-kinematics.md) and
[retargeted clips](animation-retargeting.md) feed their existing portable pose
evaluation. The renderer consumes final bone/morph revisions and never advances
animation or applies root motion to actors. Applications retain movement policy.

Morph targets add mesh-local position and optional normal/tangent offsets before
skinning. Finite negative and extrapolated weights retain the shared contract.
Unchanged weights reuse the CPU pose and GPU vertices; all-zero weights use the
base vertex stream. Skeletal-only animation changes palettes without reuploading
mesh vertices. Pause, unchanged poses and repeated views/shadow passes produce
no redundant direct-backend pose uploads.

## Rendering and lifetime

Immutable vertex/index/joint/weight streams belong to mesh caches. Direct D3D12
uses four joint influences, a read-only structured `BoneUniform` palette and
separate static/skinned vertex entry points. Both bone transforms and inverse-
transpose normal matrices are consumed, with skinned tangent/bitangent axes
preserving handedness through nonuniform and mirrored component placement.
Morph vertices replace only the base vertex stream; indices, skin influences
and materials stay shared. The same deformation is used for shadow depth and
color, including cutout texture/UV/opacity rules.

PBR/unlit maps, transparent/additive ordering, sidedness, directional/cascade/spot
shadows, fog, HDR composition and MSAA follow [backend contracts](windows-d3d12.md).
Animated bounds include both morphing and skinning and feed the same camera and
shadow culling. Deformed meshes keep full source geometry under RenderFrame's
existing static-LOD restriction. Culling never controls animation clocks.

Weak pose identities key the private GPU cache. A changed revision stages bytes
into the current fenced upload arena and copies them to a reusable default-heap
buffer, ordered by resource barriers. Old buffers survive until a submission
fence completes; resizing an arena waits for its frame fence. Replaced/removed
camera lists clear collected frame references/LOD history, and unbinding releases
collected CPU resources. Expired pose buffers retire on the next rendering frame.
None of these caches owns a scene or component. Device recovery recreates these GPU caches from the retained source poses; see
[recovery ownership](windows-d3d12-recovery-performance.md).

## Bounds and diagnostics

Limits are 1024 palette entries (including identity slot zero), four influences
per vertex, one skin entry per source vertex and one morph vertex per source
vertex. Source Skeleton validation retains its 256 hierarchy-joint limit. Invalid
palette indices/sizes, nonfinite skin weights/matrices and malformed morph data
reject before command recording. Published mesh/pose data follow the existing
immutability/revision rules; mutating them without the owning API is unsupported.

Each of the skin and morph caches caps at 4096 identities. Pose staging shares
the existing 64 MiB per-frame upload limit with object/view/GUI data; this is a
failure bound, not a guarantee that a 64 MiB morph fits alongside everything else.
Existing mesh/draw/descriptor limits also apply. Active pose buffers are not an
automatic global GPU memory/LRU budget. No performance improvement is claimed.

Direct `diagnostics().renderState` reports milestone 8, `skinning` and
`morphTargets`; `performance.counters` adds `skinnedDrawCalls`, `morphDrawCalls`,
`skinUploads`, `morphUploads`, their byte counts and cached pose counts. Draw counts
include color and shadow submissions. Upload counts represent changed poses once
per revision, independent of the number of views/passes. They are diagnostic
counters, not GPU timestamps or a detailed profiling timeline.

## Try and validate

```powershell
./scripts/build-windows.ps1
./build/windows/Release/webengine_animation_windows.exe --backend d3d12
./build/windows/Release/webengine_animation_windows.exe --backend dawn
$env:WEBENGINE_D3D12_VALIDATION='1'
./scripts/windows-animation-smoke.ps1 -EvidenceDir build/evidence/windows-animation-new
```

The example loads a bundled imported two-clip rig, shows independent players and
a morph-only object, and supplies retained playback/blend/pose/morph/shadow/fog/
HDR/MSAA controls. Pose scrubbing pauses both players and seeks the first.
F11 toggles borderless fullscreen; Escape exits. `--capture image.ppm` makes a
deterministic paused capture including GUI and exits; its parent must exist.
The build stages `assets/animation.fbx` alongside the other demo assets. Direct-
only builds work with `--backend d3d12`; the default selection remains Dawn.

See [milestone 4 evidence](windows-d3d12-milestone4-validation.md). Run focused
native animation/morph/IK/retargeting/RenderFrame tests and relevant browser
`morph-smoke.cjs` with `BACKEND=webgpu|webgl2`, `skeletal-animation-smoke.cjs`
(WebGPU palette-upload contract) and `animation-layers-smoke.cjs` (both backends).
Desktop browser/native acceptance does not establish physical mobile coverage.

Milestone 5 also supports layered materials on these deformed meshes, including
normal/height blending, world projection and cutout shadows. See [terrain](terrain.md).

Milestone 6 adds water displacement after deformation and animated sources in
planar reflections. See [water](water.md) and [milestone 6 evidence](windows-d3d12-milestone6-validation.md).

Milestone 8 now supports shared-pose instancing, semantic BC7 uploads and bounded
device recovery; see [contracts](windows-d3d12-recovery-performance.md). This does not implement GPU
clip evaluation, dual-quaternion skinning, cloth, ragdolls or new import formats.
