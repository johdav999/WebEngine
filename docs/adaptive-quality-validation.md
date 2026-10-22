# Adaptive graphics quality validation — 2026-10-21

Implemented and verified in the shared WebEngine checkout, preserving unrelated
work. No deployed site, consumer engine pin or saved preference changed.

## Portable checks and build

Native Debug CTest passed **8/8** selected suites in `build/native`:
`adaptive_quality`, `render_quality`, `mesh_detail`, `render_frame`, `shadow`,
`application`, `terrain`, `performance`. Used the matching WinLibs MinGW runtime.
The terrain suite's stale writer-version assertion was updated from 31 to the
existing current format 34; its archive, collision and source-sharing checks
remain in place. Adaptive quality itself changes no archive format.

`adaptive_quality_tests` checks disabled behavior, warmup/spikes, bounded first
steps and settling, floor clamping, no repeat publications at the floor,
vsync-compatible gradual recovery, target changes retaining pressure, invalid
samples/settings leaving valid policy intact, interruption/eligibility resets,
exact ceiling restoration, lower authored budgets and preservation of unrelated
MSAA/post/pixel policy. A real portable RenderFrame test verifies coarser projected
LOD selection without changing the source mesh and preserves authored fixed LOD.

Repository-local Emscripten **6.0.11** built Release `webengine`, `web_assets` and
`adaptive_quality_wasm_tests` in `build/web-release`. The focused WebAssembly test
passed in Node. `EMCC_TEMP_DIR` used the existing task-local generated build folder.
JavaScript syntax checks and diff whitespace checks passed.

## Browser acceptance

Served the selected `dist` on `http://localhost:8137`. Browser suites ran serially
with Chrome **155.0.8059.85**, actual **WebGPU** and forced **WebGL2**.
`scripts/adaptive-quality-smoke.cjs` passed at **DPR1 and DPR2**:

- Actual RAF intervals with a chosen 4 ms target produce bounded resolution/detail
  reductions. The timing replay export rejects calls without its test query.
- Explicit deterministic replay follows the same controller/publication path,
  exercising first-step size, settling, floor and slower gradual recovery.
- Effective backing resolution and actual shadow/terrain submissions decrease;
  ceiling versus floor screenshots visibly differ on each renderer.
- Scene archive hashes remain identical while paused before/after adaptation and
  real device/context loss; authoring/simulation data are not quality state.
- Requested 4x MSAA survives adaptation. Target validation, invalid manual edits,
  target changes retaining pressure and quality-floor reporting work.
- Suspension/resize/recovery retain the active quality and ceiling. Disable
  restores selected settings; a manual profile change stops adaptation.
- Performance JSON exposes the transient state, and compact controls remain
  usable. No final page/console errors occurred.

Evidence: `build/evidence/adaptive-quality/results.json` and
`build/evidence/adaptive-quality-dpr2/results.json`, with ceiling/floor/control
screenshots. Timed real-frame feedback and deterministic replay are reported
separately. Replay is correctness evidence, not a workload benchmark.

Existing `scripts/render-quality-smoke.cjs` passed all eight checks: explicit
profiles/budgets, invalid settings, identical paused recovery pixels, composed
host lifecycle, compressed/RGBA fallback, bounded recovery/retry, fallback
acquisition, GUI recovery and pending-acquisition shutdown. Its section locator
now selects **Resolution and shadow budgets** explicitly because the panel has
multiple independent sections. Results: `build/evidence/adaptive-existing-quality/results.json`.

Existing `scripts/terrain-detail-smoke.cjs` passed both renderers: four material
layers, weight normalization, invalid edits, camera/shadow LOD independence,
automatic selection, crack-free full/coarse/mixed coverage, ground/collision
agreement, projections/resize, files/malformed preservation, paused recovery and
demo return/reset/restart. Its stale format-23 assertion now expects current
format 34. Both current backends render shadows, so the former WebGL2 no-shadow
assertion now verifies shadow submission reduction and enabled controls on both.
Results: `build/evidence/adaptive-existing-terrain-detail/result.json`.

## Staged identity and boundaries

SHA-256 of the final served artifacts:

| File in `dist` | SHA-256 |
| --- | --- |
| `webengine.wasm` | `5f1e08448414fcc1025303634a007d9468cd1795348dc3c1b1a026a1d06df286` |
| `asset_decode.wasm` | `c29f77dac3ce3a21ecdea8eccb0a516ca84f7a6013465bdb3cf403418f554543` |
| `quality.js` | `c7d7d655da388be68e3252964e1cd139b6dd006f9284ae12695fa8529ca2d95b` |

These establish functional feedback/control and resource/submission behavior on
desktop Chromium. They do not establish sustained frame-rate improvement,
physical Android/iOS acceptance, other browsers or a guaranteed target. Frame
intervals include scheduling and CPU/GPU pacing; they do not identify a bottleneck.
Minimum budgets, refresh rates and CPU-heavy work can leave the target unattainable.
Read [contracts](adaptive-quality.md) for defaults, tuning and ownership.

The installed engine-skill and CatGame repository copy were updated together,
including supporting API/validation references. CatGame-specific guidance and
its selected dependency pin were retained.
