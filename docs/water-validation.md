# Water validation - 2026-10-14

Verified the working checkout, including its existing uncommitted rendering,
sky/weather and archive changes. This is desktop Windows/Chrome evidence.

## Build identity

- Repository-local Emscripten 6.0.11; Release target `webengine` in
  `build/web-water-release`.
- Staged host/worker/models/fonts in `build/water-browser-site`, served on the
  task-owned localhost port 8148. Published the same output through CMake
  `web_assets` into `dist`.
- `webengine.wasm` SHA-256:
  `DCC3862E531F504BC115D1EE80074069CD5F86F6FC3AE776E2F188CFB5F1F2C2`.
- `asset_decode.wasm` SHA-256:
  `1D0A7EE37354180A66945D2D3FFECF2540872EF0A7AB67B06623E9DB177069B7`.
- Desktop Chrome 155.0.8059.79, forced WebGPU and WebGL 2.

## Checks run

`ctest --test-dir build/native -R
'^(water|material|scene_archive|persistence|sky|render_frame)$'
--output-on-failure`: all six passed. Water coverage includes invalid values,
atomic overrides, normalized binding identity, culling at a displaced-frustum
edge, independent material batching, format-7 asset/scene round trips and a
format-6 no-resource scene. Existing archive tests also cover legacy format 1;
sky tests cover the format-5 sky layout. This does not claim a fixture for every
historical archive permutation.

`scripts/water-smoke.cjs`: both backends passed with no browser/GPU errors.
Real canvas checks cover:

- Geometric wave animation and independent moving ripple-normal animation;
  zero amplitude/ripple strength produces stationary pixels.
- Fresnel reflection controls, WebGPU environment reflection and WebGL's
  environment-independent analytic fallback.
- Explicit clock changes, invalid clock/preset rejection, pause/resume.
- Alpha and mirrored/double-sided water pipeline execution.
- Actual supported 1x/4x MSAA attachments, perspective plus orthographic inset
  rendering/animation, viewport resize, paused GPU/context loss and restoration.
- User orbit gesture, Return to demo and deterministic restart.
- Browser scene save/load and return: restored water settings produce matching
  paused pixels, with format-7 archives of approximately 1.5 MiB.

The sampled water crop changed 141,582 pixels on WebGPU and 158,356 on WebGL 2
between clock 0 and 1.25 seconds. These demonstrate animation, not image quality
equivalence or a performance improvement. Results/screenshots are in
`build/evidence/water/result.json` and the neighboring PNGs. The WebGPU demo
was also visually inspected from a real browser screenshot.

Affected regression scripts passed:

- `scripts/material-smoke.cjs`: PBR/unlit/alpha variants, IBL intensity/rotation,
  normal maps, skinned/mirrored tangent bases, atomic edits and cache stability.
  Updated its obsolete readiness-text assertions and disabled the playground's
  automatic sky lighting within the fixture so authored IBL controls stay fixed.
- `scripts/compositing-smoke.cjs` with `MSAA_SAMPLES=4`: ordered views, layers,
  masks, clear/load behavior, stable uploads and one GUI pass after composition.
- `scripts/render-fallback-smoke.cjs`: existing textured scenes, fallback
  acquisition/failure paths, real context restoration, skeletal/morph animation,
  particles, ordered views and GUI.

Regression evidence is under `build/evidence/water-regression-materials`,
`water-regression-compositing` and `water-regression-fallback`.

## Scope

No physical Android/iOS, sustained device-performance or non-Chromium acceptance
was run. Refraction, foam, nearby object reflections, underwater rendering and
buoyancy remain outside the delivered surface shader. Native builds verify
portable contracts rather than a native graphics backend. Installed engine-skill
instructions and the contract map, feature integration and build-validation
references were updated alongside this implementation.


## Depth-aware extension - 2026-10-14

The current extended Release build was rebuilt with repository-local Emscripten
6.0.11 in `build/web-water-release` and tested from the matching staged site on
localhost 8148, desktop Chrome 155.0.8059.79. The earlier reflective-only record
above describes the previous build; current evidence paths now contain this
extended build's results.

- Current `webengine.wasm` SHA-256:
  `6581CF148623B4AE637DC803F732BC0C3D1B85DE0C3EF2B2ED7B11E45242CAC3`.
- Matching format-8 worker `asset_decode.wasm` SHA-256:
  `C0156BFACA1CD70550CCD6614832DC5739EC6E1B5DC0CC749A7BC0ED40132171`.
- Seven native suites passed: water, material, scene_archive, persistence, sky,
  render_frame and render_quality. Water now checks effect ranges, optional
  capture selection by layer/mask/enabled state, object input flags, all new
  archive fields and an actual format-7 water material layout with new effects off.
- `scripts/water-depth-smoke.cjs` passed on forced WebGPU and WebGL 2 with actual
  1x/4x attachments. It covers refracted lakebed geometry, normal distortion,
  depth absorption, isolated shoreline foam, foreground-plane rejection,
  perspective/orthographic ordered views, resize, paused recovery and optional
  scratch memory/pass costs. Strong distortion produced zero red foreground
  leaks on both backends. The isolated foam changes 15,558/15,511 sampled pixels
  on WebGPU/WebGL respectively. A single active demo view uses six passes with
  effects enabled versus three with both disabled; these counts are conditional
  on the demo's one view/no GUI and are not a performance benchmark.
- Updated `scripts/water-smoke.cjs` passed both backends: animation, independent
  waves/ripples, reflection controls, pause/resume, invalid clock/presets,
  alpha/mirrored pipelines, MSAA, ordered perspective/orthographic views,
  resize/recovery, return/restart/orbit and real format-8 save/load/return.
- Existing material, 4x compositing, fallback/recovery and 4x shadow suites passed
  on this exact build. Stable non-water scenes retain their original pass counts
  and unchanged-frame upload behavior; shadow timestamps remain valid.
- New screenshots were visually inspected on both backends. Current results are
  `build/evidence/water-depth/result.json` and `build/evidence/water/result.json`;
  regressions are under `water-depth-materials`, `water-depth-compositing`,
  `water-depth-fallback` and `water-depth-shadows` in `build/evidence`.

Refraction uses an opaque screen-space snapshot, not full volume ray tracing.
Transparent, hidden/offscreen and other-water geometry are excluded. Foam is
reconstructed depth-gap shading, without a persistent simulation. Physical
Android/iOS, sustained mobile performance and non-Chromium browsers remain
unverified. Architecture, water, archive, fallback and quality contracts plus the
installed engine skill and affected references were updated with the extension.

## Planar reflection extension — 2026-10-15

Added optional nearby-object planar reflections on both backends. The final
Release browser build used the repository-local Emscripten 6.0.11 SDK and was
served from `build/water-browser-site` on port 8148. The shared checkout also
contained concurrent terrain work; its archive version 10 was preserved. Water's
planar-strength field begins at version 9, and old water loads with capture off.

- Final `webengine.wasm` SHA-256:
  `520BB21173A69549438E3539E48F906FEBB2BC313E1F47AFA6C186ADF3B7DFC0`.
- Matching `asset_decode.wasm` SHA-256:
  `21F1A428F7D21FF3697358DDF0EE113839311CB65487E25D11D387B8B7CF76AF`.
- `scripts/water-planar-smoke.cjs` passed on the final build in desktop Chrome
  155.0.8059.79, forced WebGPU and WebGL 2, with zero browser/GPU errors. It checks
  actual reflected object pixels, submerged-object clipping, single/4x main MSAA,
  ordered perspective/oblique orthographic views, resize, identical paused pixels
  after real device/context recovery, current scene archive restoration and
  optional target/pass costs. Both backends used actual 4x MSAA when requested;
  reflection targets themselves remain single-sampled.
- Animated default versus planar-off changed 8,293 WebGPU / 6,835 WebGL pixels in
  the sampled water region. The isolated red fixture produced 1,702 / 1,625 red
  reflection pixels above the plane and zero below it. Planar-only capture added
  exactly one pass, from three to four in this one-view/no-GUI fixture, and both
  color/depth storage increased. These are acceptance checks, not performance
  benchmarks or general device guarantees.
- Seven focused native suites passed during this extension: water, material,
  scene_archive, persistence, sky, render_frame and render_quality. Water includes
  transformed/mirrored plane math, camera involution, invisible/layer/mask
  selection, atomic settings, current archive round trips, and actual format-8
  material layout retaining refraction/foam while planar capture defaults off.
  The final World-only object eligibility flag received an additional water test
  rebuild/rerun after the broader native run.
- Water animation/archive, depth/refraction/foam, material, 4x compositing,
  WebGL fallback/recovery and 4x shadow browser suites passed during integration.
  Depth tests still found zero red foreground leaks and zero deep-water foam on
  both backends. These regressions ran before the final World-layer eligibility
  and opaque capture-alpha corrections; the planar suite was rerun afterward.
  An initial parallel run hit local HTTP connection refusals;
  the affected water/material/fallback suites passed when rerun serially.
- Final default and still-mirror captures were visually inspected on both
  backends. The in-app browser also showed the updated WebGPU demo with nearby
  landmark reflections, refraction and foam. The demo tab remains available.
- `web_assets` published the host and matching worker to `dist`. Architecture,
  water/archive/fallback/quality contracts and the installed engine skill plus
  three supporting references were updated.

Results: `build/evidence/water-planar/result.json`; screenshots include
`webgpu-animated-default.png`, `webgl2-animated-default.png`, `webgpu-still-planar.png`
and the below-plane controls. Other extension runs are under
`build/evidence/water-planar-water`, `water-planar-depth`, `water-planar-materials`,
`water-planar-compositing`, `water-planar-fallback` and `water-planar-shadows`.
Earlier entries above describe their own historical builds and evidence.

Limits: one selected plane per view, opaque/masked World objects only, no
roughness mip filtering, recursion, reflected particles/transparent objects or
independent reflection LOD selection. Main-camera-selected geometry/poses are
reused without a simulation tick. Physical Android/iOS, sustained mobile
performance and non-Chromium browsers remain unverified.

## Underwater rendering extension — 2026-10-15

Added optional underwater depth fog and RGB absorption on WebGPU and WebGL 2.
The final Release browser build used repository-local Emscripten 6.0.11, was served
from `build/water-browser-site` on port 8148, and published matching host/worker
artifacts to `dist`. Concurrent terrain/material-layer/editor changes were
preserved; the current writer is format 12, with underwater fields introduced in
format 11 and older water loading with underwater effects off.

- Final `webengine.wasm` SHA-256:
  `7FA702380D365F52491CF3E0B26267D4BCC205D628913946AC638C35B0D37E40`.
- Matching `asset_decode.wasm` SHA-256:
  `11C9392B2E3E02E81CE62A2880759739298FD96D87F6A840DC7C5598C5061BD3`.
- Eight focused native suites passed from task-specific `build/native-underwater`:
  water, material, scene_archive, persistence, sky, render_frame, render_quality
  and terrain. Water covers finite ranges, failed-edit atomicity, enabled/layer/
  mask/footprint selection, waterline epsilon, transformed world distances,
  upward/side ray exits, orthographic entry, distance caps, current round trips,
  and actual older material field layouts with underwater disabled. An initial
  shared build caught concurrent format/fixture changes; the isolated build uses
  the existing pinned Jolt source cache and passed after integration.
- `scripts/water-underwater-smoke.cjs` passed on the final build in desktop Chrome
  155.0.8059.79, forced WebGPU and WebGL 2, with zero browser/GPU errors. It checks
  the visible submerged selector, distance-varying fog, stronger red than blue
  absorption on near/far white markers, capped optical paths, surfacing and leaving
  the footprint, actual 1x/4x main attachments, perspective/orthographic views,
  an above-water inset over a submerged main view, untouched magenta Overlay
  geometry, resize, identical paused pixels after device/context recovery, current
  scene archive restoration and demo return/restart. WebGPU's final cache change
  also verifies zero unchanged underwater view-uniform writes while paused.
- Fog-only white-marker red averages were about 198 near / 161 far on WebGPU and
  197 / 159 on WebGL 2. With absorption only, the far marker measured RGB
  (18,108,159) / (25,107,157), confirming stronger red extinction on both backends.
  The Overlay fixture retained 11,664 magenta pixels on each backend. The dry
  inset, surfaced camera and camera outside the footprint retained control pixels.
- In the single-view/no-GUI demo, underwater adds two passes, from seven to nine
  with existing surface effects enabled. Pure underwater at 1x uses six passes
  versus three with all effects off: snapshot, fog composite and final
  single-sample presentation. Scratch storage releases when no effect requests it.
  These are fixture acceptance counts, not sustained performance measurements.
- Existing water animation/archive, refraction/foam, planar reflection, material,
  4x compositing, WebGL fallback/recovery and 4x shadow browser suites passed during
  integration. The planar red-fixture crop was adjusted for the demo's explicit
  above-water camera framing; the underlying pixel thresholds were preserved.
  Those seven regressions precede the final underwater uniform-cache change and
  unrelated editor integration rebuild; underwater acceptance was rerun afterward.
- Final submerged captures were visually inspected on both backends. The in-app
  browser was refreshed, switched to Underwater, paused and left ready to inspect.
  Architecture, water/archive/fallback/quality docs and the installed engine skill
  plus three supporting references were updated; documentation links and artifact
  identities were checked.

Final underwater results and screenshots are in `build/evidence/water-underwater`.
Other integration evidence is under `build/evidence/underwater-water`,
`underwater-depth`, `underwater-planar`, `underwater-materials`,
`underwater-compositing`, `underwater-fallback` and `underwater-shadows`.
Previous entries above describe their own historical builds.

Limits: one containing rectangular local-XZ footprint per camera, below the mean
plane without a bottom; arbitrary polygons/holes and authored water volumes are
not inferred. Transparent fog reuses opaque/surface depth. Physical Android/iOS,
sustained mobile performance, non-Chromium browsers, caustics, volumetric lighting
and physical underwater optics remain outside this verification.

## 2026-10-15 animated underwater caustics

Implemented on WebGPU and WebGL 2 and checked in desktop Chrome 155.0.8059.79.
Built Release with repository-local Emscripten 6.0.11, published through `web_assets`
and served the selected `build/water-browser-site` on 8148. Host and decode worker
Wasm/JS artifacts match the build and published dist. Selected build SHA-256:

- `webengine.wasm`: `C2A7779A78436B6E1781F3A76DCFDB6BCF0C4B7C9F592106E275F8DEB4C4942B`
- `asset_decode.wasm`: `F632D125B584050F03CE7126171B259ABA97AED175EB384925E3E83AEB1EF445`

The published build includes concurrent terrain/editor work and archive writer
v14. Water's caustics fields are gated at v13; native legacy-v12 fixtures retain
layered-material bytes and load with caustics off. Earlier format 7/8/10/11 water
fixtures still pass. Host and worker were rebuilt together after the format change.

- Eight focused native suites passed in isolated `build/native-underwater`:
  water, material, scene_archive, persistence, sky, render_frame, render_quality
  and terrain. Water adds settings validation, caustics-only selection, distinct
  batch identity, atomic invalid override rejection and non-default archive round trips.
- `scripts/water-caustics-smoke.cjs` passed on both backends on the published build,
  with 82,124 changed receiver pixels on WebGPU and 89,974 on WebGL 2 against off.
  It checks explicit animation, zero caustic speed, pause/resume, depth fade,
  unchanged ceiling/sky, above-water and outside-footprint rejection, perspective
  and orthographic cameras, dry ordered inset, actual 4x MSAA, resize, identical
  paused recovery, caustics-only rendering, archive load and demo return/restart.
- Caustics on/off with fog retained both use nine demo passes and identical texture
  storage (13,074,852 bytes WebGPU; 9,730,120 WebGL 2). Paused WebGPU has no unchanged
  underwater uniform writes. These are fixture costs, not sustained performance evidence.
- Refraction/foam and planar-reflection browser suites passed on both backends on
  the published build. Foreground leaks and below-plane reflected red pixels remain
  zero. The original water animation and underwater fog/absorption suites passed
  during integration before the concurrent v14 publication; water rendering code
  is unchanged by that archive/terrain/editor rebuild.
- Material browser regression passed, including warm-cache writes, scalar edits,
  normal mapping, texture/IBL import, alpha variants, mirrored/skinned bases and
  invalid-import preservation. Its texture-map selector was scoped to the material
  panel after the concurrent editor added a second identically named section.
- Final captures were visually inspected on both backends. The in-app browser was
  switched to Underwater and paused. Architecture, water, quality/fallback/archive
  docs and the installed engine skill with its three supporting references were
  updated. Documentation links and `git diff --check` passed.

Final caustics evidence: `build/evidence/water-caustics-final`. Regression evidence:
`build/evidence/caustics-depth`, `caustics-planar`, `caustics-materials`,
`caustics-water` and `caustics-underwater`. Historical entries above retain their
original scope. Caustics are an artistic screen-space receiver effect for submerged
views; refracted sunlight/shadow transport, above-water visibility, separate
transparent receivers and physical mobile acceptance remain outside this delivery.

## 2026-10-15 interaction ripples and moving-object wakes

Both backends implement bounded, caller-supplied World-water interaction snapshots.
Applications own contact/motion emission; shaders change normals and bright crests
without displacing vertices or changing physics/picking. The playground includes
moving yellow wake and bouncing orange contact sources, an Object interactions
selector and Calm interaction pool preset. Scene replacement clears transient
inputs; GPU recovery retains them. Archive format remains 14.

Built Release with repository-local Emscripten 6.0.11 in `build/web-water-release`,
published with `web_assets` and served `build/water-browser-site` on port 8148.
Final host/worker JS and Wasm hashes match across the selected build, staged site
and dist. Final SHA-256 identities:

- `webengine.wasm`: `46572C593F989AD4D145EF931A5651DACE8FA9C8616107F7E6F88A0E52596732`
- `asset_decode.wasm`: `F632D125B584050F03CE7126171B259ABA97AED175EB384925E3E83AEB1EF445`

Desktop Chrome 155.0.8059.79 acceptance:

- `scripts/water-interactions-smoke.cjs` passed on WebGPU and WebGL 2 against the
  final published build. At age one, isolated ring/control differences measured
  47,879 / 47,187 pixels; the changed bounds grew from 268x268 / 267x266 to
  411x442 / 409x440 at age two. Independent directed wakes changed 27,963 / 26,471
  pixels behind the positive-X heading. These are image acceptance metrics.
- Future, expired, masked and other-elevation sources retained control pixels.
  Pause/resume, actual supported 4x MSAA, perspective/orthographic ordered views,
  resize, identical paused recovery, scene-replacement clearing, disabled loaded-
  scene demo controls, source motion, invalid modes and demo return/restart passed.
- Isolated flat water retains three passes with interactions on or off, unchanged
  texture storage, and zero unchanged WebGPU frame writes while paused. The private
  frame payload is now 2400 bytes. These counts do not establish mobile performance.
- Eight native suites passed: water, material, scene_archive, persistence, sky,
  render_frame, render_quality and terrain. New water cases cover copied input
  staging, finite ranges, normalized headings, age boundaries, masks, bounded
  counts and whole-snapshot invalid-edit rollback.
- Existing water animation/archive and caustics suites passed on both backends;
  material, 4x ordered-view compositing and WebGL fallback/recovery regressions
  passed during integration. Existing water tests explicitly disable interaction
  sources, repeating that state after restart for comparable old baselines.
  The interaction suite was rerun after final demo path clearance and minimum-
  lifetime boundary validation. The 4x shadow regression passed on the preceding
  renderer build; shadow
  coverage includes mirrored/skinned bases, masks and warm uniform caching.
- Both backend final moving-source captures were visually inspected. The in-app
  browser was switched to Calm interaction pool with both source types active.
  README, water/fallback/quality contracts and architecture were updated. Installed
  and CatGame-local engine skills have matching water sections and affected
  reference updates, retaining CatGame pin/dependency and terrain guidance.
  Both copies' local links and documentation content checks passed. This does not
  advance CatGame's engine gitlink or establish game runtime integration.

Final interaction results/screenshots: `build/evidence/water-interactions-final`.
Other evidence: `interactions-water`, `interactions-caustics`,
`interactions-materials`, `interactions-compositing`, `interactions-fallback` and
`interactions-shadows` under `build/evidence`. Earlier entries retain their own
historical build and acceptance scope. Fluid simulation, automatic physics contact
subscriptions, geometric wake displacement, physical Android/iOS acceptance and
sustained mobile performance remain outside this delivery.
