# Cloth collision: milestone three

`WebEngine::Clothing` adds opt-in particle self-contact and explicit environment
planes/boxes to the [M2 solver](cloth-dynamics.md). These are portable CPU mechanisms
shared by browser and Windows. No renderer, physics dependency or frame phase changes.

## Usage

Set `ClothDefinition::selfCollisionDistance` before constructing the garment or solver.
Zero (default) preserves M2 behavior. Enabled distance is in [.001,1] world units.
It is the minimum separation between eligible particle centers, independent of the
external `thickness`. Author it for mesh spacing; the sample cape uses .075.

```cpp
std::array<engine::ClothPlane,1> floor={engine::ClothPlane{{0,1,0},0}};
std::array<engine::ClothBox,1> blocks={engine::ClothBox{{-1,0,-2},{1,1,-1}}};
outfit.advanceCloth(scene,simulationDelta,bodyCapsules,{0,-9.81f,wind},
                    {floor,blocks});
```

The trailing `ClothEnvironment` is optional on both `advanceCloth` and
`ClothSimulation::advance`; existing calls remain valid. Spans are borrowed only
during the call, never retained. Applications own collider placement and lifetime.
Boxes are solid world-space axis-aligned bounds; use capsules for round obstacles.
Planes define an allowed half-space `dot(normal,position) >= offset`, with a unit
normal. Free particles stay an additional `thickness` outside the forbidden volume.
Boxes expand by thickness on each axis, conservatively squaring their corners.
The nearest face resolves initial box penetration with deterministic tie order.

At most 16 planes and 32 boxes may be supplied, in addition to 32 capsules. Bounds
and offsets must be finite within +/-1e6; box extents must be at least 1e-5 on each
axis. Plane squared normal length must be within 1e-4 of one. Invalid solver input
rejects before state publication, including counters and time remainder. The
clothing wrapper stages all active garment simulations and render streams before
committing. Paused/inactive clothing returns without inspecting unused colliders.

## Solver behavior and limits

Each constraint iteration solves links, tethers, self-contact, capsules, boxes,
then planes. Free particles have equal inverse mass; pins never move for contacts.
Pairs joined by any authored link, or within 1.01 times the separation distance in
bind space, are excluded to avoid fighting local mesh constraints and welded seams.
These exclusions are built once; reset preserves them. Coincident eligible particles
separate along their bind-space difference. A deterministic spatial grid is rebuilt
each iteration and searches neighboring cells. Corrections that create new contacts
are revisited on subsequent iterations. Worst-case dense folds remain quadratic,
bounded by the existing 1024-particle/16-iteration/8-substep limits. Small meshes
are recommended; these limits are safety bounds, not performance guarantees.

External contacts retain M2 friction and speed limiting; self-contact only applies
position separation. External clearance takes priority over self-contact/tethers.
Conflicting or overlapping constraints may not all converge within the iteration
budget. Pins must be authored outside solids. Reset and automatic teleport rebind
without collision projection until the next simulated step. There is no implicit
Jolt query, reaction force, retained scene reference or automatic collider discovery.

M3 is discrete **particle** self-contact within one garment. It does not prevent
triangle interiors or crossing edges from intersecting, guarantee continuous
collision under fast motion, or collide different garments with each other. Mesh
surface contact and bounded linear sweeps are added in [M4](cloth-surfaces.md);
tearing, fitting, GPU simulation and two-way body coupling remain future work.
Moving external colliders supply only their current endpoint;
there is no swept motion or surface-velocity transfer.

`ClothStats::selfContacts` and `environmentContacts` count applied pair/proxy
projections cumulatively, not unique contacts. Reset retains counters; replacing a
garment starts a new solver. `clothStats(scene)` sums currently equipped garments.
Existing pause, 120 Hz stepping, bounded catch-up, ownership, dynamic stream,
rigid-transform and outfit persistence contracts remain unchanged.

Both host examples enable cape self-contact and supply a floor and visible static
block from their shared scene. The browser exposes contact counters for smoke tests;
Windows `--smoke` waits for environment contact as well as movement/pause checks.
Build commands remain in [clothing.md](clothing.md#build-and-validate). Evidence and
platform gaps are recorded in [M3 validation](cloth-collision-validation.md).
