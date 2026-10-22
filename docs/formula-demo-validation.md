# Formula circuit validation — 2026-10-19

Easy handling follow-up: `FormulaHandlingSettings` now supplies the Formula-only
steering response, tyre/drivetrain tuning and speed taper. The repository-local
Emscripten 6.0.11 Release build passed and published updated matching host/Wasm.
Current Wasm SHA-256:
`83131920F86DC9594CC4E3F579668194640DC9AE1F70E3243721EBEF5F4895D9`.

Native `formula_demo` passed in 60.06 s: first-tick smoothing, 60/120 Hz response
equivalence, speed scaling, immediate neutral, wheel contacts, braking/reset and
the complete physical circuit. The lap was 73.70 simulated seconds, with maximum
centerline distance 1.32 m including chassis height. The separate native
`formula_handling_tests.exe` passed the production easy preset on a wide flat
collider: after 20 seconds of throttle it reached 31.70 m/s (114.13 km/h), then
held full steering for three seconds with minimum chassis up-Y 0.999992 and peak
tyre lateral slip 0.09105 rad (5.22°). Cancel, disable and re-enable retained
neutral commands. These are scoped automated checks, not human handling approval.

Both browser suites passed on forced WebGPU and WebGL 2:
`build/evidence/formula-easy/results.json` checks the actual first-step/held/
neutral steering response, keyboard, braking/reset, collision seam and touch;
`build/evidence/formula-easy-mouse/results.json` checks inverted mouse input,
physical rightward travel, concurrent throttle/brake, overrides and lifecycle
cancellation. Input assertions now distinguish raw `formulaInputState` from the
effective physics command in `vehicleState`. The earlier evidence below is
retained for the previous asset/input/handling versions.

Red car replacement: the demo now fetches `assets/formula1-red-driving.glb`,
prepared from `formula1_red.glb`. All 18,977 triangles are retained; the three
4096 px textures are resized to 2048 px for normal importer memory limits.
The deployed GLB is 2,689,600 bytes. `scripts/formula-smoke.cjs` passed on both
forced Chrome renderers with the new asset, including wheel contacts, physical
turning, acceleration/braking, reset, seam collision, keyboard and touch input.
The screenshots visibly show the red model. Evidence:
`build/evidence/formula-red/results.json` and the backend grid/narrow PNGs.
This was an asset/host publication using the existing compiled Wasm; no native
handling changes, new native run or full Wasm rebuild is claimed.

Original red source SHA-256:
`753C87554560920B31F14D2E2340BBA80AD7FB644672BF52B90B0EF0DC3B22E9`.
Derived red asset SHA-256:
`FE1AFF22F204A7B524C276524E584D539F1B9219478F57DFAEF37D0302F4307E`.

Mouse steering follow-up: `scripts/formula-mouse-smoke.cjs` passed against the
served JS/CSS on both forced WebGPU and WebGL 2. It checks actual rightward
chassis travel from leftward mouse input with the requested inverted mapping
while W accelerates, concurrent service braking,
left/right signs and centre dead zone, A/D override, toggle, pointer exit onto HUD,
blur, pause/resume, reset, context changes and resize cancellation. Touch movement
cannot write the mouse source. The added toggle remains within a 390×844 viewport.
Evidence: `build/evidence/formula-mouse/results.json` and backend screenshots.
This change only updates the browser application adapter and styles; no native
code changed or new native build/test run is claimed for the mouse follow-up.
The existing Formula keyboard/brake/reset/touch regression smoke also passed on
both renderer backends after adding mouse steering.

The initial silver-car Release browser build used repository-local Emscripten 6.0.11 and
published the complete selected host/worker/assets into `dist/`. Local acceptance
served that directory on port 8116. No paid providers were used.

`scripts/formula-smoke.cjs` passed on forced Chrome WebGPU and WebGL 2:

- Optimized supplied GLB loads, all four wheels contact asphalt, and the 884.23 m
  closed circuit exposes its minimap and valid initial lap state.
- Real W, D, Space and arrow input reaches the existing vehicle action path.
  A simulated positive steering command physically travels toward +X.
- Controlled acceleration reaches 22.24–22.64 m/s; service braking reduces it to
  2.69–3.21 m/s. Reset returns the car to the opening straight with four contacts.
- Pause holds car position and lap clock. Blur cancels held throttle.
- Ground probes on both sides of the closed road seam hit the same 0.04 m
  asphalt surface with upward normals.
- At 390×844, held driving buttons remain in bounds, at least 44 px tall, and
  submit/release throttle. This is desktop narrow-layout testing.

Evidence and screenshots: `build/evidence/formula/results.json`,
`webgpu-grid.png`, `webgl2-grid.png`, `webgpu-narrow.png`, `webgl2-narrow.png`.

The optimized model SHA-256 is
`AA1FEE8F3396927A5A15B106A4B39B4ECF8C1C8E7B28E32922AC0804A553F362`.
Its original source is retained unchanged. The final browser Wasm SHA-256 is
`A51DEFED357660B410E82B45A4F717499D6DFDBAC9E6ABE2ECABEC338DD7B3AA`.

Native `formula_demo_tests` passed (76.60 s in the Debug build): synthetic race
rules cover consecutive laps, best time, seam farming, off-track invalidation,
teleport rejection and paused clocks. The full scene checks closed position/tangent
continuity, four wheel contacts, acceleration, braking and reset. A physical car
then drives all 884 m using test-only look-ahead steering and successfully
completes lap one and begins lap two. This test controller is not a gameplay feature.
The driven lap takes 73.37 simulated seconds, with maximum centerline distance
1.52 m (including chassis height).
The existing native `vehicle` suite also passed, including the strengthened
positive-right steering assertion: two selected CTest suites, zero failures.

The broader existing `vehicle-smoke.cjs` exercised handling and editor Play on
both backends, but its final zero-error assertion failed with editor JavaScript
`Cannot read properties of null (reading 'after')`. Its stale archive-version
expectation was updated from 29 to the current source format 31. See
`build/evidence/formula-vehicle-regression/results.json`; this is not reported as
a passing editor regression. The dedicated Formula demo produced no JS errors.

Physical mobile, actual controller hardware, sustained-performance benchmarks,
production Formula 1 handling, rigged visible-wheel animation and race-state
archive persistence are outside these checks.
