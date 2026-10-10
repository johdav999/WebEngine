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

Bounds picking is conservative: gaps inside a mesh AABB can hit. It is not a
triangle/GPU-ID query. Nonparticipating geometry does not occlude targets. Child
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

See [validation results and commands](selection-validation.md).
