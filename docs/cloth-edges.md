# Cloth milestone five: edge-to-edge self-contact

`WebEngine::Clothing` adds optional edge/edge self-contact to [M4](cloth-surfaces.md).
It catches crossings through edge interiors where no vertex approaches an opposing
triangle. This remains a portable CPU feature within one garment, shared by the
browser and Windows hosts, without another clock, renderer or physics dependency.

## Enable it

```cpp
definition.selfCollisionDistance=.05f;
definition.surfaceTriangles=garmentFaces; // Actual indexed mesh triangles, as in M4.
definition.edgeSelfCollision=true;        // Default false; requires nonempty topology.
definition.sweptCollision=true;           // Optional linear edge sweeps, as well as M4 sweeps.
```

Unique undirected edges are derived from validated `surfaceTriangles`, sorted by
endpoint indices and owned by each solver. Shared triangle edges appear once;
there is no separate editable edge list or runtime topology mutation. Enabling
edge contact requires at most **2048 unique edges**, in addition to M4's vertex and
face bounds. Invalid topology/budgets reject before publishing a solver or outfit.
The old default is preserved: no edge work occurs with the flag off.

Edges sharing a vertex, or having any directly linked, triangle-adjacent or
bind-space-near endpoint pair, are excluded using the existing immutable neighbor
mask. This intentionally excludes local mesh neighborhoods and welded duplicates.
Rest-near uses M3's 1.01 times selfCollisionDistance threshold. Author shared mesh
indices/seams and sensible spacing; no automatic sewing or garment fitting occurs.

## Detection and response

Each constraint iteration derives current (or swept) edge bounds and uses a stable
sweep-and-prune pass to reject distant pairs. Closest-point segment queries handle
finite endpoints, parallel overlap and collapsed runtime edges. Parallel overlap
chooses its middle; exact coincidence uses a deterministic perpendicular normal.
This is a separation rule, not a way to recover an originally intended fold side.

The two closest-point parameters supply four signed endpoint weights. Separation
uses the sum of squared weights times inverse mass; free endpoints share the
response and pinned endpoints remain fixed. An all-pinned contact is skipped.
No extra edge friction is added; velocity reconstruction/damping/limits remain M2's.

With sweptCollision disabled, only the candidate endpoints are tested. When enabled,
all four endpoints interpolate linearly from the start of the current 1/120-second
substep. Conservative advancement uses the sum of each edge's maximum endpoint
travel as a distance-speed bound. Searches are limited to 64 queries with a 1e-5
distance tolerance. Exhaustion conservatively retains the last checked separation
plane, which can produce early/false contacts. Previous/candidate positions and
query results are transient; no external pointer or scene object is retained.

Order is links, tethers, particle self-contact, vertex/triangle self-contact,
edge/edge self-contact, then external capsules/boxes/triangles/planes. A correction
can create new contacts outside the pass's initial bounds; these are rediscovered
in later iterations. External clearance takes priority. Conflicting constraints,
initial intersections, fast nonlinear motion and finite iterations may leave
intersections. Sweeps reduce tunneling but do not guarantee globally intersection-free
cloth or full mesh/mesh CCD. Inter-garment collision, swept particle pairs and moving
external-proxy history remain unsupported.

Worst-case edge work is quadratic in edge count, multiplied by iterations/substeps
and bounded search work. Bounds are safety limits, not dense-garment performance
targets. Keep collision meshes small; render culling and LOD never change simulation.
The cape example enables the flag on its existing 117-vertex topology in both hosts.

`ClothStats::edgeContacts` counts applied edge-pair projections and contributes to
`selfContacts`; swept edge projections also contribute to `sweptContacts`.
`surfaceContacts` continues to count vertex/triangle projections only. Counts are
not unique impacts. Reset retains them; replacing an outfit creates new solvers.
The clothing wrapper aggregates counters over equipped garments. Paused simulation
does not change particles, render revisions or counters.

All M2–M4 ownership, rigid transforms, time bounds, candidate publication, reset and
outfit persistence contracts remain unchanged. Edge state is transient and is not
saved in `WECL 1`. Existing build options and [commands](clothing.md#build-and-validate)
apply. See [M5 evidence](cloth-edges-validation.md) for actual platform coverage.
