# Cave camera controls — 2026-10-23

The shared demo camera supports right-mouse drag to look, WASD flight relative to
the view, Q/E world vertical movement, and Shift boost (6/18 m/s). Pitch is bounded
to avoid flipping. Browser users click the scene for keyboard focus; pointer
capture keeps a drag owned by the scene. Form fields retain typing ownership.
Escape, blur, resize and input cancellation release controls. Left-drag remains
hole painting. Flight has no collision and does not move the walking explorer.

`examples/hybrid_terrain/flight.hpp` composes existing InputBindings, InputSystem
and CameraComponent on the Application input clock. Platform adapters do not own
camera logic. The controller holds no scene pointer or subscription, and camera
pose persistence uses the existing scene archive. Held controls reset on load.
There is no new engine API or archive version.

## Browser evidence

The rebuilt browser demo passed `scripts/hybrid-flight-smoke.cjs` with forced
WebGPU and WebGL2. Real browser mouse/key events verified rotation, each WASD
direction, Q/E, speed boost, Escape, typing exclusion, focus transfer while moving,
and saved position/rotation restoration. Both runs had zero page errors. The
WebGL2 result and control help were visually inspected.

The existing full `scripts/hybrid-terrain-smoke.cjs` regression also passed on both
backends: painting/undo/redo/cancellation, entrance editing, seam blending,
collision traversal, three LOD levels, independent shadows, automatic LOD,
graphics recovery, resize and save/load. Both restored scene images had zero
changed pixels and both runs reported zero unexpected errors.

Evidence: `build/evidence/hybrid-terrain-flight/browser/report.json`, screenshots,
and `regression/report.json`.

## Windows evidence

Release builds and the complete native demo `--smoke` passed on Windows Dawn
and direct D3D12. Each run used SDL mouse/key events to verify right-drag rotation,
W forward movement, E ascent, and cancellation while W was held. Results are in
`dawn/flight.json` and `d3d12/flight.json`, with `flight.ppm` captures. The same
runs passed existing retained GUI editing, seam/collision/LOD checks, repeated
checkpoint loading, corrupt-save preservation and post-load traversal.

`tools/verify_hybrid_terrain.py` passed for both native renderers, including
before/after checkpoint scene pixel comparisons. Build logs, native stdout,
`native-pixels.json` and `pixel-verification.log` are under the same evidence root.

## Limits

These controls target desktop mouse and keyboard. No touch flight gesture,
pointer lock, collision-constrained flight, physical mobile acceptance or new
large-world performance claim is included.
