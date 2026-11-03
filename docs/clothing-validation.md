# Clothing milestone one validation

Work performed 2026-10-25. See [contracts and commands](clothing.md).

- Native Windows configuration attempted with Visual Studio 18 2026, x64 and
  clothing library/tests/demo enabled. CMake could not find any Visual Studio
  instance; vswhere found no installation providing the VC x86/x64 tools.
  Native compilation and Dawn/direct D3D12 runtime checks remain unverified.
- Emscripten 6.0.11 Release built `clothing_tests` and `clothing_demo`. CTest
  `clothing_contracts` passed, covering the final shared palette, disabled follower
  players, rigid hat movement, animated bounds/collection, body hiding, atomic
  rejected replacements, malformed saves, independent colors/poses with shared
  assets, pending destruction, mismatched rigs, clear and driver destruction.
- Edge 155.0.4283.62 headless browser smoke passed separately on WebGL2 and WebGPU,
  with no captured page/console errors. Checks cover all four outfit slots, tint
  changes, paused presentation, localStorage save/restore, invalid-save rollback,
  reload persistence and changed rendered pixels. Before/after screenshots were
  inspected. Reproducible script: `scripts/clothing-browser-smoke.cjs`; local
  screenshots/JSON reports: `build/clothing-smoke/` (ignored build evidence).
- `examples/clothing/windows.cpp` passed a syntax-only check using the available
  Emscripten compiler. This checks example C++ expressions against the public host
  headers; it is not a Windows compiler, linker, storage or GPU runtime test.
- Documentation links and all six updated skill installations passed checks;
  installed/CatGame clothing references match the repository reference. Existing
  RPGGame/CafeGame project guidance and dependency pins were preserved.

No physical mobile device acceptance or sustained performance claim is made.
