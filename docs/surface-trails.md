# Surface-following trails

Driving Stage 6 adds `ParticleRenderMode::SurfaceTrail` to the existing optional
CPU FX system. It has an explicit surface alignment contract, separate from
camera-facing Ribbon mode. Positions and normals come from the application;
the engine never queries or modifies collision to create these marks.

```cpp
auto definition = std::make_shared<ParticleEmitterDefinition>(
    *surfaceTrailEffect()->emitters[0].definition); // engine/effect_presets.hpp
definition->trailMaxPoints = 512;
definition->trailLifetime = 12;
definition->size = {{0, .32f}, {1, .32f}}; // Full width, world metres.
auto& marks = scene.addChild<ParticleEmitterComponent>("Contact marks", definition);
// Before each scene variable update, after obtaining authoritative contact data:
if (contact && shouldLeaveMark)
    marks.submitSurfaceContact(contactPosition, contactNormal);
// The normal Application scene update consumes the contact. Do not update twice.
```

Games own tyre/slip thresholds, supported surfaces, wheel-to-emitter association,
receiver changes and query timing. Use one emitter per independent stroke stream.
Submit at most one intended endpoint per variable update; the last valid submission
wins. Passing no contact for an update creates a gap, including airborne motion.
Submission is owner-thread transient input, not a callback/subscription or a queue
of fixed-step contacts. If physics runs several steps per frame, applications choose
the contact to present. No interpolation can reconstruct unobserved hills/motion.

## Sampling and bounds

World space is required. Emitter/parent transforms and scale never move old marks
or resize their width. Contact coordinates and normal components must be finite
and within +/-1,000,000; normals must have length at least 1e-6 and are normalized
before publication. Bad submissions preserve the previous pending contact and all
live state. Local space, particle collision and offsets outside +/-1 metre are
rejected when configuring SurfaceTrail. The shared definition's other validation
continues to apply. Alignment/planeNormal do not override supplied trail normals.

The existing trail settings provide fixed capacity (2..4096 points), positive
lifetime <=86400 seconds, minimum sample distance >=.0001, teleport break distance
>=sample distance and texture repeat length >=.0001. Preset defaults: 512 points,
12 seconds, .05 m sampling, 2 m break, 1 m texture repeat, .015 m normal offset,
.32 m full width, dark alpha colour fading over the last fifth of life. Width and
RGBA use the existing normalized-age curves. Rate, bursts, particle lifetime,
birth shapes, velocity, gravity and flipbooks do not generate strip geometry.

Sampling retains residual distance and interpolates positions, birth times and
normalized normals between submitted endpoints. More than 256 required new samples
in an update drops that sampling interval and starts a new strip. Capacity evicts
oldest points; `droppedParticles()` includes evictions and work-limit drops.
`trailPointCount()` / checked `trailPoint(i)` expose the bounded oldest-first ring.
Opposed/orthogonal endpoint normals, teleport distance, missing contact updates,
`breakTrail()` and resume create new strokes. Texture distance resets on a new
stroke; eviction does not reset surviving texture phase.

Scene variable updates own ages; pause freezes age, stop drains, stop(true) clears,
and play(true) resets. Disabled hierarchies freeze/hide state; applications must
break or clear at disabled movement/control handoffs. A supplied contact is consumed
once. `Scene::synchronize()` and rendering never sample or age the trace.

## Geometry, receivers and presentation

`SceneFrame::ribbons` carries `surfaceAligned`, endpoint normals and ordinary
texture/blend/width/colour data. `FxFrame` expands endpoint sides from path tangents
cross supplied normals. Endpoint widths lie in their supplied tangent planes;
orientation is independent of camera. Invalid projected tangents or reversed side
directions suppress the segment instead of producing camera-facing geometry.
Shared endpoint tangents provide simple joins, without miter/round caps or terrain
projection of the full width. Coarse samples may cut through curved surfaces or
span holes: choose contact spacing/width and explicit breaks for the receiver.

At most capacity segments (including one transient head), twice that many triangles,
and six 48-byte vertices per segment are produced per emitter/view. Ring storage is
capacity times `sizeof(TrailPoint)`. Existing retained geometric GPU buffers,
transparent sorting, visibility masks, depth test/no depth write, alpha/additive
blend, fog, texture repeat and optional soft-depth fading apply on WebGPU/WebGL2.
Counters remain `trailSegments`, `trailDrawCalls`, `trailUploadBytes`; suppression
can make rendered counts smaller than collected candidates. Applications bound
aggregate emitter/view counts. There is no claimed maximum-budget performance test.

Marks bake supplied world positions. Receiver movement, removal, terrain edits,
stream/page eviction and unsupported/deforming receivers do not move/reproject or
automatically remove them. Games may break, clear or replace the relevant stream
when receiver identity/revision changes. The engine retains no receiver pointers,
physics handles or terrain pages. Use DecalComponent for independent discrete marks.

## Editor and persistence

Details -> Particle FX -> Surface trail creates the reusable preset with existing
immutable history, duplication and isolated Play. Trails contains rendering mode,
capacity/lifetime/sample/break/repeat and offset controls. Shape -> Simulation space
must remain World; invalid edits preserve source and history. Size/RGBA curves and
appearance texture/blend/soft fade use the existing FX authoring controls.

The isolated FX preview supplies an orbit on a sloped receiver, including an
airborne interval each four seconds. Move trail source, pause/restart/step/isolate,
point/drop/segment/upload diagnostics and renderer recovery reuse EffectPreview.
It owns the receiver and synthetic contact policy; it never samples the authoring
scene or creates physical contact. This is a generic contact example, not automatic
tyre emission for arbitrary vehicles.

Archive **v28** introduces authored mode/settings and bounded
live point normals plus previous source normal, ring, age clock, distance residual,
breaks and playback state. Older trails default normals to +Y. Pending contacts,
receiver references and render caches are transient. Resume continuation by
resubmitting the next contact before the next update; without a submission it
starts a gap. Load rebuilds runtime handles/buffers. Scenes, asset libraries,
`.wefx`, duplicate, Play clones and application envelopes share this codec. Source
allocation limits and normal/chronology checks apply before publication. Deploy
matching host, worker and Wasm artifacts. The current shared checkout writes v29
and reads 1..29 because separate FX composition work extends the same envelope;
Stage 6 remains the v28 increment, and an actual v28 effect fixture is tested.

Validation: `surface_trail_tests`, CTest `surface_trail`, optional
`surface_trail_wasm_tests`, `scripts/surface-trail-smoke.cjs`, existing trail/depth
and editor FX suites. Use the pinned local SDK and the build commands in README.
See [validation evidence](surface-trails-validation.md).


Native Dawn and direct D3D12 support this presentation contract; see
[native FX scope and validation](windows-fx-rendering.md).
