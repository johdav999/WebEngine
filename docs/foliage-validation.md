# Foliage validation

This records the earlier M1–5 CPU-path checks. See [M6 validation](foliage-dense-validation.md)
for the current GPU default, bulk rendering and new tests; the results below are historical.

Validation performed on 2026-10-28 against the shared checkout, preserving unrelated
work. Contracts and build commands are in [foliage.md](foliage.md).

## Portable and native contracts

- Repository-local Emscripten **6.0.11**, Release: `foliage_tests` and
  `foliage_demo` built. Final `foliage_contracts` passed **1/1**, 4.93 seconds
  (5.10 total), using the repository-local Node emulator.
- Windows x64, VS2022 / MSVC 19.44, Release: `foliage_tests` and
  `foliage_demo_windows` built with direct D3D12 enabled and Dawn disabled.
  Final native `foliage_contracts` passed **1/1**, 3.94 seconds (4.32 total).
- Disabled-library CMake script guards passed: all foliage options off succeeds;
  enabling foliage tests without the library fails with the explicit requirement.

The same test source covers positive/negative cells, repeatable placement and
stable IDs, cross-cell spacing, height/slope/mask/exclusion filters, ground
alignment, translated source terrain and holes, malformed samples/definitions,
candidate/instance budgets and extremely sparse density without coordinate
narrowing overflow. Runtime fixtures cover independent visual/collision focus,
registered/queryable trunk bodies, generation-checked handles, harvest persistence
through eviction, removal saves and malformed-load rollback, failed source/update
preservation, source edits, idle traversal enforcement, transient archives,
collision-free visual-only use, bounded admissions, pending convergence,
hysteresis, teleport eviction and ordinary static instance batching.

Presentation fixtures cover root pinning, complete deformed bounds, explicit-time
wind, unchanged paused streams (including unchanged residency updates), revision
and cache-identity preservation, color/shadow pose identity, invalid-input
preservation, explicit lower details, distant shadow reduction, draw-distance
omission and wind disable/restart. Collection never advances wind.

## Windows rendering

The final command
`foliage_demo_windows.exe --d3d12 --smoke build/foliage-native` exited 0 with
`Windows foliage smoke PASS`. It exercised actual rendering/capture, positive
plant/trunk counts, paused time/deformation, harvest count changes, local file
save/restore, streaming with unchanged gameplay collision, wind disable/resume,
explicit clear and host shutdown. Final state: 560 visual instances, 23 trunk
colliders and one saved removal.

The hidden acceptance runner explicitly clears lifecycle suspension before each
test step. This enables capture without native window focus; ordinary interactive
hosts retain normal focus/lifecycle behavior. It is not native focus/input
acceptance. The initial hidden run timed out under lifecycle suspension before
this test-only handling was added.

PPM captures and result JSON are in `build/foliage-native`; converted PNGs and pixel
comparisons accompany them (1099x750; 75,105 pixels changed after streaming and
29,987 after wind/harvest/resume). A native capture was inspected: trunks/crowns,
double-sided cutout plants, terrain and the control panel were visible without
broken geometry. No Windows Dawn run was performed; the source uses existing
portable mesh/deformation paths, but D3D12 evidence is not Dawn acceptance.

## Browser rendering

Final WebGL2 and WebGPU rendering checks are recorded below after running
`scripts/foliage-browser-smoke.cjs`. Reports/screenshots live in
`build/foliage-browser`. The fallback uses one visual cell, preserving the same
source catalog/IDs and nine gameplay collision cells; WebGPU uses nine visual
cells plus budget-permitted hysteresis cells.

Both paths passed in **Edge 156.0.4314.50**, with no reported console/page errors:

| Backend | Wind changed pixels | Streaming changed pixels | Plants after saved reload | Gameplay trunk colliders |
| --- | ---: | ---: | ---: | ---: |
| WebGL2 | 8,019 | 15,588 | 45 | 23 |
| WebGPU | 32,600 | 88,298 | 422 | 23 |

The script requires actual moving deformation before pause, freezes simulation
time/motion while paused, compares wind-on/off pixels, verifies one-plant harvest,
regrowth/save/restore, invalid-save rollback, visible cell streaming without a
collision-count change, removal retention after re-entry, wind resume and
localStorage restoration after a full page reload. Restored screenshots from
both paths were inspected. The different visible populations reflect explicitly
different presentation residency, with identical source generation and collision
coverage. Pixel counts are bounded smoke evidence, not a performance benchmark.

## Documentation and scope

Architecture, feature contracts, README and the engine integration guide were
updated. The repository and current-user engine-skill copies received a foliage
reference and routing guidance; both passed `quick_validate.py`. The two required
Johan/CatGame copies remain **pending a manual Windows administrator update**: their ACLs
allow this process to read but deny writes. The prepared updater is
`.tools/sync-foliage-skills.ps1`. Tool-driven elevation returned a cancellation
without a visible Windows prompt; those stalled launches were stopped. Prepared
copies are in `build/foliage-skill-update`. Running the updater with `-Scope Protected`
from an administrator PowerShell applies only the two outstanding destinations.
Completion of that required documentation gate must be verified separately
before reporting the entire task complete.

Sandboxed native builds stalled before compilation and Emscripten links hit
temporary-file access restrictions. Working runs used the installed toolchains
outside the sandbox with `.tools/foliage-emcc-temp` as TEMP/TMP. These are environment
execution constraints, not a renderer-support claim.

This is bounded CPU wind and ordinary scene-component foliage. GPU wind, bulk
grass instances, impostor baking, asynchronous worker publication, interactive
bending and automatic biome/terrain subscriptions are not supplied. Tests do not
establish physical Android/iOS acceptance, sustained device performance, universal
foliage density or general engine/other-game regressions. No full repository test
suite or unrelated consumer upgrade was performed.
