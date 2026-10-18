# FX first milestone validation

Validated on 2026-10-17 using the repository-local Emscripten 6.0.11 Release build
and Chrome 155.0.8059.80 on Windows. Both WebGPU and WebGL 2 were forced explicitly.
The task preserved the pre-existing dirty checkout and unrelated changes.

## Delivered scope

Box/sphere/disc/cone/static triangle-surface emission; shape speed; drag/wind;
seeded sinusoidal gusts; vector velocity/force age curves; per-camera velocity
alignment/stretch; optional opaque-depth soft particles; v17 persistence and the
Fountain and mist example preset. See [contracts](fx.md).

## Verification

- Native CTest `fx`, `scene_archive`, `persistence`, `water`, `terrain`: five passed.
  FX checks include bounded shape distributions, area-weighted triangles, failed
  configuration preserving live state, drag/wind and curve math, near-zero drag,
  deterministic gust restart, archive continuation, soft preflight masks/disable,
  alignment staging and a real v16 archive loading with legacy defaults.
- `scripts/fx-milestone-smoke.cjs`: both backends passed with no console/GPU errors.
  Orthographic and perspective receiver fades, alpha/additive fading, far and clear
  depth, velocity-stretched coverage, 1x/4x MSAA, ordered masked views, resize,
  device/context recovery, v17 save/load and public fountain play/pause/clear/restart.
  Evidence: `build/evidence/fx-milestone/results.json` and adjacent screenshots.
- Existing `scripts/fx-smoke.cjs`: all ten groups passed, including alpha/depth,
  traversal-independent alpha ordering, additive overlap, Overlay isolation,
  flipbook pixels, mesh opacity, playback/drain/restart, PNG import/error recovery,
  paused GPU upload reuse and responsive controls. Evidence:
  `build/evidence/fx-milestone-regression/results.json`.
- `scripts/water-depth-smoke.cjs`: both backends passed refraction/distortion,
  absorption, foam, foreground rejection, 1x/4x capture, perspective/orthographic
  views, resize/recovery and optional-target/pass checks. Zero foreground leaks.
  Evidence: `build/evidence/fx-water-depth-regression/results.json`.
- `scripts/fog-smoke.cjs`: both backends passed, including particles, additive
  extinction, water/underwater composition, views, MSAA, recovery and v17 archive.
  Evidence: `build/evidence/fx-fog-regression/results.json`.
- Both installed and CatGame engine-skill copies updated and hash-verified across
  SKILL.md, contract map, integration, validation and the new FX reference, retaining
  CatGame-specific dependency guidance.

Center-pixel soft-depth results matched on both backends: hard [149,37,0], near
soft [15,171,0], far [149,37,0], clear [150,1,2]. Stretch changed 17,748 pixels.
These checks establish the visual contract, not an FPS improvement.

## Legacy fixture provenance

`tests/fixtures/fx_v16.wescene` (253,812 bytes) was saved from the retained
`build/editor-terrain-textures-site` pre-milestone browser build using
`?renderer=webgpu&fxTest=1`, `_engine_fx_fixture(4)` and
`EngineSceneArchive.saveSceneBytes()`. The header was verified as v16 before saving.
It contains two live alpha particles and retained disabled playground content.
SHA256: `3b4d185d90b220d4cab73c1f7a98dbdb27d67dfc7ebaabf79589c7bb8df9f883`.
The native FX test reads the unmodified older-layout file; it is not a relabeled
v17 payload.

## Limits

Desktop Chrome checks do not establish physical Android/iOS acceptance, sustained
mobile performance, other browser acceptance or volumetric correctness. Curved/gust
motion uses bounded numerical approximation. Mesh emission samples static local
triangles, not deformed surfaces. Soft particles use opaque depth and keep existing
center-based transparency sorting. Shared capture currently copies color too.
HDR/bloom, trails/ribbons, collision, GPU simulation and an FX editor remain future
work. No engine pin in a consuming game was advanced.
