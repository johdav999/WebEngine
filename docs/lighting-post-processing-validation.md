# Lighting and post-processing validation

## Showcase refresh: 2026-10-21

The rendering implementation was already present. This change adds a usable
`?scene=lighting` showcase and extends the existing headlight editor example:
two shadowed headlights, a torch and cascaded sunlight, visible pillar occlusion,
an HDR emissive source, and shadow/post comparison toggles. Built-in routes skip
the unrelated imported model download. Stale no-spot-shadow editor/README/skill
text was corrected; no new portable API or archive format was introduced.

- Pinned Emscripten **6.0.11** Release `build/web-release`: built `webengine`,
  staged `web_assets`, and built/ran `spotlight_wasm_tests`: **111 checks passed**.
- Native Debug: **5/5 passed**: `render_quality`, `scene_archive`, `shadow`,
  `spotlight`, `render_frame`. Spotlight's stale v31 assertion now expects current
  scene archive v34; runtime serialization was unchanged.
- Chrome **155.0.8059.85**, Windows desktop, actual WebGPU and forced WebGL2:
  `lighting-showcase-smoke.cjs` passed at **DPR1 and DPR2**. Each preset has visible
  shadow on/off differences; restoring shadows and HDR restores paused pixels.
  Post comparison, recovery, preset controls and compact 390px layout passed.
  DPR1 changed shadow pixels: headlights 54,338; torch 31,007; sun 659 on WebGPU.
- Fresh `lighting-post-smoke.cjs` passed both backends: spotlight/sun shadow pixels,
  bloom, exposure, grayscale grading, invalid retention, post disable restoration,
  MSAA, resize and device/context recovery.
- Fresh `lighting-composition-smoke.cjs` passed both backends for HDR water,
  refraction, MSAA and underwater fog; forced missing WebGL float support retained
  the requested policy and rendered the standard path without errors.
- Existing `spotlight-smoke.cjs` passed both backends for cone/range/mask/budgets,
  authoring/history, Play, scene round trip, resize and recovery. Its stale v31
  expectation now uses v34; recovery waits for a new generation and masks the DOM
  status footer, whose retained overflow warning legitimately changes to ready.
  An initial run failed on that status-text difference while scene pixels matched.
  The direct `spotlight-example-smoke.cjs` editor route also passed both backends.

Evidence: `build/evidence/lighting-showcase{,-dpr2}/results.json`,
`build/evidence/lighting-post-refresh/results.json`, and
`build/evidence/lighting-composition-refresh/results.json`. Build/test logs have
`lighting-showcase-` or `lighting-spotlight-` prefixes under `build/`.
Main Wasm SHA256: `4A6553C89B1AA4356E98A628D16FF07495E9FBB63021CC1ED5A72DAC9571321F`;
worker: `C29F77DAC3CE3A21ECDEA8ECCB0A516CA84F7A6013465BDB3CF403418F554543`.
Source/staged `app.js` and `editor-spotlight.js` match. Both skill copies and their
lighting/spotlight references were updated and checked, preserving the Paws pin
and native Windows proof guidance. Earlier evidence below remains historical.
Editor regression reports are in `build/evidence/lighting-spotlight-refresh/` and
`build/evidence/lighting-headlight-editor-refresh/`.

Verified 2026-10-19 in the shared WebEngine checkout. Existing unrelated changes
were preserved. No commit, consumer gitlink change or remote deployment was made.

## Build and native checks

- Repository-local Emscripten 6.0.11, Release `build/web-release`, explicit
  `web_assets` target published generated binaries and current host to `dist`.
- Native Debug `build/native`: **5/5 passed**: `render_quality`, `scene_archive`,
  `shadow`, `spotlight`, `render_frame`. Tests cover finite/invalid settings,
  perspective spotlight projection, cascade frustum coverage/splits/budgets,
  existing frame participation, authored shadow flag/history and archive round trips.
- Final browser Wasm SHA256:
  `9DC0A9D82ED86924ABA61F9ACCA5CCDDB2C1E7177B7B409480D9A3BE59ECAB48`.
- Build/test logs: `build/lighting-native-build.log`, `lighting-native-test.log`,
  `lighting-web-build.log`, `lighting-build-hash.txt`.

## Chrome acceptance

Chrome **155.0.8059.80**, Windows desktop, localhost:8137, forced WebGPU and WebGL2.
These are actual browser renders, not native renderer or physical mobile tests.

| Suite | Verified result |
| --- | --- |
| `lighting-post-smoke.cjs` | Visible spotlight shadow on/off; HDR bloom on/off; exposure; grayscale scene with ungraded interface; invalid candidate retains pixels; post disable restores original pixels; visible cascaded sunlight; 4x MSAA; resize; device/context loss recovery |
| `lighting-composition-smoke.cjs` | HDR water/refraction, MSAA and underwater fog on both backends; forced missing EXT_color_buffer_float retains requested policy and uses standard WebGL rendering |
| `shadow-smoke.cjs` | Existing WebGPU fixed shadow path: cutouts, poses, cast/receive, budgets, off-camera casters, masked ordered views, warm uploads and timing |
| `spotlight-smoke.cjs` | Both backends: cone/range/parent/mask/light-budget pixels, MSAA, editor outlines/properties, undo/redo, invalid cones, shadow authoring, duplication, Play, scene round trip and recovery |
| `water-depth-smoke.cjs` | Both backends: refraction/distortion/absorption, isolated shoreline foam, foreground rejection, 1x/4x color/depth capture, perspective/orthographic views, resize, paused recovery and optional targets |

Final lighting and composition evidence:
`build/evidence/lighting-post-final/results.json` and
`build/evidence/lighting-composition-final/results.json`, with PNG captures.
Regression evidence: `build/evidence/lighting-shadow-regression/`,
`lighting-spotlight-regression/` and `lighting-water-regression-final/result.json`.
All successful reports contain no unexpected console/page errors. The spotlight
budget test records its expected eight-light overflow diagnostic separately.

The first water regression run reported ERR_NO_BUFFER_SPACE while loading host
resources and consequent missing-module errors. Its final isolated rerun passed
every image assertion and reported no errors. Earlier test setup also exposed an
old scene-validation guard and an unstaged `webengine`-only build; the guard was
updated and the final tests use `web_assets` staging.

Both installed and CatGame engine-skill copies were updated and verified. Their
lighting references match, and the CatGame-specific dependency/pin guidance remains.

## Boundaries

No physical Android/iOS, Safari/Firefox, sustained thermal/performance, HDR display,
point-shadow cubemap, LUT grading or automatic exposure acceptance is claimed.
Bloom covers the complete composite and can cross adjacent view boundaries.
WebGL retains its basic-lighting/no-IBL limitations. Quality and post settings are
transient renderer policy. See [usage and limits](lighting-post-processing.md).
