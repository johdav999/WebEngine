# Event-driven FX composition

`EffectAsset` can contain immutable `EffectTrigger` rules. A rule names a source
emitter and Birth, Impact or Death event, and embeds a shared child EffectAsset,
an optional projected decal preset, or both. Applications still decide when to
play an effect and how surfaces, damage, audio or game rules select it.

```cpp
auto effect = std::make_shared<EffectAsset>();
effect->emitters.push_back({debrisDefinition, {}}); // Enable collision for Impact.
EffectTrigger impact;
impact.emitter = 0;
impact.event = ParticleEventType::Impact;
impact.effect = sparksAndDust;
impact.alignNormal = true; // Child +Y follows the event normal.
impact.inheritVelocity = .2f;
impact.decal.emplace();
impact.decal->settings.texture = scorchTexture;
impact.decal->settings.lifetime = 5;
impact.decal->settings.fadeOut = 1;
impact.decal->scale = {.6f, .2f, .6f};
effect->triggers.push_back(impact);
effect->validate();
auto& instance = actor.addComponent<EffectComponent>("Impact FX", effect);
```

The supplied `impactChainEffect()` example combines falling debris, contact sparks,
dust and fading scorch marks. It is also the editor's Impact chain preset.

## Events, execution and ownership

ParticleEmitterComponent optionally captures bounded `ParticleEvent` records via
`captureEvents(mask, capacity)`. Bits 1/2/4 select Birth/Impact/Death. Capture is off
by default. Capacity is 1..4096, default 256. `events()` describes the latest
variable update and is cleared before the next tick, including paused ticks;
`droppedEvents()` counts overflow since restart. Existing `impacts()` is a separate
application-owned queue and is neither cleared nor consumed by composition.

Records contain world position, normal and velocity. Birth/death use the stored
particle velocity and plane normal; impacts use contact point, sweep normal and
incoming velocity including over-life motion. Collision kills produce Impact and
Death. Particles born and expired within one update can produce both events.
Restart/clear is cancellation and does not produce Death. Skipped emissions never
produce Birth. Quad and mesh emitters are supported; ribbon and supplied-contact
surface trails reject particle-event rules. Impact rules require collision on the
source emitter. Uncaptured event kinds retain existing expiration optimizations.

`Component::postTick(delta)` is an optional variable-update hook, called after
existing children finish their ticks, only while the component is initialized,
active and tick-enabled. It does not run during synchronize or fixed ticks.
EffectComponent consumes its direct emitters' records here, in emitter/event/rule
order. It appends ordinary owned child components after the existing traversal;
new descendants initialize and begin simulation on the next scene update. There
is no reentrant simulation, particle callback, worker mutation or second clock.
Fixed simulation and presentation/interpolation remain independent.

Generated child effects/decals remain owned by the source EffectComponent but keep
their spawn pose in world space. At variable ticks they compensate for source
movement. Parent transforms that cannot represent the required local TRS reject
a spawn; an existing generated instance is retired if anchoring later becomes
unrepresentable. Singular/sheared hierarchies therefore need an independent root
for reliable placement. Birth/death orientation follows the event's plane normal;
Impact follows the receiver normal. Child emitter transforms still apply inside
the placed child effect. Velocity inheritance adds a world velocity once at each
child particle birth, converting to local space when necessary; the multiplier
is finite 0..10, default zero. Receiver motion is not automatically tracked.

Effect pause/resume propagates to descendant emitters and freezes managed decal
age. `stop(false)` disables all further triggers and emission while particles and
marks drain. `stop(true)` or restart removes generated descendants through normal
deferred destruction; flush destruction or update before saving. Source removal
owns all generated descendants. Disabling a hierarchy freezes it. Keep the owning
EffectComponent tick-enabled when composition is active. No renderer/camera state
drives event production or lifetime. Scenes continue to outlive borrowed views.

Completed child effects are reclaimed after emission, particles, trails and owned
descendants finish. Looping children intentionally remain active until stopped.
`particleCount()` includes descendant effect particles; `playing()` retains the
existing direct-emitter emission meaning. `compositionStats()` reports descendant
effect/decal counts, successful spawns and dropped rule spawns/event overflow.
Counters and event output are transient diagnostics.

## Bounds and validation

Assets allow 1..64 emitters and up to 64 rules. The complete shared graph allows
up to 64 distinct assets and eight reference edges in a path. Cycles, missing
targets, invalid source indices, invalid transforms/settings and unsupported
source modes reject the complete candidate before publication. Immutable graph
assets must not be mutated through retained mutable aliases after publication.

`EffectLimits` configures the following bounds. The root's active, spawn and depth
limits govern every generated descendant; event capacity is configured per asset:

| Setting | Default | Accepted range |
| --- | ---: | ---: |
| maxActiveEffects | 64 | 1..256 |
| maxActiveDecals | 128 | 1..512 |
| maxSpawnsPerUpdate | 64 | 1..1024 |
| maxDepth | 4 | 0..8 |
| maxEventsPerEmitter | 256 | 1..4096 |

The root resets its spawn allowance before descendant updates. Effects and decals
each cost one spawn; a combined rule admits each target independently. Excess
events/spawns are dropped in stable traversal order, with no deferred backlog.
Depth zero disables spawned targets. Descendant counts exclude pending removals.
Child assets retain their own limits for standalone use; the root's active/spawn/
depth limits govern nesting, while each asset's event queue capacity configures its
own emitters. Per-emitter particle/collision bounds still apply. These are explicit
bounded CPU allocations and scene nodes, not GPU simulation or a global scene FX
pool. Large child definitions/active counts still require application budgeting.

## Managed decals

DecalSettings adds `lifetime` and `fadeOut` in seconds, both zero by default.
Lifetime zero preserves persistent decals. Lifetime is 0..86400; fadeOut must be
0..lifetime. Triggered decal presets require positive lifetime and default to five
seconds with one second of fade. Scale defaults {.5,.2,.5}, must be finite and
positive, and is bounded to 1000 units per axis. Other settings and receiver
exclusions follow the [projection contract](fx-contacts-decals.md).

`age()` advances during scene variable updates. During the final fade interval,
`effectiveOpacity()` multiplies the authored opacity by remaining time / fadeOut.
Fade does not replace settings, rebuild projection geometry each frame, modify
receiver materials or depend on culling. Expiration requests normal component
destruction. Setting fadeOut to zero retains full opacity until expiration.
Settings edits that would put a positive lifetime before the current age reject
without changing the current settings or clock; restarting an effect creates fresh
decal instances.

## Authoring and persistence

Particle FX -> Event composition authors rules for the selected emitter: event,
orientation, velocity multiplier, child preset or imported .wefx, optional decal
texture/tint/opacity, volume, lifetime/fade, and root limits. Removing an emitter
removes its rules and remaps later indices; duplicating an emitter retains the
original rules on the original emitter. Child imports are explicit replacement
operations with stale-document checks. Source edits use existing immutable effect
history, validation and undo/redo. Nested assets can be authored separately and
imported as children; this is not a recursive node-graph UI.

Preview runs the complete chain in its isolated scene, creates a collision floor
when any referenced effect needs one, and reports active effects/decals and budget
drops. Pause/step/restart and source-preserving close/Play retain normal ownership.
Standalone decal Details properties also expose lifetime/fade. Audio and physical
surface dispatch remain application code.

Archive v29 retains embedded shared graph dependencies, rules/limits, inherited
velocity, child instance state, world anchors, managed decal age and pause/stop
state. Older scene/effect files read with no rules and persistent decals. Generated
children serialize as ordinary owned nodes, so checkpoints retain their live pools;
event queues/counters and decal geometry caches are transient and never replayed.
The codec bounds recursive assets separately and rejects cyclic references. Use
matching main/worker artifacts. .wefx retains definitions/dependencies, not live
instances. See [validation](fx-composition-validation.md) for builds and acceptance.
