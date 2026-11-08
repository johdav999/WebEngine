# Git synchronization repair, 2026-11-08

Local main at `76e30a3` had one unpublished commit and lacked 58 commits from
`origin/main` at `28522cc`. The working tree was clean. A fetch confirmed the
expected remote `https://github.com/johdav999/WebEngine.git`. The original local
tip is retained as `codex/backup-before-sync-20261108`.

A normal merge preserves both histories. Fifteen files conflicted. Resolutions
retain local clouds/fog, advanced materials, clustered LOD, dynamic lighting and
camera render surfaces alongside upstream foliage wind/instancing, the D3D12
unlit specialization and optional RPG/clothing/foliage/simulator additions.
No force push, reset or history replacement is used.

## Integration details

- Preserve the same ordered 34 DXC entries in CMake and shader embedding.
- Keep wind independent from cloud/surface pipeline bits and D3D12 root bindings.
  Wind uses root21/22; fog17, GI18, fog optical cache19 and cloud noise20 remain.
- Retain advanced material data plus wind phase in the 720-byte D3D12 object
  constants, and 176 attribute bytes in each portable 256-byte object slot.
- Compose clouds in the unlit fast path before fog, including planar reflections.
- Preserve WebGL2 instancing, cluster-selected ranges and per-instance shadow
  receiving. Scope cached wind uniforms to the selected material program.
- Preserve scene declarations and implementation includes for both histories.

## Fresh checks

- Native Windows Release: Dawn and direct D3D12 compiled, including all DXC entries.
- Native contracts: render_frame, camera_follow, fog, sky, render_quality,
  clothing_contracts, foliage_contracts and simulator_contracts passed (8/8).
- Pinned Emscripten 6.0.11/Ninja: browser and foliage demo compiled;
  clothing_contracts, foliage_contracts and simulator_contracts passed (3/3).
- Cloud rendering: 34 scenarios each on WebGPU, WebGL2, Dawn and D3D12 (136 total).
  Checks cover terrain shadows, depth composition, planar water reflections,
  opt-out/fallback, HDR/MSAA, browser recovery, controls and explicit clock steps.
- Native foliage: direct D3D12 and automatic selection resolving to Dawn passed
  wind, harvest/save/restore, visual/collision separation and injected recovery.
  Before/recovered paused images are pixel-identical; streaming and resumed wind
  change tens of thousands of pixels on each backend.
- Direct D3D12 also passed the 10,000-clump fixture combining unlit materials
  with instanced GPU wind and active wind draw counters.
- Browser foliage: WebGPU and WebGL2 passed visible wind, pause/resume, instancing,
  harvesting, persistence/reload and separate collision/visual streaming without
  page or console errors.
- No conflict markers remain and shader compile/embed order matches. Three
  upstream blank-line-at-EOF findings are retained in unrelated imported files.

Evidence is under ignored `build/evidence/push-fix/`, with build/test logs named
`build/push-fix-*`. Browser tests use the matched `build/push-fix-site/` snapshot
on port8186. The native incremental build initially retained a pre-merge
nonvirtual morphPose object; rebuilding engine_core resolved that linker failure.
The browser configure explicitly retains its existing Ninja generator.

The repository skill and both required installed/CatGame copies include the
combined rendering contracts. Standalone RPG and physical Android/iOS suites were
not rerun as part of this synchronization repair. Prior release documents retain
their historical evidence and limitations.
