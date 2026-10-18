# Static navigation meshes

Link optional `engine_ai` and include `engine/navmesh.hpp`. `NavMesh` implements
`NavigationSurface`, alongside `WaypointGraph`; `Navigator` accepts either.
`WaypointNavigator` remains a source-compatible alias of `Navigator`.
Game goals, actions, request scheduling and motor ownership remain application code.

```cpp
scene.synchronize(); // Register current authoritative collision first.
auto mesh = engine::NavMesh::bake(scene);
engine::NavigationSettings settings;
settings.queryBudget = 512; // Application choice; library default remains 64.
engine::Navigator navigator(settings);
navigator.navigate(mesh, destination);
// Owning actor fixed tick, before its child CharacterMotorComponent:
navigator.step(motor, fixedSeconds);
// Disable/detach, target loss, interruption or control handoff:
navigator.cancel(motor);
```

## Bake ownership and geometry

`bake(Scene, settings, filter)` snapshots registered static solid collision bodies,
including collider-local transforms and authoritative world poses. Sensors,
dynamic and kinematic bodies are excluded; layer and handle exclusions remain
available. Physics must already exist. Call on the scene owner while idle, after
synchronization, never during a solver callback or concurrent scene mutation.
No renderer geometry, LOD, interpolation or terrain presentation mesh is consulted.

`PhysicsWorld::triangleGeometry(filter, triangleLimit=100000)` returns owned
world-space vertices/indices and contributing body component handles. It traverses
solver leaf shapes, validates finite coordinates and rejects capacity overflow
without returning partial geometry. The snapshot retains no scene or solver types.

`bake(PhysicsGeometry, settings)` accepts an independently owned triangle source.
This overload describes surfaces: callers must exclude undesired enclosed voids
inside solid objects. The scene overload additionally removes occupied walkable
voxel spans using static-solid overlap queries before erosion and polygonization.
These synchronous queries/callbacks are not retained by the resulting resource.
Two-sided or arbitrarily wound source triangles are not automatically repaired;
walkable triangles must face upward.

Bakes validate before publication and return `shared_ptr<const NavMesh>`. Retain
the previous resource until replacement succeeds. The immutable single-tile mesh
owns query data, convex-polygon adjacency, build statistics and an engine `Mesh`
for debugging. Every query owns its mutable scratch state. Public headers expose
no Recast/Detour or browser types. Vendored Recast/Detour 1.6.0 are private build
dependencies; provenance and license are in `third_party/recastnavigation/`.

## Profile and work bounds

Defaults: .2 m horizontal/.1 m vertical cells, .35 m radius, 1.9 m total height,
pi/4 maximum slope, .3 m step, .5 m endpoint snap and .5 m vertical snap.
Rasterization filters slope, low ceilings and ledges; radius erosion reserves
clearance. `compatible(motor.settings())` requires a motor no wider/taller than
the baked agent and at least the baked slope/step capabilities. `Navigator`
rejects an incompatible profile before issuing movement.

Default budgets are 262144 grid cells, 2000000 conservative triangle-to-cell
raster work units (also the upper bound on scene occupancy queries), and 4096
polygons. Hard maxima are 1048576 cells, 8000000 raster work and 4096 polygons;
source input is at most 100000 triangles/300000 vertices. Voxel sizes are .01–10 m,
height is 3–4096 vertical voxels and radius at most 255 horizontal voxels.
Invalid, oversized, degenerate or empty results throw; no partial mesh replaces
valid state. Baking is synchronous preparation work, with no frame-time promise,
worker, streaming, incremental rebuild or background completion in this API.

## Queries and movement

`project(point, horizontalRange=1)` returns an optional projected position and
resource-local polygon index. It enforces circular horizontal range and the
bake's vertical snap; it cannot select an arbitrarily distant floor.
`findPath(start, goal, WaypointSearchSettings)` projects both ends using the lesser
of query snap distance and baked endpoint snap, then performs bounded A* over
convex polygons. `nodes` are polygon indices, not actor handles. Complete paths
contain projected endpoints; requested destinations can differ within snap
tolerance. Missing endpoints/disconnected routes return Unreachable, exhausted
work returns BudgetExceeded, and neither publishes partial points.

The synchronous non-retained `canTraverse` predicate checks endpoint connection
and polygon center/portal segments. Detour funnel straightening is accepted only
when its segments also pass within the remaining query budget; otherwise the
already checked center/portal route is retained. Paths contain at most 4098 points.
Expansion and segment budgets remain explicit; applications budget across agents.

`Navigator` uses the existing capsule clearance, pending-segment check, delayed
replanning, retry/deadline limits and cancellation contracts in [AI](ai.md).
Navmesh requests always validate projection, including already-near destinations.
The retained requested goal survives retries; arrival refers to its projected
destination within follower tolerances. Runtime probes use current obstacles,
but only existing polygon corridors are searched. They can conservatively reject
stairs/slopes the motor could climb; this increment validates baked step/slope
connectivity and actual motor movement on flat ground, not general stair walking.

Meshes are immutable snapshots. Moving/removing baked static geometry requires
invalidation and an explicit rebake/replacement as described below. There is no dynamic carving, tiled streaming,
off-mesh action/jump link, crowd avoidance, editor authoring or archive codec.
Runtime sweeps do not prove support or detect a removed floor in stale topology.

## Navigation revisions and replacement

`NavigationSource` is an application-owned publication slot for an immutable
`NavigationSurface`. It has no scene, worker, subscription or global registry.
Use it when collision can change; direct `navigate(mesh, goal)` retains the
original immutable behavior and cannot detect source geometry changes.

```cpp
auto source = std::make_shared<engine::NavigationSource>(mesh);
navigator.navigateSource(source, destination);

// On the scene owner, BEFORE changing collision/support:
const auto revision = source->invalidate();
// Apply the application's door/prop/terrain collision changes here.
scene.synchronize();
// Explicit preparation work; not a per-frame automatic bake:
auto candidate = engine::NavMesh::bake(scene);
if (!source->publish(revision, candidate)) {
    // A newer invalidation superseded this result; leave it unpublished.
}
```

All source operations and navigator steps run sequentially on the scene owner.
This API introduces no thread-safe scene access or background executor. A host
can prepare owned data separately, but must return complete candidates to the
owner for publication; scene baking itself must remain owner/idle work.

Revisions start at 1. `invalidate()` advances the 64-bit revision before marking
the source dirty and returns the token required by `publish`. Revision overflow
throws without changing state. Multiple invalidations supersede older tokens.
`publish(token, nonnullSurface)` succeeds only for the current dirty revision,
exactly once; stale/duplicate completions return false. Null candidates throw.
A default-constructed source starts dirty with no surface and can publish token 1.
Invalidation retains the last successful `surface()` for diagnostics, but
`ready()==false` prohibits using it as current navigation. A failed bake leaves
the source dirty and never reactivates its previous topology.

Before its next child-motor tick, an active navigator cancels stale follower
commands and enters `WaitingForSurface`. It performs no searches or clearance
queries while dirty. Gravity and physical collision continue: invalidation cannot
keep an agent suspended if support directly beneath it was removed. Invalidate
before mutation and step navigation before motors to prevent stale commanded
travel onto removed support; applications still own physics-change ordering.

Once a matching replacement is ready, the next fixed step drops old points and
failed-corridor history and searches from the current authoritative position to
the retained original goal. Replacement does not reset elapsed time, query
statistics or the request's finite search/replan allowance. Waiting for a bake
counts toward the same total deadline; repeated replacements cannot cause an
unlimited retry loop. One search per eligible fixed step remains the limit.
Incompatible replacement profiles fail before movement. Terminal Arrived,
Unreachable, BudgetExceeded, TimedOut and Cancelled requests do not automatically
resume on publication. Use explicit `retry()` for failures or issue a new request;
`cancel(motor)` releases both source and surface interests. Switching to direct
`navigate(mesh, goal)` also releases the source binding.

Applications explicitly invalidate all affected sources for doors, moved static
props, terrain edits or collision removal. There is no automatic collision-change
subscription, affected-region detection or tile carving in this increment.

## Browser demonstration

Open `?scene=navmesh`, or open the AI demo and enable **Static navmesh navigation**.
Click the visible navigation surface to set an arbitrary destination; orbit drag
retains camera ownership. The translucent cyan mesh shows walkable polygons,
yellow shows the full planned route, and green marks the destination. Toggle the
navigation overlay independently. `NavMesh::raycast` performs a bounded CPU ray
test against its derived mesh. Main application code maps existing Select input
through the active camera; portable navigation never reads browser input.

The demo excludes its movable diagnostic blockers from the static snapshot and
uses a 512-query planning budget. Pause, resize, return and re-entry use existing
application lifecycle ownership. Saving active custom AI actors still requires
application codecs. See [validation](navmesh-validation.md).

**Close/Open passage** and **Remove/Restore floor bridge** invalidate before
changing registered collision. **Rebuild navigation** explicitly publishes a new
mesh. The status reports dirty/ready revision; stale overlays disappear while
dirty. An active goal is preserved across rebuilds. Rebuild a removed bridge to
see a disconnected route, then restore/rebuild before retries expire to see the
same request recover. Terminal failures use the existing **Retry goal** control.
See [navigation update validation](navigation-updates-validation.md).
