# Physical surfaces, compound collision and impact data

Stage 2 of the [driving support plan](driving-support-plan.md), implemented 2026-10-18.
These optional portable mechanisms live in `engine_core`; game rules, sounds,
damage and material naming remain application choices. Both renderers consume the
same simulation. See [recorded validation](physical-surfaces-validation.md).

## Resources and combination policy

`PhysicalSurface` is shared through `shared_ptr<const PhysicalSurface>`. Construct,
validate, then publish immutable resources; retaining and mutating a non-const alias
violates the resource contract. `id` is a descriptive ASCII identifier (1–128
letters/digits/`._-:`). Pointer/resource identity controls sharing: different edited
versions may retain the same ID. There is no process-wide mutable registry.

Defaults are neutral: `friction=1`, `restitution=0`, `tyreGrip=1`,
`rollingResistance=0`. Friction and grip accept finite 0–10, restitution and rolling
resistance finite 0–1. Coefficients are dimensionless. No visual material property
implicitly sets physical properties.

```cpp
auto asphalt = std::make_shared<PhysicalSurface>();
asphalt->id = "road.asphalt";
asphalt->friction = 1;
asphalt->tyreGrip = 1;
asphalt->validate();
collider.setSurface(asphalt); // Collider fallback; synchronize before queries.
```

Mappings are on `ColliderComponent::surface`, immutable
`CollisionShape::withSurfaces(default, triangleSurfaces)`, and compound child
`surface`. Triangle maps are empty or exactly source `indices.size()/3` entries;
null entries inherit. A child override replaces that child's own shape default;
descendant shape defaults/child overrides can refine it, and triangle entries
have final precedence. Remaining nulls resolve to the collider fallback, then
neutral coefficients. Metadata preserves the original source triangle order through
Jolt cooking/reordering. Changing topology invalidates source paths/triangle indices.

Solid contact friction is `sqrt(body1.friction * surface1.friction *
body2.friction * surface2.friction)`. Restitution is the maximum of the two body and
two surface values. Tyres use `sqrt(wheelCurve * targetBody.friction *
targetSurface.friction) * targetSurface.tyreGrip` independently for longitudinal
and lateral friction. Wheel telemetry exposes the actual combined coefficients.
The chassis surface is not used as the tyre's surface. Surface-aware meshes retain
individual contact manifolds so different regions can have different coefficients.

Rolling resistance is an optional approximation: the previous step's grounded
wheel coefficients (each weighted 1/4) apply a tangent drag at chassis COM of
`mass * |gravity| * coefficient`, capped to stop within the next step. It has a
one-step delay and does not model wheel load, tyre deformation or drivetrain loss.
Setting it to zero preserves the pre-extension policy.

## Compound shapes and body lifecycle

```cpp
const CompoundChild parts[] = {
    {CollisionShape::box({.8f,.25f,1.75f}), {}, asphalt},
    {CollisionShape::box({.7f,.18f,.7f}), {{0,.4f,-.2f}}, {}}
};
collider.setShape(CollisionShape::compound(parts));
```

Children hold immutable shared shapes, finite local positions/Euler rotations in
radians, unit scale and optional physical overrides. Each compound accepts 1–32
children; complete expanded trees allow eight child levels, 1,024 descendant nodes
and 100,000 triangles. Empty children, null resources, scaled poses and invalid
geometry fail before publication. Shared leaves are supported. Jolt types stay
private. COM/inertia derive from compound geometry; authored body COM offset is
added to the geometric COM in actor axes, with existing inertia scaling semantics.

Moving solids and sensors require convex leaves. Mesh leaves are supported only
where existing static-mesh body rules permit them. Ray queries, collision geometry
snapshots and bounded debug drawing traverse compounds. Overlap/sweep query shapes
may be compounds containing convex leaves. They retain one deepest overlap per
body/closest sweep result, not every intersecting leaf.

Changing a shape or a shape-level surface map creates a new immutable resource and
follows existing body rebuild semantics: attached joints/vehicle constraints are
removed and rebuilt where applicable; body velocities and transient vehicle state
reset. Author in idle editor mode. Collider fallback reassignment updates contact
metadata, invalidates contact caches and wakes scene dynamic bodies without rebuilding
geometry. Commands/contacts remain per instance and simulation remains independent
of interpolation, LOD, visibility, MSAA and rendering backend.

## Query, wheel and impact observations

Ray, overlap and sweep hits carry `PhysicsSurfaceHit surface`. The same structure
appears in each wheel contact and on both sides of `CollisionEvent`:

- `surface` retains an immutable resource or null (neutral default).
- `feature.children[0..depth)` is the source compound child path, maximum eight.
- `feature.triangle` is the original source mesh triangle, or `UINT32_MAX`.

Wheel contact metadata clears when contact is lost. These are observations, not
persistent IDs for topology edits. Hold owned resource pointers or copy observations;
resolve actor/component generation-checked handles before using scene objects.

Begin/Stay collision events additionally contain world-space `relativeVelocity =
velocity(second, point) - velocity(first, point)` in m/s and
`closingSpeed = max(0, -dot(relativeVelocity, normal))` in m/s. Normal points from
first body toward second. Measurements are taken pre-solver at the first manifold
point and delivered through the existing guarded, post-solver callback. They are
not impulses, forces, damage, peak deformation or one callback per crash. Stay can
repeat; applications own thresholds, deduplication and cooldowns. End has
`impactValid=false`, zero measurements and no surface observations. Sensors retain
the separate trigger contract. The existing per-step event queue is bounded at
8,192; overflow increments `PhysicsStats::droppedEvents`. Retained resource pointers
do not authorize use of stale scene handles.

## Terrain, pages and workers

`TerrainData::physicalSurface` is the terrain fallback; `cellSurfaces` is empty or
exactly `(samplesX-1)*(samplesZ-1)` row-major entries. Each cell maps both collision
triangles. Null inherits the terrain fallback and ultimately the collider fallback.
Physical mapping uses source cells, independently of visual layers and camera LOD.
`TerrainAsset::withPhysicalSurfaces` returns a validated `TerrainEdit`; applying it
shares visual meshes/materials and rebuilds affected collision, including streamed
chunks. It may rebuild a whole collider; this is not an incremental solver edit.

`TerrainPage` carries the same fields over its halo sample dimensions. Optional
WETP version 3 palettes preserve them through page files, write batches, worker
build/hydration and source edits. Neutral pages continue writing WETP2; readers
accept WETP1–3. WETP3 is limited to 256 physical resources, a 64 KiB `.weassets`
palette and 256 KiB total page bytes. CRC, counts, resource types/references,
physical halo agreement, source memory budgets and worker collision/source map
agreement are validated before residency publication. Map pointer storage and
unique surface resources count toward source residency/dirty memory. Independently
decoded halo resources compare by values rather than pointer identity.

Physical page maps can be provided through page loaders; the existing page brush
only sculpts/paints visual data and preserves physical maps. The full-source editor
supports physical map authoring; paged physical-map painting is not implemented.

## Editor and persistence

Select a collider, its actor, or a complete terrain instance. In Details open
**Physical surfaces and collision shapes**. Create a surface asset, select a
binding, then assign, clear, or apply an edited copy. Bindings support collider
fallback, shape defaults, compound child overrides, triangle ranges and terrain
cell ranges. The catalog includes retained and scene-used resources. Edited copies
affect only the selected binding; no global in-place shared mutation occurs.

Select a source child path to edit its local pose, wrap a shape, add/remove children
or replace a shape with box/sphere/capsule or an existing collision asset. Debug
uses the existing collision/suspension overlay. Vehicle telemetry includes contacted
surface IDs and combined longitudinal grip. Authoring is disabled during Play.

Portable `EditorSession::setCollider` and `setTerrainPhysicalSurfaces` provide exact
resource undo/redo, reject invalid/stale data, and restore the previous collider
on synchronization failure. Surface library imports use existing library history.
Terrain mapping requires a complete editable source instance, following existing
terrain restrictions; generated/paged implementation children remain protected.

Scene/application/effect envelope writer v25 adds physical resources, child topology,
triangle and terrain maps, collider fallbacks and the retained asset catalog.
Sharing is deduplicated by the existing typed resource table. Readers of v1–24
apply neutral defaults; old readers reject v25. Rebuild/deploy matched main/worker
Wasm and host JavaScript. Subtree duplication, asset libraries and isolated Play
use built-in codecs; runtime contact metadata, queues, solver caches and callbacks
are transient. Application code reconstructs consumers after restore. Reads enforce
archive resource/allocation budgets, reject cycles/unknown references and construct
candidates before replacing valid scenes. Saves do not capture exact solver rewind.

The application fixture is `?scene=vehicle&surfaceTest=1`: three coloured lanes, a
compound chassis and mapped terrain. `vehicleTest=1` adds paused-only test controls;
these are application adapters, not engine interfaces. Run `physical_surface_tests`,
Node `physical_surface_wasm_tests.js`, and `scripts/physical-surface-smoke.cjs` at
DPR1/DPR2. Validation details and limitations are recorded separately.
