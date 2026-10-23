# Curved cave routes and offset chambers — 2026-10-23

The source loft follows a smooth lateral centreline: entrance at x=0 m, grand
chamber at x=−6 m, and lower grotto at x=+6 m. Floor elevations remain 0/−3/−8 m.
Default nominal room cross-sections are 6×4.3 m, 10.8×6.5 m, and 6.6×4.8 m.
Passages narrow to 3×2.5 m, with reduced height at the minimum entrance ceiling.
Cross-sections remain shared rings in one indexed source surface. The monotonic
longitudinal coordinate avoids folding or overlapping adjacent sweep sections.

The same smooth route positions chamber cameras/lights and steers the motor using
the path tangent plus bounded lateral correction. HYBRID7 application metadata
identifies the curved system; HYBRID5/6 saves retain their saved geometry and
straight route behavior. Generic scene archive v36 and engine APIs are unchanged.
Existing stalactite/stalagmite work in the shared checkout was preserved.

## Focused evidence

`hybrid_cave_route` passes for default and all eight width/depth/height extrema. It
checks the offset room floor/ceiling collisions, full motor descent through both
bends, curved layout restoration and complete ascent after loading. Sampled
motor x positions confirm travel through both lateral offsets. The mesh remains
within the existing 30,000-triangle bound.

Collision rays measure the actual cross-sections: every chamber is over 20%
wider and at least 0.1 m taller than the sampled narrow passage, including the
minimum ceiling configuration. The final focused run passed 5/5 suites:
`hybrid_cave_route`, `hybrid_terrain`, `terrain_lod_archive`, `terrain_seam`, and
`terrain_hole_lod`. Existing formation assertions remain enabled and passed.

A real earlier straight-system checkpoint was loaded and walked with its original
straight steering. Browser and isolated native Release builds passed. Default
WebGL2 previews reported 12,309 triangles including the current rock formations;
the grand chamber and grotto captures were visually inspected.

Evidence is under `build/evidence/hybrid-terrain-curves/`, including
`route-tests.log`, build logs, previews and full platform smoke results.

WebGPU and WebGL2 passed full editing, hole/seam/collision, LOD, graphics recovery,
corrupt-load preservation and checkpoint restoration checks. Both verified the
offset camera x positions, curved descent to x>5.8 m, a deep explorer checkpoint,
and full ascent. Both had zero unexpected errors and zero changed load pixels.

Windows Dawn and direct D3D12 passed the complete native smoke, including chamber
captures, a restored grand-chamber view and full curved descent/ascent. Pixel and
submission verification passed for both renderers. The native executable remains
in `build/windows/cave-system/`, preserving any running normal-build session.

## Limits

The route curves through three sequential chambers rather than branching. Old
files retain their prior geometry until an entrance change rebuilds it. The
static plateau extension remains outside heightfield brush/query ownership.
Physical mobile and sustained large-world performance are not established.
