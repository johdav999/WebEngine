# Physical clothing milestone two: validation

Work date: 2026-10-25. Contracts: [cloth-dynamics.md](cloth-dynamics.md).

Results below distinguish source integration from platform execution.

- Native tool discovery: `vswhere` found no Visual Studio installation with the
  VC x86/x64 tools. Native Windows compilation and Dawn/direct D3D12 execution
  require that toolchain; browser checks do not establish native acceptance.
- Native configuration was retried with Visual Studio 18 2026 x64 and the clothing
  library/tests/demo enabled. CMake again reported that it could not find any Visual
  Studio instance. The Windows example and cloth C++ source passed syntax-only
  compilation with the available Emscripten compiler; this is not a native build.
- Emscripten 6.0.11 Release built `clothing_tests` and `clothing_demo`. Physical
  contracts passed through CTest `clothing_contracts`: fixed-step partitioning with
  static targets, pins/stretch limits, wind, capsule clearance, zero-delta freeze,
  catch-up budget, invalid-input rollback, automatic/manual reset, dynamic bounds,
  normals/tangents, no second skinning, equip/save/load, unsupported scale and driver
  destruction. The final run also passed stable stream identity/revision reuse and
  rejection of updates during guarded traversal. Existing M1 contracts are included
  in the same executable; final CTest result: 1/1 passed (1.23 seconds).
- Documentation links, skill validation and synchronization checks passed for all
  six repository/installed/game skill copies, preserving unrelated project guidance.

- Edge 155.0.4283.62 headless passed `scripts/clothing-browser-smoke.cjs` separately
  on WebGL2 and WebGPU, with zero captured page/console errors. Both verified
  changing cape positions over simulation steps, frozen steps/positions while paused,
  reset/teleport commands, cape removal/recreation, saved selection restoration and
  invalid-save rollback. Outfit before/after comparisons changed 26,375 pixels on
  WebGL2 and 27,758 on WebGPU. The physical cape screenshots from both backends were
  visually inspected.
- Local evidence is under `build/cloth-m2-smoke/`: backend JSON reports and
  `*-before.png`, `*-after.png`, `*-cloth.png`. This ignored output is reproducible
  by setting `CLOTHING_SMOKE_OUTPUT=build/cloth-m2-smoke` before running the smoke
  script against the clothing site on port 4193.

No physical mobile device or sustained performance acceptance is claimed.
