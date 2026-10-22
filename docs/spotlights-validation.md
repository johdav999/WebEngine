# Spotlight validation — 2026-10-19

Stage 7 delivers ordinary unshadowed Spot lights, smooth cone/range attenuation,
typed editor history and bounds, v30 persistence/prefab support, and a separate
headlight example. This work preserves the shared checkout's other features.

## Builds and portable checks

- Repository-local Emscripten **6.0.11**, Release `build/fx-contact-web`, selected
  `web_assets` publication to `dist`. Host, worker, loader/Wasm and source adapters
  are checked together in the recorded build metadata.
- Native MinGW Debug `build/fx-contact-native`: **67/67 CTest suites passed**,
  **12.47 seconds** (`build/stage7-ctest.log`). This includes existing concurrent
  features; Stage 7 adds `spotlight`.
- Explicit `spotlight_wasm_tests`: **112 checks passed** in Node on the 32-bit Wasm
  ABI (`build/stage7-wasm.log`). The focused native suite also passes 112 checks.

Coverage includes exact cone inner/outer/range boundaries, smooth interpolation,
invalid direction/angles/shadow settings, rotated/nonuniformly scaled parents,
world-unit cone bounds, masks/disabled ancestors, degrees/radians authoring,
undo/redo and invalid edit retention, duplication, exact scene round trip and Play
isolation. A synthetic v29 light fixture removes only v30 cone fields and repairs
the length/checksum: Point type/zero unused direction and default angles load
correctly. Invalid cone bytes with a repaired checksum are rejected by v30 loading.
Prefab/capability contracts are checked.

The full regression caught an accidental common-component validation call during
collection. Removing it retained allocation-free light settings validation.
`render_frame` now passes its existing 1000-iteration warm collection/preparation
zero-heap-allocation checks. The expanded 64-byte GPU light, 2592-byte frame uniform
and retained upload-cache sizes are updated together, guarded by compile assertions.

## Browser acceptance

Chrome **155.0.8059.80**, forced **WebGPU and WebGL2**, 1500x1050 CSS pixels at
**DPR 1 and 2**, resize/recovery at 1180x850:

- `build/evidence/spotlights/results.json`
- `build/evidence/spotlights-dpr2/results.json`

`scripts/spotlight-smoke.cjs` checks actual cone centre/transition/outside pixels,
Point contrast, range and direction cutoff, transformed parent output, separate
red/blue masked views, first-eight-light truncation, 1x/4x MSAA, visible headlights,
typed light properties and cone overlay, undo/redo, invalid cone/shadow retention,
duplication, isolated Play source SHA-256, v30 save/open, corrupt-file retention,
resize and GPU recovery. The deliberately excessive ninth light triggers the
expected WebGPU overflow diagnostic, recorded separately; no unexpected console/
page errors or failed requests remain.

For the WebGPU cone fixture the centre measured `[169,169,169]`, transition
`[139,139,139]` and outside `[0,0,0]`. Comparisons test each backend's cone/range
behaviour rather than asserting equal PBR/basic-lighting output between backends.
Backend-specific editor, headlight, mask/budget, MSAA and recovery screenshots were
inspected for lit geometry and readable controls. The native cone law tests the
exact angular boundaries; browser probes sample inside, transition and outside.

The UI check found the new section still in the hidden non-editor sidebar; it now
relocates its same controls into the Details dock across mode changes. The real
`?editor=1&scene=spotlight` URL also waits for normal model startup before opening
the example. `scripts/spotlight-example-smoke.cjs` verifies both lights and visible
illuminated pixels on both backends (`build/evidence/spotlight-example/results.json`).

Affected regressions passed:

- Directional WebGPU shadows: `build/evidence/stage7-shadow-regression/results.json`.
- Both-renderer animated water, lighting/reflections/MSAA and archive/recovery:
  `build/evidence/stage7-water-regression/result.json`.

Exact artifact/report hashes are recorded in
`build/evidence/spotlights/build-metadata.json`. Architecture, feature contracts
and installed/CatGame engine-skill copies are updated; no consumer pin advanced.

## Scope

Spotlights remain unshadowed and use the existing finite-range attenuation law.
No volumetric beams, clustered/per-object ranking, automatic vehicle controls or
additional shadow passes are claimed. Eight slots are shared per camera, including
zero-intensity lights. These desktop and DPR-emulated checks do not establish
physical Android/iOS acceptance or sustained maximum-content performance.
