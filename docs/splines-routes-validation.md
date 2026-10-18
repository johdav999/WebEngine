# Splines/routes validation — 2026-10-18

Stage 5 was implemented in the existing shared checkout. Spline/profile/route
sources, static collision, editor history, prefab construction and built-in
persistence are delivered. Lane rules, junction solving/meshing and automatic
long-route streaming remain application/future work.

## Builds and portable acceptance

- Repository-local Emscripten **6.0.11**, Release `build/fx-contact-web`,
  `web_assets` published to `dist`; generated loader/Wasm and source adapters match.
- Native MinGW Debug `build/fx-contact-native`: **64/64 CTest suites passed**,
  12.58 seconds (`build/stage5-ctest.log`). This count includes the pre-existing
  AI controller work in the dirty checkout; Stage 5 adds `spline_route`.
- Explicit `spline_route_wasm_tests` passed in Node on the 32-bit Wasm ABI
  (`build/stage5-wasm.log`).

The focused suite checks exact straight endpoints/length, open clamps and closed
wrap, 64/128 arc-sampling convergence for its curved fixture, bounded nearest
queries, invalid/analytic stationary derivatives and source limits; vertical seed
and vertical-to-horizontal frame transport; nonplanar closed-frame seam, banking,
winding, profile perimeter topology, UV distance, exact closed positions/tangents
and bounds; render/collision
height/normal agreement; guarded mutation rejection; grouped editor creation,
exact resource undo/redo, duplication, shared resources on load/Play and real
vehicle contact on a curved/banked road. Malformed source counts and invalid banks
are tested with repaired valid checksums, along with truncation and load-allocation
budget failures. Legacy v26 records remain readable.

The current dirty checkout also needed a one-line const-correctness repair in the
existing AI actor lookup to compile. No AI behavior or user-authored content was
replaced as part of Stage 5.

## Browser acceptance

Chrome **155.0.8059.80** on desktop Windows. `scripts/spline-route-smoke.cjs`
passed forced **WebGPU and WebGL2** at 1450x1000 CSS pixels, **DPR 1 and DPR 2**.
The closed/recovery view also exercised 1250x850. Final runs had no page/console
errors or failed requests. Evidence:

- `build/evidence/spline-route/results.json`
- `build/evidence/spline-route-dpr2/results.json`
- `build/evidence/spline-route/build-metadata.json` records exact artifact hashes.
- Each folder contains backend-specific solid editor, collision preview, driving
  and closed-loop screenshots, inspected for geometry and readable authoring UI.

The authored road measured about **64.78 m**, with **262 vertices** after editing.
A banked collision probe reported height **3.9988 m** and upward normal Y **.9793**
near the authored 4 m centre. Both backends drove a real car on the edited source
collision with all four wheels reporting `route.asphalt`, at roughly **6.7 m/s**
after the controlled fixed-step fixture. Values are fixture observations, not a
formal error/performance guarantee for arbitrary curves.

The browser suite verifies position/tangent/bank/profile editing as one command,
world-grid snapping, ring-budget/invalid tangent rejection without changing source
bytes, point insertion and undo, neutral/profile physical-surface switching,
collision enable/disable agreement, all-or-nothing terrain point baking and undo,
and shared source/profile/mesh identity after duplication. Isolated Play supports
real driving and touch cancellation; Stop preserves the original scene SHA-256.
Valid scene files and IndexedDB reload retain authored sources and collision;
malformed files preserve the current document. Resize and forced GPU/context
recovery retain collision queries. Closed-loop preset authoring previews on each
backend.

Existing both-backend regressions passed:

- `scripts/vehicle-smoke.cjs`: `build/evidence/stage5-vehicle-regression/results.json`
- `scripts/physical-surface-smoke.cjs`:
  `build/evidence/stage5-surfaces-regression/results.json`
- `scripts/editor-structure-smoke.cjs`:
  `build/evidence/stage5-editor-structure-regression/results.json`

Native physics, spatial queries/joints, archives, terrain and editor suites also
passed in the complete run. Architecture, feature contracts and installed/CatGame
engine-skill copies were updated; the game dependency pin was not advanced.

## Closed seam repair

A focused final check reproduced a mismatch between the two UV-seam tangent frames
when using generic triangle-averaged tangents (`build/stage5-seam-before.log`).
The sweep now publishes analytic profile-edge tangents and an exact repeated
closing frame, preserving distance UVs and normal-map basis continuity. The
regression requires identical seam positions and matching tangent directions/signs.

## Scope

Arc/nearest results are bounded approximations. Synchronous route rebuilding has
explicit per-resource admission limits; no stress/sustained performance result is
claimed for maximum-sized paths or large editor histories. Independent baked mesh
references are not automatically linked back to route caches on reload. Required
collision children are managed and do not permit arbitrary independent authoring.
Terrain point sampling is a one-time bake, not full-section/live conformity.

The test harness keeps archive bytes in the browser rather than expanding large
scene files into JavaScript number arrays across Playwright. An early harness
iteration exhausted Node's heap through that unnecessary transport; final runs use
ordinary browser File/Uint8Array workflows without increasing engine budgets.
These desktop/emulated checks do not establish physical mobile acceptance,
production road content or physical controller performance.
