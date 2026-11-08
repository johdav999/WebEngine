# Physical clothing: milestone two

Milestone two extends `WebEngine::Clothing` with portable CPU cloth dynamics.
It adds a cape with animated pins, distance constraints, gravity/wind acceleration,
body capsule collision, per-character state and bounded fixed stepping. The same
implementation and dynamic vertex stream run through browser WebGPU/WebGL2 and
Windows Dawn/direct D3D12. See [validation](cloth-dynamics-validation.md) for the
platform checks actually performed, and [outfit contracts](clothing.md) for M1.

## Author a physical garment

Set `ClothingItem::cloth` to a shared immutable `ClothDefinition`. The garment's
mesh still uses the driver's exact skeleton and mesh bind space, as in M1. Supply
one `maxDistance` per vertex: zero pins the vertex to its animated, world-space
skinning target; positive values bound its distance from that target. At least one
pin is required. Uniform unit particle mass is used for free vertices.

Author `ClothLink {a,b,compliance}` pairs for structural, shear and bend distances.
Rest lengths come from mesh bind positions. Compliance zero is a hard distance
constraint; larger values soften it. The example uses 1e-7 structural, 1e-6 shear
and 2e-4 two-edge bend links. This is distance-based bending, not an angular shell
model. Duplicate/degenerate links, invalid indices and nonfinite parameters fail
catalog validation before outfit replacement. Authored morph targets cannot be
combined with physical clothing in this milestone. Cloth should use a connected
triangulated surface with shared vertex indices; welded seams are an authoring task.

The sample cape has 117 vertices, pinned along its shoulder row. Both hosts expose
cape on/off, wind on/off, reset and teleport. Its item/color selection uses the
existing `WECL 1` format; particle positions/velocities are transient and restart
from the animated garment when equipped or loaded. Other garments retain M1 behavior.

## Simulation ownership

```cpp
engine::ApplicationFrameCallbacks callbacks;
callbacks.afterSimulation=[&](engine::Scene& world,const engine::ApplicationFrame& frame) {
    outfit.advanceCloth(world,frame.simulation.delta,worldCapsules,{windX,-9.81f,windZ});
};
callbacks.present=[&](engine::Scene& world,const engine::ApplicationFrame&) {
    outfit.synchronize(world);
};
```

Call `advanceCloth` exactly once per simulation frame, after the character's
animation/IK and movement. `Application` also invokes afterSimulation while paused;
use `frame.simulation.delta` (zero while paused), never presentation delta or host
wall-clock time. There is no renderer-owned simulation or second animation player.
Disabled character hierarchies do not advance. Visibility masks, culling and LOD
do not control the solver. A zero delta preserves both simulation and vertex revision.

Particle state and capsules are world-space; the driver must have a rigid,
orientation-preserving unit-scale world transform. Apply asset sizing offline.
Nonuniform/uniform scene scaling and mirrored character transforms are rejected
for physical clothing; ordinary M1 clothing still supports its existing transforms.
Skinning supplies animated targets; the cloth result is transformed back to driver
space and rendered **without skinning again**.

Each garment accumulates the supplied simulation delta into fixed 1/120-second
steps, at most eight per call. Whole excess steps are dropped and counted in
`ClothStats::droppedSeconds`; a fractional remainder is retained. Delta must be
finite and in [0,.25]. Damping is exponential per second. Target endpoints are
interpolated across the steps in that call; this does not reconstruct unsampled
animation/physics motion. Acceleration is world-space, including gravity (default
`{0,-9.81,0}`); example wind is a time-varying acceleration, not an aerodynamic model.

Simulation is independent of presentation interpolation. Explicitly call
`resetCloth(scene)` after teleport, animation seek, origin rebasing or loading a
character, including while paused. It resets positions to current targets and
clears velocities/remainder. A target jump beyond `teleportDistance` (default 2)
automatically resets on the next positive-delta update. Pause alone does not reset.

## Collision and limits

`ClothCapsule {a,b,radius}` values are explicit world-space kinematic proxies;
equal endpoints produce a sphere. The application derives body proxies from its
character and can supply environment proxies too. The sample transforms torso and
leg capsules using the same pose palette and world transform as the character.
Capsules do not read or mutate Jolt bodies and receive no reaction impulse.

Each constraint iteration projects free particles outside capsule radius plus
cloth thickness. Pins always win; they must be authored clear of the body. Collision
is applied after tethers, so incompatible tether/body constraints can exceed a
tether distance. Overlapping proxies, fast motion and coarse triangles may still
intersect: this is discrete particle/capsule collision, not continuous or surface
collision. Friction damps tangential velocity; inward contact velocity is removed.
Free-particle speeds are capped at 50 world units/second for stability.

Bounds per garment: 1024 vertices, 8192 links, 1–16 iterations (default 8), 32
capsules per update. Maximum tether 100, thickness [0,1], damping [0,100], friction
[0,1], teleport threshold (0,100], compliance [0,1]. Position/target coordinates
are finite within +/-1e6 and each acceleration component within +/-1e4. These are
safety bounds, not a performance recommendation: the example is intentionally small.

`ClothSimulation` is independently usable with rest positions and a definition.
It owns copied settings and state, retains no scene/callback pointers, and stages
updates before publication. `ClothingInstance` stages all physical garment updates
and their render streams before committing any of them. Invalid solver data or
unsupported transforms retain previous simulated/rendered garment state.

## Rendering and lifetime

Physical garment nodes are ordinary `MeshComponent` draws. Virtual `morphPose()`
returns a complete `MorphPose` vertex stream with a new revision on publication.
Stream identity stays stable to reuse GPU buffers. Publish before collection and
rendering, as for ordinary morphs; retained stream pointers are live views, not snapshots.
This uses the existing CPU/GPU deformation upload, bounds, picking and shadow
paths. Normals and UV tangents are rebuilt from the simulated surface; double-sided
fabric is an authored material choice. Asset geometry is immutable. No renderer
backend interface, browser API or private graphics type enters the solver.

Generated cloth nodes follow the same scene ownership, handles, deferred character
destruction and archive-transient contracts as M1. Equipping/removing a cape stages
and retires its solver with the generated subtree. Unrelated instances retain their
own particle state. The game owns wardrobe/inventory policy and body proxy placement.

Milestone three adds [particle self-contact and plane/box environment collision](cloth-collision.md).
Triangle/edge contact, tearing, arbitrary mesh environment collision, automatic
fitting, GPU simulation and two-way rigid-body coupling remain future work.

## Build and checks

The [M1 build commands](clothing.md#build-and-validate) remain unchanged. The existing
`clothing_tests` now includes physical solver and scene integration cases;
`scripts/clothing-browser-smoke.cjs` additionally checks moving/frozen cape state,
teleport/reset and equip/unequip on both browser backends. Native `--smoke` includes
the same physical cape and pause lifecycle. Run Dawn and direct D3D12 separately.
