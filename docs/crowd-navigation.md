# Crowd avoidance and changing obstacles

Open `?scene=crowd`, or **Crowd & changing obstacles → Open arena**. **Head-on
pair** demonstrates passing; **Eight characters** starts opposing streams.
**Close gate / Open gate** moves a tracked solid, rebuilds the walkable footprint
and makes active agents retain their goals and replan. The green overlay is the
last published navigation resource (diagnostic only while updating). Agent diagnostics show movement, arrival, yielding
and waiting. Pause stops simulation; camera movement remains independent.

This extension composes existing `Navigator`, `PathFollower`, `NavigationSource`,
`NavMesh`, scene handles, physics queries and character motors. Game goals,
spawning, gate policy and debug meshes remain in `src/crowd_demo.*`.

## Crowd service and fixed-step order

`engine/crowd_navigation.hpp` in optional `engine_ai` provides application-owned
`CrowdNavigation` and `CrowdSettings`. Register scene-root collision-aware motors:

```cpp
engine::CrowdNavigation crowd;
crowd.add(motor, dynamicNavigation.source(), destination);
crowd.setGoal(motor.id(), anotherDestination);

// Exactly once per simulation fixed step, BEFORE every registered motor tick:
crowd.step(scene, fixedSeconds);

// On owner disable/destruction, target cancellation or control handoff:
crowd.remove(scene, motor.id()); // Cancels and stops this motor.
crowd.clear(scene);             // Cancels all; releases the scene binding.
```

The service retains generation-checked motor handles and shared read-only sources,
not scene/actor/component pointers. The application owns it and must clear/remove
controlled motors before abandoning it. Stopping calls alone leaves persistent
motor commands intact. Only one producer may command each motor; stop other
controllers before registration. `add` validates without replacing existing agents;
duplicate, foreign-scene, invalid goal and capacity failures reject. Sources may
start dirty. `setGoal` validates before replacing a request.

Character motors remain direct components of scene-root actors. For a scene-owned
coordinator actor, place that root **before** agent roots with ordinary
`scene.setChildIndex(coordinator, 0)` and call `step` from its fixed callback. The
demo follows this arrangement. Do not nest motor actors under the coordinator or
invoke the coordinator after some agents have already moved. There is no new
implicit scene phase, global AI registry, worker access or render-driven movement.

Each step snapshots authoritative poses and actual previous motor velocities,
advances existing navigators, gathers preferred commands, then scores bounded
velocity candidates for all agents. It favors route progress and consistent
right-side passing, predicts closest XZ circle separation over a time horizon,
checks current-world capsule clearance and navmesh support, and submits commands
after coordination. A simultaneous next-step proximity guard can stop conflicting
commands. It never teleports agents or advances physics itself.

The service refreshes cached probes when capsule dimensions change. Missing,
disabled, non-fixed-ticking or free-mode motors cancel safely; reactivation needs
an explicit new goal. Scene mismatch/invalid timestep reject before stepping.
A query/stepping exception clears controlled commands and registrations before
rethrowing. Stale generations cannot command replacement components.

Near-zero neighbor-induced motion marks **yielding**. The new optional
`yielding=false` argument on `Navigator::step` / `PathFollower::step` suppresses
only stuck-time accumulation while yielding; total request/route deadlines remain
finite. Ordinary callers retain existing behavior. Navigation revision changes
retain the existing retry/deadline budgets, and terminal failures require a new
goal or explicit retry policy. Crowd requests do not reset budgets every frame.

## Bounds and behavior

Defaults: 32 agents (hard maximum 64), 12 nearest neighbors (1–64), neighbor range
4 m (0–32], prediction horizon 1.5 s (.1–5), separation margin .08 m (0–1), and
8 candidate-clearance tests per moving agent (1–32). Each moving agent scores 29
velocity candidates, including stop. Neighbor scanning and the four-pass next-step
guard are bounded by the 64-agent limit; candidate scoring uses the neighbor cap.
`stats()` reports agents, neighbor relations, candidates, obstacle queries and
safety stops for the last step. Per-agent `snapshot()` returns owned diagnostics.

Crowd default navigation uses 2 m/s, a 4 s stuck interval, 120 s route/request
deadline, 32 replans and 256 navigation queries per agent/step. These are separate
from the avoidance-probe bound; applications budget total agents and planning.
Other navigator/follower defaults remain unchanged. No frame-time guarantee is
made by these finite work limits.

This is local predictive steering with conservative checks, not an optimal or
deadlock-free crowd solver. Narrow passages, impossible spawn overlap, too-dense
traffic or conflicting goals can require queuing/application priorities or end in
the existing finite timeout/failure states. It does not add general mutual
character contacts to the physics solver. Non-crowd motors are not automatically
neighbors, and collision-induced deviations can differ from predicted motion.
Prepared static LOD/culling/interpolation never determine navigation state.
Unaltered preferred route commands (including slowing along the same direction)
retain the existing navigator's route validation and waypoint tolerances.
For steering detours, `NavMesh::canTraverseSegment` checks both projected endpoints and a bounded
walkability ray across at most 256 polygons; a disconnected span or exhausted
buffer rejects the candidate. This keeps local detours from skipping holes just
because the endpoint is supported. Endpoints use .08 m horizontal snap and the
bake's vertical snap. Waypoint-only surfaces retain their
application-authored support guarantees. Fixed-step calls must remain <=.25 s.

## Dynamic obstacle service

`engine/dynamic_navigation.hpp` supplies application-owned `DynamicNavigation`:

```cpp
engine::DynamicNavigation navigation;
scene.synchronize();
navigation.track(scene, obstacleBody.id()); // Solid RigidBodyComponent handle.
navigation.prepare(scene, 0);               // Initial complete bake.

// On the idle owner, before subsequent agent fixed steps:
navigation.prepare(scene, simulationSeconds);

// BEFORE changing untracked floor/support/static topology:
navigation.invalidate();
// Apply collision changes, then prepare again.
```

`prepare` synchronizes scene physics, polls tracked registered body AABBs and
invalidates on significant motion, disable/re-enable or removal. It is idle
preparation work and cannot run inside traversal/fixed callbacks. A kinematic
movement target becomes an actual body bound when physics moves it; an immediate
`RigidBodyComponent::teleport` is detected immediately after synchronization.
For known changes, explicitly invalidate before mutation so old motor commands
stop at their next crowd/navigator step. The host must prepare before those steps.

Up to 64 solid bodies are tracked with handles, no retained body/scene pointers.
Default motion threshold is .02 m per AABB axis (0–1); sub-threshold changes
accumulate against the last significant bound. Default rebuild interval .25 s
(0–10) admits at most one synchronous whole-resource bake per prepare call.
The caller supplies simulation delta in [0,.25]; pause need not advance cooldown.
The service binds one `Scene::runtimeIdentity()` (the existing runtime handle
scope, newly exposed read-only, never persisted). Foreign scenes reject.

`NavMesh::bakeWithObstacles(scene, bounds, settings, filter)` reuses the existing
static collision bake, removes tracked static bodies from its source filter and
carves their current solid AABB footprints from walkable voxel spans before radius
erosion. It preserves existing cell, raster, occupancy and polygon budgets. Bounds
are finite strict min/max boxes, with at most 64 per bake; arbitrary rotated or
compound bodies are conservatively represented by their world AABB. A footprint
blocks vertically overlapping agent space above the step-height threshold. This
does not create walkable tops on moving objects or navigation links/lifts.

Successful complete immutable meshes publish through the existing revision slot.
Dirty sources stop active routes until publication, then retain their original
goals and replan. Old meshes remain diagnostic only while dirty. Failure records
an error and leaves the source unavailable; `retry()` admits an explicit attempt,
and new obstacle/topology revisions also admit retry. There is no automatic retry
storm against unchanged failed input. `untrack(handle)` invalidates before
removing a tracked definition and releasing its slot; mutate/remove the physical
body afterward. Destroyed handles remain inert tracking records until untracked.

Untracked floor/topology changes still need explicit invalidation. This extension
uses bounded full rebakes, not asynchronous/tiled incremental rebuilding. Frequent
continuous obstacle movement can hold routes waiting between publications; local
clearance still checks current collision. Applications choose tracking/cooldown
and update timing. There are no implicit physics subscriptions or worker bakes.

## Integration and persistence

Both services are optional `engine_ai`; core gains only a read-only scene runtime
identity accessor. Recast/Detour and solver types stay private. Existing source,
motor and scene ownership is unchanged. Profiling scopes attribute crowd stepping
and dynamic preparation to AI. Settings/runtime requests are transient; no archive
version or built-in AI checkpoint codec changes. The demo's custom coordinator
has no serializer, so active-demo save/clone rejects under ordinary archive rules.
Return to the playground to restore ordinary saves.

The browser owns controls/reporting in `web/crowd.js`; simulation remains C++.
`?crowdTest=1` permits paused explicit fixed-step acceptance; the export rejects
ordinary/running use. Desktop browser tests are not physical mobile acceptance.
See [validation](crowd-navigation-validation.md), [AI](ai.md) and [navmesh](navmesh.md).
