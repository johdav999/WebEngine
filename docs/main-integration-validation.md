# Main branch integration validation

2026-10-23: integrate local `209c28a` with remote `633dce6` after a
non-fast-forward push rejection. The starting working tree was clean; local
main was one commit ahead and four commits behind. The merge preserves both
histories and all local engine extensions, including hybrid terrain and the
remote module/quest SDK consumer handoff.

Conflicts retain both CMake/documentation feature sets, the reusable browser
target, standalone enabled/disabled consumers and native runtime dependency
staging. Desktop pointer preview/post-GUI notifications now compose with the
existing touch/pen GUI owner and backend-independent graphics wrapper. Preview
reservation cannot steal active touch GUI capture; handled notifications do not
also create gameplay pointer input. Optional water phase overrides use the same
graphics interface on both native backends; null preserves simulation-clock
pause behavior. Archive regression assertions retain the local version-36 writer
instead of restoring the older upstream version-34 expectation. Readers and
serialization were not changed by this merge.

Fresh evidence is in `build/evidence/git-sync/`:

- `sdk-native.log`: independent module consumer, quest-binding consumer,
  modules-disabled consumer and quests-disabled consumer all configured, built
  and passed CTest on Windows x64.
- `sdk-wasm.log`: the same four configurations passed CTest using the pinned
  repository Emscripten 6.0.11 and Node emulator.
- `hosts-native.log`: Windows module/quest rendered hosts built. Ten CTests
  passed: editor_fx, terrain_paging, spline_route, physical_surface, region_stream,
  gui_clear, desktop_api, desktop_api_d3d12, desktop_pointer and
  desktop_pointer_d3d12. Added regressions exercise preview reservation, normal
  retained-GUI dispatch, handled post-GUI contacts, touch ownership and water
  phase validation.
- `hosts-wasm.log`: engine_browser, module_demo, quest_demo and gui_clear_tests
  built. Node/Wasm GUI clear capture, focus, stale-ID and rebuild checks passed.
- `rendered.log`, `modules-browser/report.json`, `quest-host/report.json`: fresh
  isolated local servers and real Chrome controls passed forced WebGPU/WebGL2
  module counters and quest-host pause, save/load, receipts and scene travel.
  Quest checks ran browser-only; native quest file interchange was not rerun.

The broader existing engine/device matrices were not rerun for this integration.
No consumer engine dependency pin or game-specific rules were changed. Both
installed engine-skill copies retain the SDK and merged host contracts.
