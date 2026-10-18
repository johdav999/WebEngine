# Particle contacts and projected decals

Particle collision is optional simulation in the existing emitter pool. Projected
decals are independent scene components that clip a projector volume against real
opaque receiver triangles. Applications consume impacts and decide whether to create
a mark, play sound or spawn another effect. The engine does not run game callbacks
inside particle integration.

## Particle collision

```cpp
auto d = std::make_shared<ParticleEmitterDefinition>();
d->space = ParticleSpace::World;
d->collision.enabled = true;
d->collision.response = ParticleCollisionResponse::Bounce;
d->collision.radius = .02f;
d->collision.restitution = .4f;
d->collision.friction = .3f;
d->collision.layers = 1u << 0;
d->collision.maxQueries = 256;
auto& emitter = scene.addChild<ParticleEmitterComponent>("Sparks", d);
// Add ordinary scene colliders/bodies for the surfaces that should stop particles.
scene.update(delta);
for (const auto& impact : emitter.impacts()) {
    // World-space point, normal, incoming velocity/speed and particle age.
    // actor/body are scene-scoped handles; resolve them before using the receiver.
}
emitter.clearImpacts();
```

`ParticleCollisionSettings` defaults off, Bounce, radius .02 world units,
restitution .4, friction .3, settleSpeed .1, layers 0xffff, maxQueries 256,
maxImpacts 128, excludeOwner true. Radius must be .001-100; restitution/friction
are [0,1], settle speed is finite/nonnegative, maxQueries is 1-65536 and maxImpacts
1-4096. The layer mask is 16-bit and independent of render visibility and the
physics layer-pair matrix. Settings validate before publication. Collision is
supported on quad and mesh particles; Ribbon mode rejects it explicitly.

Scene variable updates integrate motion in at most 128 midpoint intervals, normally
near 1/60 second when collision is enabled. Each moving interval sweeps an immutable
sphere through the existing PhysicsWorld query API. Queries use authoritative body
poses synchronized before scene ticks, include solid static/dynamic/kinematic bodies,
exclude sensors and optionally exclude the emitter's nearest owning actor. No rigid
body is created per particle. The sphere is fixed world radius, independent of
visual size, mesh shape and hierarchy scale.

Bounce reflects incoming normal velocity using restitution and attenuates tangent
velocity by friction. Slide removes incoming normal motion and applies tangent
friction. Kill removes the particle and queues its impact. Settle freezes motion
and spin while age/fade continue. Low response speed can also settle a particle.
Initial penetration depenetrates along the query normal; coincident geometry may
have an ambiguous normal under the existing sweep contract. Contact leaves a .001
world-unit separation. The remaining movement in a colliding interval is discarded;
it is not a multiple-impact rigid-body time-of-impact solver. Moving-body velocity
and angular momentum are not transferred.

Local particle movement is swept in world space and converted back to local
coordinates for response. Parent movement between variable updates is not swept;
use World particles for independent debris. Singular local transforms freeze that
interval. Zero-displacement probes do not query overlaps. Extremely long updates
approximate curved motion because of the substep cap.

`collisionQueries()` reports the last non-paused update's query count. Once the
per-emitter budget is exhausted, untested position/velocity changes are frozen;
particles still age and animate. Active pool order rotates per update to spread
budget opportunities. This order is stored by normal pool persistence. Budgets are
per emitter, not a global scene physics budget. Kills can conservatively occupy the
existing subframe emission-capacity heap until their original expiry within the
same update; overloaded collision effects may drop more emissions in long frames.

`impacts()` exposes bounded queued output until `clearImpacts()`, clear or restart.
`droppedImpacts()` counts overflow, and `skippedCollisionSteps()` counts frozen
budget intervals; restart resets these diagnostics. No executable callback or
borrowed receiver pointer is retained. Body/actor handles can become stale after
destruction and must be resolved through the scene. Collision has no effect on
authoritative gameplay bodies, damage or quests.

## Projected decals

```cpp
DecalSettings settings;
settings.texture = markTexture;
settings.color = {.3f, .12f, .05f};
settings.opacity = .8f;
auto& mark = scene.addChild<DecalComponent>("Scorch", settings);
mark.transform.position = groundContact;
mark.transform.scale = {1.2f, .3f, 1.2f};
// Identity rotation projects down local -Y onto an upward ground normal.
```

The projector is the transformed local box [-.5,.5] on all axes. Local XZ covers
the full texture; local +Y is the accepted receiver normal and projection proceeds
along -Y. Scale chooses width/depth/height of the volume, while rotation aligns it
to walls or slopes. Only receiver surfaces inside the volume receive a mark. A
projector remains a volume: if a receiver moves away, the old mark does not remain
attached to that receiver automatically. Parent the projector deliberately when
an attached mark is wanted.

`DecalSettings` owns a shared RGBA texture, RGB tint, opacity, normalOffset,
normalThreshold, blend, receiverMask and source/output triangle budgets. Defaults
are white texture/tint, opacity 1, offset .002 world units, threshold .1,
Alpha blend, receiverMask 0xffffffff, 20000 source triangles and 4096 output
triangles. RGB must be finite/nonnegative, opacity [0,1], offset [0,1], threshold
[-1,1], source budget 1-1000000 and output budget 1-100000. Alpha/Additive are
supported. Invalid candidates preserve current settings. Decal visibilityMask
defaults 1 and is independent of receiverMask.

`MeshComponent::receivesDecals` defaults true. Eligible receivers are solid opaque
World mesh parts matching receiverMask. Transparent/cutout parts, Water, active
skinning/morph poses, mesh particles and other decals are excluded. Terrain's
ordinary static chunk geometry can receive projection. Renderer-only deformation
and lower camera LOD geometry are not evaluated: projection uses source triangles
collected before presentation LOD selection. Decals may therefore diverge from
coarse/deformed presentation surfaces. Normal threshold rejects triangles facing
away from the projector. Receiver bounds provide broad-phase rejection, then each
triangle clips against six box planes; generated UVs follow projector coordinates.

The component caches bounded immutable clipped geometry by projector and receiver
geometry/pose/range. Static receivers reuse it. Moving receivers or changed settings
rebuild it; different view masks can also change the receiver set. Large/moving
projectors can incur repeated CPU clipping and mesh uploads. If either triangle
budget is exceeded, the candidate is discarded and no partial or stale mark is
drawn; `projectionLimited()` reports this state. `triangleCount()` and
`projectionBuilds()` expose presentation diagnostics. Singular projectors draw
nothing. The cache is not scene simulation state and is never archived.

Projection emits an ordinary unlit, double-sided, transparent MeshDraw with shared
texture, normal offset, depth testing and scene fog. There is no new shader or
depth-buffer pass. Both renderers reuse mesh resource lifetimes and transparent
ordering. Decals cast/receive no shadows and modify neither receiver geometry nor
material, collision, picking or gameplay. They do not write PBR normal/roughness
channels. Intersecting marks retain center-based transparency limitations. Marks
are persistent components until application removal; no automatic lifetime/fade
clock or automatic impact-to-decal spawn policy is imposed.

To orient a root-level projector to a world contact normal `n`, normalize `n`, use
Euler rotation `{asin(clamp(n.z,-1,1)), 0, atan2(-n.x,n.y)}`, position at the contact
and scale `{width,depth,height}`. Convert to parent space when the parent is
transformed. Consume queued impacts after scene traversal, and cap/spawn/remove
marks in application code. The browser acceptance fixture demonstrates one impact
creating one projected mark, outside reusable systems.

## Authoring, persistence and ownership

FX Collision controls expose enable, response, radius, restitution/friction,
settle speed, physics masks, budgets and owner exclusion. The isolated EffectPreview
owns a static ground collider and displays impact/query diagnostics. Pause/step and
normal immutable effect undo/redo remain isolated from authoring source state.

Add DecalComponent to a selected actor via Scene structure. Its typed properties
edit RGB/opacity, normal offset/threshold, receiver mask and source budget. Transform
controls place/rotate/scale the projector. Projected decal texture selection reuses
imported Assets images and has bounded undo/redo through `setDecalTexture()`.
Mesh properties expose Receive decals. The Details panel reports projected triangles
or budget suppression. Creation/removal, property/texture edits, Play/Stop and scene
save/reopen reuse normal stable identities and editor ownership; runtime handles
rebuild. Source scenes outlive borrowed renderer views as before.

Archive v22 introduces collision settings, settled particle state, receiver opt-out
and DecalComponent records with shared textures/settings/pose. Versions 1-21 default
collision off and receivers on, with no decals. Pending impact queues and diagnostics
are transient and are not restored/replayed; consume them into application state
before checkpointing if required. Parked editor undo subtrees retain their in-memory
queues. Concurrent material-layer extensions currently advance the shared writer to
v23; these v22 records remain part of v23. Deploy matching host/worker codecs.

Build with repository-local Emscripten pinned by .emscripten-version. Standard
Release build is `scripts/build.ps1 -Configuration Release`; isolated build paths
and exact commands/evidence are in [validation](fx-contacts-decals-validation.md).
Native targets fx_contact_tests plus FX/authoring/scene-archive/persistence/render
frame/mesh/plane/trail suites cover these contracts. Browser acceptance uses
`scripts/fx-contact-smoke.cjs` with Playwright/pngjs, NODE_PATH, BROWSER_EXECUTABLE,
DEMO_URL, EVIDENCE_DIR and optional DEVICE_SCALE_FACTOR=2. Physical mobile and
sustained performance require separate acceptance.
