# Git synchronization repair, 2026-11-03

## Cause and resolution

Local main at 0d09114 had one unique commit and was missing 53 commits from
origin/main at a630f1f. The commit succeeded; a dry-run push reproduced GitHub's
non-fast-forward rejection. Fetch succeeded. No force push is needed.

The original local tip is retained on codex/backup-before-sync-20261103.
A normal merge preserves both histories. Twenty-two files conflicted. RPG files
retain the remote's extensions to the same R2 transaction implementation; the
original R2 character tests, fixture and consumer match the remote versions.
Documentation retains both the newer RPG milestones and local clothing, foliage
and simulator contracts. D3D12 shader compilation/embedding retains unlitPS and
the four wind entries. The pipeline cache distinguishes unlit and wind choices
independently rather than assigning both to the same bit.

## Checks run on the merged tree

- Windows MSVC Release: engine_renderer_d3d12 compiled, including all DXC entries.
- Windows MSVC Release: clothing_contracts, foliage_contracts and
  simulator_contracts passed (3/3).
- Windows MSVC Release: all 21 standalone RPG, module, quest and consumer runtime
  tests passed. The separately configured RPG bridge-off rebuild was not run.
- Pinned Emscripten 6.0.11 / Node: the same 21 standalone RPG, module, quest and
  consumer runtime tests passed; the separate bridge-off rebuild was not run.
- Native and Wasm seven-case configuration matrices passed when invoked directly
  through tests/rpg_cmake_contract.cmake with the configured generator/toolchain.
- Windows Intel hardware D3D12, debug layer enabled: hidden foliage smoke passed,
  including pause/resume, harvesting, persistence, independent visual/collision
  streaming and controlled device-loss recovery. Fresh captures/report are in
  ignored build/sync-foliage-native/.
  Paused before/recovered PPM files have identical SHA-256 hashes.
- Native D3D12 dense fixture passed with 10,000 clumps and the debug layer enabled.
  This fixture explicitly combines non-layered unlit materials with GPU wind.
  Its fresh report/capture are in ignored build/sync-foliage-dense-native/.
- Pinned Emscripten 6.0.11 / Node: clothing_contracts, foliage_contracts and
  simulator_contracts passed (3/3).
- Edge 156.0.4314.50 headless: foliage browser smoke emitted PASS reports for
  WebGL2 and WebGPU, with no page/console errors. Wind, pause/resume, independent
  visual/collision streaming, harvesting, save and reload assertions passed.
  Reports/captures are in ignored build/sync-foliage-browser/.
- No merge conflict markers remain. Original-source whitespace findings were
  retained rather than altering unrelated content during synchronization.

## Local environment and coverage limits

Initial sandboxed MSBuild worker startup stalled and Emscripten linking could not
write a sandbox temporary JS file. Those build attempts were restarted with the
required access. Emscripten commands initialize the local emsdk_env.ps1; the
global Windows python alias is not the pinned SDK interpreter.
The native and Wasm configuration-matrix CTest wrappers reached their 180-second
deadline during setup on this host; their direct scripts were rerun with cached
builds rather than changing the repository's test timeout.

Native Dawn and physical Android/iOS were not validated in this repair.
The repository and current-user engine skill include rendering-merge.md.
Windows ACLs reject writes to the required Johan installed and CatGame skill
copies even outside the sandbox. Merged SKILL.md files preserve their existing
guidance and are staged with the new reference under
build/sync-skill-update/johan-installed/ and build/sync-skill-update/catgame/.
Installing those two prepared documentation updates requires an owner with write
access; this is independent of the Git synchronization failure.
