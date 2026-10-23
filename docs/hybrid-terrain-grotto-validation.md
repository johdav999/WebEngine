# Mesh-shaped cuts, enlarged grotto and basin validation

2026-10-23, local Windows desktop with repository Emscripten 6.0.11.
Evidence: `build/evidence/terrain-mesh-holes/`.

The last room now scales width, height and length by cbrt(10), about 2.1544,
for approximately ten times its former volume. It retains the −8 m walking
floor, a narrower connecting tunnel and a shared closed rear dome. A dry rock
basin is locally sculpted into the same floor, reaching −8.94 m beside the
unchanged centreline. Both hosts provide Lower grotto and Grotto basin views.

The demo also uses the new portable mesh-contact-rim hole preparation before
seam construction, preserving separate manual and fixed-cliff masks. Its shaped
rim retains four corner cells (74 default cut cells versus the old 78-cell
rectangle). Seam scans check both sides of every derived perimeter edge.
HYBRID11 retains rim ownership in saves/history; older openings remain unchanged
until an entrance/hole edit upgrades them.

Nine focused native CTests passed: terrain_mesh_holes, terrain_mesh, terrain_lake,
terrain_cliff, hybrid_cave_route, hybrid_terrain, terrain_lod_archive, terrain_seam
and terrain_hole_lod. The route suite covers the default entrance and eight
width/depth/height extrema, room dimensions, closed source topology, full motor
descent/ascent, basin collision, dry centreline and saved/restored geometry.
Older HYBRID5 single-cave and HYBRID6 straight-system saved fixtures also
passed native reopen and upgrade checks. Legacy geometry keeps its original
seam acceptance until an edit rebuilds it; shaped rims check the full perimeter.
The archive suite retains exact base/derived geometry, locks, repeated save/load,
invalid-load preservation and v35 mesh compatibility. The mesh-hole suite also
passed as Node/Wasm: concavity, conservative cells, islands/disjoint openings,
transforms, nonuniform sloping terrain, ownership, attribute splits/T-junctions,
source-only assets, patches, collision and atomic rejection.

Browser Chrome 155.0.8059.85 passed scripts/hybrid-terrain-smoke.cjs on forced
WebGPU and WebGL2. Both runs exercised real controls, paint/fill, undo/redo,
cancellation, entrance editing, material blending, camera/shadow LOD, device
recovery, storage save/load, fresh-page reload, corrupt-save preservation,
post-load editing, room/basin views and full curved descent/ascent. Basin-view
commands verify the full-detail bed and dry centreline collider. The saved scene
retains the shaped rim, enlarged room and basin, and LOD regenerates from saved
recipes. Fresh final browser checkpoint captures differed by zero pixels before
and after load on both backends; each saved browser fixture was 2,862,169 bytes. Native GUI fixtures were 2,862,193 bytes.

The focused final scripts/hybrid-entrance-shape-smoke.cjs passed on both browser
backends, probing retained corner ground/collision versus the open mouth, resize,
repeated storage load, same-dimension upgrade of a HYBRID5 checkpoint without
a cliff, and rendered underwater/basin views. No page errors were reported.

Native Dawn and direct D3D12 both passed the hidden Windows host --smoke run.
Each requested renderer was confirmed. Actual SDL GUI controls exercised hole
paint/fill, grouped entrance history and cancellation, blending, independent
camera/shadow LOD, actual file save/load, corrupt-file preservation, repeated
loads, post-load editing/traversal, room/basin views, underwater rendering and
real mouse/keyboard flight. system.json confirms three chambers, tenfold grotto
volume, basin collision and complete curved descent/ascent on both backends.
The user-facing executable remains in build/windows/cave-system to preserve the
normal running demo. The final rebuild changes only legacy seam acceptance;
that branch was separately checked against retained HYBRID5/HYBRID6 fixtures.

Integration defects were repaired during validation: signed cliff distance
now remains continuous around endpoint bends, allowing bounded refinement;
seam checks distinguish intentionally painted holes beside the cliff from solid
collar samples. These seam repairs do not change ownership of manual holes. The underwater
WebGPU cache now owns a typed 512-byte UnderwaterUniform matching the finite-region
payload, with a compile-time size check preventing the earlier 256-byte overflow.

The hole operation expects clean authored contact rims, before seam/sculpt
modifiers. It is conservative heightfield-cell rasterization, not exact curved
Boolean excavation or closed-volume intersection. The lower bowl contains no
water; the separate Grand Chamber pond remains. No physical-mobile or sustained
large-world performance acceptance is claimed.

Final tools/verify_hybrid_terrain.py checks passed on both native backends: scene
detail and distinct views, material blend changes, strictly reduced camera LOD,
independent shadow selection, automatic distance LOD, restored LOD counts and
zero changed checkpoint pixels. Grand Chamber, Lower Grotto and Grotto basin
PNGs are retained alongside original PPM captures and native-pixels.json.
Both engine-skill copies include the API, composition, compatibility and build/
validation guidance. Documentation links and focused tracked-file whitespace
checks passed.
