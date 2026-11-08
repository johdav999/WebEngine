# Cloth milestones six to eight

These optional extensions belong to `WebEngine::Clothing`. Browser and Windows
hosts use the same portable solver and dynamic mesh projection. See
[prior collision contracts](cloth-edges.md) and [validation](cloth-materials-motion-validation.md).

## Milestone six: weighted fabric

`ClothDefinition::inverseMass` is empty by default, preserving unit free-particle
mass. An authored vector has one finite value in [0.001,1000] per vertex. A lower
value means a heavier vertex. Zero `maxDistance` remains the only pinning rule,
regardless of its authored mass. Zero inverse mass is rejected to avoid a second,
conflicting attachment mechanism. Mass weights apply to XPBD links and particle,
vertex/face and edge/edge contact. Gravity and other supplied acceleration remain
mass independent; kinematic obstacles receive no reaction. Tethers remain hard
per-vertex bounds. Author masses consistently with mesh resolution and world units.

## Milestone seven: aerodynamic airflow

Set `ClothDefinition::aerodynamicDrag` in [0,4] (default zero/off), using the
existing validated `surfaceTriangles`. Supply `ClothEnvironment::wind` with world
air velocity (each component within +/-100) and density [0,10] (default 1.225).
Gravity remains the separate acceleration argument. Zero velocity means still air,
which can resist moving fabric; density zero disables drag entirely.

Each fixed substep samples a triangle's current area and mean particle velocity,
including animated pin velocity. Normal relative airspeed produces two-sided
quadratic pressure `0.5 * density * drag * area * vn * abs(vn)`. The force is
distributed equally to its three vertices, then multiplied by inverse mass.
Reversed winding gives the same force, tangential air produces no normal pressure,
and collapsed faces are skipped. Relative normal speed is capped at 100 and
accumulated aerodynamic acceleration magnitude at 10000 per vertex; the existing
final speed cap of 50 still applies. This is a bounded pressure/drag approximation,
not lift, turbulence, fluid coupling or a high-stiffness implicit integrator.

`ClothStats::aerodynamicFaces` counts nonzero face-force evaluations, retained across
reset and frozen while paused. The topology currently also supplies self-contact
and retains its positive self-separation requirement. The shared demo authors a
heavier hem and uses air velocity for its wind toggle on browser and Windows.

## Milestone eight: translating collision proxies

`ClothEnvironment::capsuleTranslations` and `triangleTranslations` optionally
borrow one world-space displacement per supplied capsule or environment triangle.
An empty span preserves static-snapshot behavior. Shapes are supplied at their
**end** position; start position is end minus translation. Values must be finite,
each component within +/-100, and both endpoints within the existing coordinate
bound. There are no retained pointers or implicit collider identities.

The application owns stable ordering and motion accumulation. Retain pending
translation until `steps` advances; a call with no complete fixed step consumes
none. Clear pending motion when `resets` changes, after teleport/seek, and on outfit
replacement. All garments sharing motion must have aligned step/reset histories;
otherwise update them separately. The example accumulates obstacle displacement
until its cape actually steps. No motion is inferred for rotating body capsules.

Motion is linearly split across the executed substeps, like animated targets.
If the catch-up budget drops time, the full endpoint motion is compressed across
those executed steps. With `sweptCollision`, queries transform the old particle
into each collider's end frame before applying existing bounded sweep geometry.
Without sweeps, motion still interpolates and contacts remain discrete. Contact
velocity uses translation per executed substep; friction removes tangential
**relative** velocity and inward relative normal velocity, then restores collider
velocity. The existing final particle speed cap still applies. Multiple contacts
retain the last projected normal/velocity; no contact manifold or rigid-body
reaction is added. `movingContacts` counts projections with nonzero translation.

This supports translation only: capsules keep shape/radius and triangles keep
orientation/shape. Rotating/deforming histories, moving planes/boxes, inter-garment
collision and universal CCD guarantees are not provided. Reset and pause retain
diagnostic totals; invalid inputs publish no particle or diagnostic state.

```cpp
ClothEnvironment environment;
environment.triangles = currentWorldTriangles;
environment.triangleTranslations = pendingTriangleTranslations;
environment.wind = {{0, 0, -12}, 1.225f};
auto before = outfit.clothStats(scene);
outfit.advanceCloth(scene, simulationDelta, bodyCapsules, {0,-9.81f,0}, environment);
auto after = outfit.clothStats(scene);
if (after.steps != before.steps || after.resets != before.resets)
    clearPendingTranslations(); // application-owned history, not a library API
```

## Build and validation

Use the existing clothing CMake options, `clothing_tests`, and browser
`clothing_demo` / Windows `clothing_demo_windows`. Run `clothing_contracts`,
`scripts/clothing-browser-smoke.cjs`, and native `--d3d12 --smoke output-directory`.
VS2022 supports the direct-D3D12 configuration; the pinned Dawn binary requires
a newer compatible MSVC runtime. See [native setup](windows-native.md).
The shared demo exercises all three extensions; browser state exposes
`aerodynamicFaces`, `movingContacts`, `obstacleMotion` and `milestone: 8`.

