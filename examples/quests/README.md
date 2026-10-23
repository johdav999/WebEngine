# Quest module and host sample

This optional C++20 consumer demonstrates explicit quest-module composition and a
custom delivery objective. Harbour and Orchard are disposable scene projections of
one shared session. Both hosts use the same controller, save bytes and quest rules.

Activate, deliver two apples, then choose Friendly (500 ms of simulation) or Quiet
(open the gate). Pause freezes progress. Travel preserves it. Save persists the
quest state and wallet together; startup and Load restore the saved location and
pause state. Retry cannot grant the first-completion coins twice.

Enable `WEBENGINE_BUILD_MODULES`, `WEBENGINE_BUILD_QUESTS` and
`WEBENGINE_BUILD_QUEST_DEMO` in the configured engine build. Build `quest_demo`
for Emscripten and serve the generated `quest-site`, or build/run
`quest_demo_windows` with the Windows desktop dependencies. `--d3d12` selects the
direct Windows renderer; default is Dawn/D3D12.

Native examples:

```powershell
& build/windows/Release/quest_demo_windows.exe
& build/windows/Release/quest_demo_windows.exe --import session.qhost --export result.qhost
```

Browser Export/Import uses those same `.qhost` files. Save is explicit; imported
state remains in memory until saved. No Paws data or default browser profile is
used by the automated tests.

The standalone `examples/quests/CMakeLists.txt` builds only the M2 kernel tests
without renderer/core dependencies. Host lifecycle tests additionally need engine
core: enable `WEBENGINE_BUILD_QUEST_TESTS` with the host demo and build
`quest_host_tests`. These run natively and under Node/Wasm. The rendered matrix is
`scripts/quests-host-smoke.cjs`; it runs both browser backends and native process
restart/import phases on Dawn/direct D3D12.

Read [kernel contracts](../../docs/quests.md), [host contracts](../../docs/quests-host.md)
and [reproduction/evidence](../../docs/quests-host-validation.md) for exact ownership,
scope and validation instructions.
