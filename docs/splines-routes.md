# Splines and procedural route geometry

Stage 5 adds optional generic cubic curves and cross-section sweeps to
`engine_core`. Road lanes, racing lines, junction topology, traffic and terrain
change policy belong to applications. Both browser renderers consume the ordinary
shared Mesh; there is no road-specific shader or camera-dependent collision.

```cpp
#include "engine/route.hpp"
SplineData source;
source.points={{{0,0,0},{0,0,20},0},{{3,1,20},{4,0,20},.1f},
               {{8,0,40},{0,0,20},0}};
auto spline=SplineAsset::create(source);
auto sample=spline->atDistance(12); // Local position, unit tangent, bank, parameter, distance.
auto nearest=spline->nearest({2,0,9}); // Empty when the requested query budget is insufficient.
RouteData sweep;
sweep.spline=spline;
sweep.profile=SweepProfile::create({}); // Default open section (-3,0)..(3,0).
sweep.collision=true;
auto route=RouteAsset::create(sweep);
scene.adoptChild(makeRouteActor("Path",route));
scene.synchronize(); // Reconcile static physics before querying/driving.
```

## Curves, sampling and limits

`SplineAsset` owns an immutable `SplineData` copy. Open curves require 2..256
knots, closed curves 3..256; closed input omits a repeated last knot. Each knot
stores local position, a nonzero Hermite tangent in metres per unit segment and
bank in radians. A single tangent serves both adjacent segments, giving C1
position continuity, including the closed seam; C2 curvature is not guaranteed.
Bank interpolates linearly without angle unwrapping, and is C0 at knots. Positive
bank raises the right edge of a forward +Z flat strip.

Positions/tangent components are finite within +/-100000; tangent magnitude is
[.0001,10000], bank [-pi,pi]. Coincident adjacent knots, stationary derivatives
(including analytic zeros between samples), sampled cusps/unresolved near-reversals
and curve lengths outside [.0001,1000000] reject construction. This does not solve
global self-intersection or infer junction meshes.

`sample(parameter)` uses one parameter unit per cubic segment. Finite open
parameters clamp to endpoints; closed parameters wrap, including negative values.
`atDistance` clamps/wraps source arc distance in the same way. Arc length uses a
fixed chord table, `samplesPerSegment=64` by default, configurable 8..128. It is an
approximation; increase density or subdivide knots to meet an application's error
requirement. The recorded tests establish convergence for their fixtures, not a
universal centimetre error bound for every allowed cubic.

`nearest(point,maxIntervals=32768)` scans that bounded table, projects onto its
segments and refines the best interval with 16 ternary iterations. It returns an
approximate nearest sample and squared distance. It is not an exact polynomial
closest-point solver. A positive budget smaller than the complete table returns
empty, never an unlabelled partial best. Invalid points, nonfinite parameters and
budgets outside 1..32768 throw. Sampling also rejects numerically unresolved
derivatives below 1e-6; use well-scaled source-local knots. Curves retain no
scene/node/callback references.

## Sweep frames, profiles and rebuilds

Immutable `SweepProfile` owns 2..32 across/up pairs for an open section, or 3..32
for a closed perimeter, plus shared material and optional PhysicalSurface. Bounds
are +/-1000 metres and adjacent edges must have nonzero length. An open left-to-
right flat strip faces upward. A closed profile traverses clockwise in across/up
space for outward-facing normals. Profile edges have hard normals; there are no
inferred end caps or solid-volume semantics.

`RouteAsset` combines shared spline/profile references with spacing (default 1 m,
[.01,100]), texture length (default 4 m, [.01,10000]), collision (default false) and
`maxRings` (default 1025, 2..4097). Sampling uses equal approximate arc intervals,
never camera LOD. The initial frame uses source-local +Y as preferred up, with a
stable +Z fallback for near-vertical tangents. Parallel transport carries the
frame, then closed-loop holonomy correction is distributed by distance and authored
bank is applied. Near-reversals between rings and locally folded/degenerate sweep
triangles reject construction; refine spacing/knots or narrow the profile.

UV X measures profile-edge distance / texture length; UV Y measures travelled
curve distance / texture length. The closed curve duplicates the first geometry
ring with the full end distance, preserving texture density; the texture phase at
that cut is not automatically forced to an integer repeat. Closed profiles duplicate
edge endpoints for hard corners and their perimeter UV seam. Tangents and bounds
are generated before publication. The final closed frame is made identical to
the first, and analytic cross-section tangents agree across the distance-UV seam.

Rebuilding is synchronous on the idle owner, with **complete candidate publication**.
There is no hidden per-frame rebuild, automatic chunk admission or worker job.
Candidates exceeding maxRings, 262144 vertices, 262144 triangles or a conservative
64 MiB geometry/collision/cooking estimate reject before mesh generation. Archive
loading additionally charges source/derived allocation against ArchiveLimits.
Choose coarser spacing, smaller sections or application-owned separate route actors
for long paths. Editor history uses the existing count limit (default 128), so hosts
should choose smaller history/rebuild limits for large authored resources.

`RouteComponent` is an identity-local MeshComponent on its own scene-root actor.
Position/rotate its actor; collision requires unit world scale. Required protected
static-body/collider children are managed by the component. Physics now accepts
such protected children under one identity-local component container; moving bodies
retain direct attachment. One active body/collider per actor remains the rule.
Disable/removal stops registration/queries through the ordinary hierarchy lifetime.
`configure(shared<RouteAsset>)` requires an idle hierarchy; synchronize before
physics queries. Never independently edit its generated mesh, required children
or their transforms. Ordinary material overrides remain per-instance.

Collision uses exactly the generated vertices/triangles and profile physical
surface. It is static, optional and independent of culling, camera interpolation
and mesh display policy. Rebuilds invalidate physics contacts/joints according to
existing body-rebuild rules; Undo restores authored resources, not solver history.

## Editor and persistence

Details -> **Splines and swept routes** provides open banked-road and closed-loop
presets, point insertion/removal, position/tangent/bank fields, profile coordinates,
physical-surface selection, UV/spacing/ring budgets and collision toggling. Choose
an actor or RouteComponent. Point/tangent markers preview the current authored
curve; collision preview uses the existing physics lines. Draft forms stay local
until **Apply grouped edit**, which builds a complete candidate and publishes one
EditorSession command. Snap rounds world point positions to the editor grid;
tangents remain authored. Discard, selection/document changes and Play cancel drafts.
Invalid edits preserve the prior geometry/collision/history. Structural duplication
shares immutable source and generated resources; editing a binding creates a copy.
`createRoute`, `selectedRoute`, `setRoute` are the portable editor operations.
`registerBuiltinPrefabComponents` includes a RouteComponent factory accepting a
`shared_ptr<const RouteAsset>`; use its own top-level prefab actor.

**Sample terrain at points** is an explicit all-or-nothing operation over resident
terrain using a 20 km vertical span centred on each current point: it bakes
control-point positions plus .02 m clearance, leaving tangents
and bank authored. Every point needs a terrain hit. The result is ordinary spline
source data; no terrain pointer, saved dependency or automatic live re-conform
subscription remains. Resample after terrain changes. This does not guarantee the
entire swept section stays above terrain between knots.

Scene/application/effect envelopes now write **v27**, retaining readers 1..27.
A built-in RouteComponent record stores shared RouteAsset -> SplineAsset /
SweepProfile -> material/surface references. Decoding bounds source counts before
allocation and rebuilds each distinct route once under load budgets. Generated
mesh/shape payloads are omitted; required child IDs/common settings remain saved
and are validated against reconstructed geometry. Duplicate/save/load and isolated
Play preserve sharing between route instances. Orphan spline/profile libraries
are not introduced: keep a RouteComponent reference or supply application-specific
asset persistence. A generated mesh explicitly saved as an independent ordinary
MeshComponent is a separate baked resource on reload, rather than a route-cache
reference. Scene saves never depend on GPU cache objects.

## Example and verification

Open `?editor=1&scene=route`, press Play, then drive using the existing vehicle
controls. `src/route_example.hpp` / the browser host own the elevated curved/banked
road, asphalt assignment and car placement. The closed editor preset is lifted
above the ground for clear inspection. These are application choices, not engine
road/lane semantics.

Run native `spline_route` and the complete CTest suite; build/run the explicit
`spline_route_wasm_tests` Node target. `scripts/spline-route-smoke.cjs` exercises
both forced renderers; repeat with `DPR=2`. Keep scene-file bytes inside the page,
not a large JavaScript number array transported through the test runner. Include
vehicle/physics/editor/archive regressions when changing these integrations.
See [recorded validation](splines-routes-validation.md). Desktop browser evidence
does not establish physical mobile acceptance or long-route performance.
