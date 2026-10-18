# Selection and interaction

`InteractionComponent` opts an actor into selection/focus and supplies actions.
`Scene::selection()` lazily creates the scene's `SelectionSystem`. The public
headers are `engine/interaction_component.hpp` and `engine/selection.hpp`.
Selection and focus each hold one generation-checked `ActorId`; queries never
return pending-destroy, disabled, detached or foreign targets.

```cpp
#include "engine/selection.hpp"
#include "engine/mesh_component.hpp"

auto& chest = scene.spawn("Chest");
chest.addComponent<engine::MeshComponent>("Chest mesh", engine::Mesh::cube());
auto& interaction = chest.addComponent<engine::InteractionComponent>();
interaction.addAction({"open", "Open",
    [](const engine::InteractionContext& context) {
        const auto* target = context.scene.findActor(context.target);
        return engine::ActionEligibility{!target->hasTag("locked"), "Chest is locked"};
    },
    [](const engine::InteractionContext& context) {
        context.scene.findActor(context.target)->addTag("opened");
        return engine::ActionResult{};
    }});

auto& selection = scene.selection();
const auto listener = selection.subscribe([](const engine::SelectionChange& event) {
    // Update highlight/inspector using event.kind, previous, current and reason.
});

// After input.beginFrame(), with the active camera and its actual backing-pixel
// viewport (not CSS coordinates). Applications own camera/view choice.
selection.processInput(input, camera, camera.pixelViewport(targetWidth, targetHeight));

// Explicit ray controllers (e.g. gaze or XR) use the same object resolver.
if (const auto hit = selection.resolveRay({rayOrigin, rayDirection})) {
    selection.select(hit->actor);
}

// Populate an action menu, including disabled options and their reasons.
const auto options = selection.availableActions(selection.selected(), playerId);
if (!options.empty()) {
    const auto result = selection.execute(options.front().handle, playerId);
    // A displayed enabled action can become ineligible before execution.
}
selection.unsubscribe(listener);
```

## Picking contract

Picking chooses the nearest participating actor along a finite ray. Direction is
normalized, so distances and `SelectionQuery::minDistance/maxDistance` use world units.
Transformed local AABBs support nested meshes, rotation, negative/nonuniform scale,
morphs and skeletal poses. Meshes use the same interpolated physics transforms as
rendering. A zero-scale or nonfinite transform is skipped. A ray starting inside
a bound hits at distance zero. Equal-distance ties retain hierarchy traversal order.

Bounds picking is the compatible default: gaps inside a mesh AABB can hit.
Set `SelectionQuery::geometry=SelectionGeometry::Triangles` for CPU triangle
picking of original mesh index ranges, including morph deformation, skinning and
the same interpolated model matrix as rendering. Triangle picking is two-sided,
skips degenerate faces, and returns a surface point, world geometric normal,
index-buffer triangle offset (`triangle`), barycentric weights and interpolated UV.
Bounds/proxy hits have no triangle and zero surface attributes. A ray inside a
closed mesh hits its exit surface in triangle mode. Mesh parts restrict tested
indices; LOD does not alter the query geometry.

Set `SelectionQuery::occlusion=true` to reject a target hidden behind nearer visible
mesh triangles, including meshes without interaction providers or actor owners.
Occlusion is independent of selection/focus eligibility and selection masks;
query/camera visibility masks and the additional `occluderMask` visibility-group
filter apply to blockers. The picked actor's own geometry does not occlude it.
Proxies remain bounds even in triangle mode and do not themselves block rays.
Nonzero-opacity translucent meshes block geometrically; texture alpha, shader
displacement, material culling and ordered render-layer depth composition are not
evaluated. This is a geometric CPU query, not GPU-ID/pixel-accurate picking.

Both options are per-query and off by default; no archive migration is required.
Triangle testing is linear in candidate geometry after bounds rejection, with no
BVH cache; applications choose when to repick and their mesh budgets. Child
actors require their own interaction provider; nested components belong to their
nearest actor. Mesh visibility and query/camera visibility masks apply; meshes with
zero opacity and disabled subtrees are ignored. LOD and shadow detail never change
the selected actor.

Set `proxyBounds` to a valid `MeshBounds` in the interaction component's local
space to replace mesh picking, including for invisible trigger/action objects.
Proxy picking uses the interaction `selectionMask`; it is independent of mesh
visibility masks. `selectable` and `focusable` are independent. Masks only filter
queries; changing a mask does not clear an explicit selection.

`resolveRay` and `resolvePointer` do not change state. `select`, `focus` and `clear`
are explicit state APIs. Invalid nonempty handles are rejected without replacing
the current state; an empty handle clears it. Pointer coordinates outside the
supplied viewport or an inactive/foreign-scene camera produce no hit. Pointer
queries also respect the camera's near/far clipping planes. Malformed
rays or pixel viewports throw `invalid_argument`.

`processInput` consumes the shared ordered pointer and Select action packets:
hover updates focus; a Select pulse picks at the latest pointer position; empty
space clears selection. Pointer movement/drag alone does not select. CancelAll
and cancellation of the current pointer clear focus while preserving selection.
A stationary pointer is picked again on subsequent frames, so camera/object
motion updates focus. The browser input adapter decides which GUI/drag/touch events
become scene packets. Call this consumer only for the active input context/view;
clear focus on view changes, pointer leave or GUI capture when the host emits no
cancellation packet. Ray/gameplay controllers can set focus explicitly.

## Dragging and rotating objects

`engine/manipulation.hpp` supplies an optional application-owned
`ManipulationController`. It borrows a scene which must outlive it, uses actor and
component handles, and owns one session without callbacks/subscriptions. It writes
authoritative actor transforms through the existing validated TRS API. Rendering
interpolation, LOD, culling and picking never become simulation movement policy.

```cpp
#include "engine/manipulation.hpp"

engine::ManipulationController drag(scene); // Keep alive across input frames.
engine::SelectionQuery query;
query.geometry = engine::SelectionGeometry::Triangles;
query.occlusion = true;
// On the application's drag-start command, using its active camera/backing viewport:
const auto ray = camera.screenPointToRay(pointerPixels, viewport);
if (const auto hit = scene.selection().resolvePointer(pointerPixels, camera, viewport, query)) {
    engine::ManipulationSettings settings;
    settings.direction = {0, 0, 1}; // World plane normal chosen by the application.
    drag.begin(*hit, ray, settings);
}
// On move: drag.update(camera.screenPointToRay(nextPixels, viewport));
// On accepted release: drag.commit();
// On Escape, pointer cancellation, GUI capture or view change: drag.cancel();
```

Plane translation preserves the original picked offset. Axis translation uses
the closest points of the pointer ray and the world axis through the picked point.
Set `mode=ManipulationMode::AxisTranslation` and `direction` to that axis.
For rotation, set `mode=ManipulationMode::AxisRotation`, a world `direction` axis
and world `pivot`; ray/plane samples accumulate signed angles across wraparound.
Each rotation sample must advance less than half a turn; a ray at the pivot or
parallel to the constraint cannot update. Directions are normalized. Malformed
rays/settings throw `invalid_argument` before changing a valid session.

`begin`, `update`, `commit` and `cancel` return success; failed start never acquires
ownership. A second start requires explicit commit/cancel of the first session.
Applications assign one controller/writer per actor; there is no global lock registry.
An update miss or a candidate requiring local shear leaves the last valid pose
and session intact. Commit retains that pose and ends the session. Cancel restores
the exact initial local transform; destruction cancels an uncommitted session.

Target/provider/surface removal, disable, provider selectability changes,
reparenting, parent movement or an external local transform write invalidates
ownership. `target()` immediately hides invalid sessions. The next update,
commit or cancel releases the stale session without rollback or overwriting the
new owner's work. The controller rejects actors with rigid-body/character-motor
components, including controlled ancestors, so it never bypasses physics pose
ownership or creates a physics world. Physics grab/joint policy is a separate
consumer of physics commands. Applications own input capture, feedback, snapping,
move legality, history and release/cancellation routing; sessions are not archived.

## Actions and events

Actions have unique nonempty string IDs per provider. Multiple providers may expose
the same ID; `ActionHandle` identifies the target, provider and definition revision.
Adding/removing a definition invalidates earlier handles from that provider.
Eligibility receives a scene, target and optional instigator; applications implement
distance, inventory, ownership and other gameplay rules there. A nonempty instigator
must refer to an active actor in the same scene. `availableActions` includes disabled
options with reasons; unbound actions are disabled.

`execute` rechecks the provider, definition revision, instigator and eligibility at
execution time. It reports Succeeded, Ineligible, Unavailable or Failed with a
reason. Exceptions from eligibility become denied eligibility; exceptions from an
executor become Failed. Actions need not belong to the currently selected actor.
Action/eligibility callbacks run under the scene traversal guard: `destroy()` and
definition removal are safe; erasing/detaching/reparenting the hierarchy is forbidden.
Callbacks should use handles to look up scene objects instead of retaining pointers.

Listeners receive synchronous `SelectionChange` records with monotonically
increasing sequence numbers, separate Selection/Focus kinds, and Explicit,
Pointer, Invalidated or Cancelled reasons. Unchanged states do not publish. Reentrant
changes are queued so all listeners see the same order; unsubscribing before a
listener's turn prevents that callback. Newly subscribed listeners start with the
next change. Listeners may destroy targets safely and should not throw. Selection
getters immediately hide invalid targets; scene update/fixed update/destruction
flush reconciles stored state and publishes invalidation. Scene shutdown does not
publish callbacks into owners that may already be destructing.

Scene archives save picking flags/masks, proxy bounds and action IDs/labels. Runtime
selection/focus, listeners, handles, eligibility callbacks and executors are not
saved. Restored actions are inert until the application removes/re-adds each
definition with its callbacks. Existing archives remain readable.

## Validation

`selection_tests` covers nearest/ranged rays, transformed and morph bounds, nested
ownership, masks, viewport offsets, perspective/orthographic cameras, state changes,
deferred destruction, slot recycling, ordered reentrancy, action eligibility,
replacement/self-removal, exception results, instigator isolation, backing-pixel
input, stationary pointer repicking, cancellation and archive round trips.
Precise regressions cover empty bounds space, hit attributes, two-sided/mirrored
triangles, morph/skinning/interpolation and noninteractive occlusion filters.
`manipulation_tests` covers plane/axis dragging, pivot rotation, wraparound,
transformed parents, shear rejection, commit/cancel, ownership loss and stale handles.

See [validation results and commands](selection-validation.md).
