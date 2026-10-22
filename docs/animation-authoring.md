# Animation libraries and state machine authoring

Select a `SkeletalMeshComponent` (or its owning actor) in the Scene Outline, then
open **Details → Animation library & state machine → Open animation editor**.
The selected mesh must already contain a skeleton and at least one clip. Import a
model normally first, or open `?scene=skeleton&editor=1` for the two-clip fixture.

## Organize clips

The left column lists clips by folder/name and filters names, folders and tags.
Display names do not change stable clip IDs or controller references. Default loop
and speed settings are used by clip preview and when assigning a clip to a state;
existing states retain their explicit playback settings. Sample slots are append-only:
sorting the library does not reorder runtime clips. Clip deletion is not exposed.

**Import clips** accepts an animated FBX/GLB model supported by the existing
importers. It retains the destination mesh and skin bindings, and imports all source
clips after validating the rig. Joint names must be unique (one unnamed synthetic
root is allowed), counts and parent relationships must match, and local bind matrices
must agree within `1e-4`. Source joint samples are remapped by name into destination
order. For compatible hierarchies with different proportions or bind poses, use
the explicit [Retarget clips](animation-retargeting.md) workflow instead.
Neither path is an arbitrary animation-only file decoder. Existing importer geometry,
image and sample budgets still apply. Missing companion images may produce importer
warnings; textures/geometry from the animation source are not retained by the library.

Clip markers use one line per marker: `seconds | name | payload`. Save markers
sorts them by time; runtime validation checks time bounds and marker limits. Both
contributors of a blended state may emit markers. Gameplay owns filtering and
responses. Root motion selects an explicit library joint; preview drains movement
without moving the actor. Gameplay must consume it and implement movement/collision.

## Author a controller

Add states, assign clips, choose looping/speed, and optionally blend a second clip
using a Number parameter and minimum/maximum range. Drag state cards to arrange;
one drag commits one library-history edit. Escape, pointer cancellation and blur
cancel an unfinished drag. **Frame graph** fits all states. The selection dropdown
also exposes every state/transition for keyboard operation.

Select a source state, choose **Connect**, then select a destination. On the arrow,
edit conditions, crossfade seconds, optional normalized exit time and priority.
All conditions on one arrow must match. The first eligible transition in the ordered
list wins; the player takes at most one transition per variable tick. Exit time 1
means one source cycle (the longer duration for a two-clip blend), checked at the
start of the next tick after the endpoint is reached. Empty conditions mean always
eligible once the optional exit time is reached. **Higher/Lower priority** changes
that order. Removing a state removes its incident edges and remaps remaining indices.
Referenced parameters cannot be removed until their conditions/blends are removed.

Parameters are Number, Boolean or Trigger. Use Boolean for persistent conditions
such as `Blocking`; gameplay clears it explicitly. Use Trigger for one-shot requests
such as `Attack`: it remains latched until a winning transition consumes it. The
**Attack example** replaces the document graph with Idle/Attack, an Attack trigger,
Speed and Blocking parameters, and a completion transition back to Idle. Its clips
are the first two library clips (or the first clip twice if only one exists); select
the appropriate character clips afterward. This is authoring example policy only.

## Preview, apply and save

**Preview graph** starts the existing isolated Editor Play scene and configures
the selected clone with the library. The animation window narrows so the main scene
remains visible. Live controls set typed parameters or fire triggers on the clone;
the graph highlights the active state and last winning edge. The display shows
playback seconds and marker names. **Preview clip** switches to manual clip playback;
scrubbing seeks and pauses it. **Preview graph** restarts controller preview.
**Stop preview** restores the original scene. Scene bytes, history and library
defaults are unchanged. Preview is not reconstruction of custom gameplay callbacks.

Edits/imports/application are disabled during Play. Source document unbinding
invalidates the target, and file-read completions are gated by the document/UI
generation. Closing stops this preview. Ordinary Play can also stop it. Preview
events and root motion are consumed for diagnostics, never sent to gameplay.

**Apply to mesh** performs one `EditorSession` command swapping staged animation
assets/playback in place while retaining component handles, saved IDs, settings,
children and skin bindings.
The prior complete playback state is parked for undo; redo restores the authored
configuration. Replay guards parent/transform and a serialized-state fingerprint
against external edits. Unsupported custom subtree codecs reject before publication.
The mesh, skeleton and player keep their original node identities and handles.

Save the **scene** to persist the applied mesh, clips, markers, controller, parameters
and playback state through existing scene codecs. Save **.weanim** separately to
retain library folders/tags/IDs, defaults, root selection and graph layout. Opening
a scene reconstructs a basic editor library from its applied clips/controller; it
does not recover separate library organization. Open the `.weanim` file to recover
that organization. Opening a library checks compatibility before replacing the
current authoring document; applying remains explicit. No automatic library autosave
or embedded library metadata in scene files is provided.

## Portable APIs and limits

`engine_core` provides `AnimationLibrary`, `AnimationLibraryData` and
`AnimationLibraryArchive` in `engine/animation_library.hpp`. `engine_editor` provides
`AnimationLibraryEditor` and `EditorSession::setAnimationLibrary`. The browser adapter
owns no authoritative animation state; DOM/SVG submits validated portable edits.

```cpp
auto library = engine::AnimationLibrary::fromSkeleton(mesh.mesh->skeleton);
auto authored = library->data();
authored.controller.parameters = {
    {"Attack", engine::AnimationParameterType::Trigger, false}
};
authored.controller.states = {
    {"Idle", 0, true, 1, {}}, {"Attack", 1, false, 1, {}}
};
authored.positions = {{60,80}, {400,160}};
authored.controller.transitions = {
    {0,1,.12,{},{{0,engine::AnimationComparison::Equal,true}}},
    {1,0,.15,1.0,{}}
};
library = engine::AnimationLibrary::create(std::move(authored));
// Configure detached/runtime-owned content; use EditorSession for authoring history.
library->apply(mesh);
mesh.animation().trigger("Attack");
// A Boolean parameter uses setParameter("Attack", true) instead, and stays set.
```

Use `clipIndex(stableId)` to resolve a library entry to the existing indexed player
API. Libraries/controller definitions/skeleton samples are shared immutable assets;
players own their independent clocks, poses, parameters and output. `apply` validates
compatibility and stages evaluation before publishing the copied mesh/skeleton;
call on the scene owner outside guarded traversal. It does not change game rules.
`lastTransition()` and `transitionCount()` are transient diagnostics, reset on
controller replacement/detachment and not archived. They are not an event queue.

Authoring limits: 64 clips, 256 joints, 1024 skin bindings, 32 MiB sampled poses;
256 states, 64 parameters, 1024 transitions and 64 conditions per edge. Names/labels/
folders/tags are at most 256 bytes with no control characters. Speed is `(0,100]`,
crossfades at most 3600 seconds, normalized exit at most 10000, graph positions
within ±10000. Runtime controller APIs retain their existing broader limits.

Library editor history defaults to 32 changes (configurable 1..128) and evicts old
entries to bound unique retained sampled poses to 128 MiB including the saved version.
Metadata edits share samples. `.weanim` retains the WEANIM1 envelope; ANIM 3
metadata (reading ANIM 1–2) wraps a checksummed scene archive containing
one skeleton/player plus bounded metadata, using the existing validated codecs.
Input is capped at 64 MiB, decoded allocations at 128 MiB and metadata at 512 KiB.
Malformed/unsupported files leave the current library and scene unchanged.

States and layers now support [1D/2D blend trees, phase synchronization and
masked override/additive composition](animation-layers.md). Arbitrary scripting,
any-state edges, hierarchical animation graphs, recursive trees and
clip trimming remain outside this interface. See [runtime controller semantics](animation-control.md) and
[validation](animation-authoring-validation.md).

[Inverse kinematics](inverse-kinematics.md) adds ordered limb/look constraints,
per-instance live targets, residual diagnostics and compatible library history.
