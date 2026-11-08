# R9 validation — shared rendered hosts

R9 delivers the neutral Training Court: shared scene/controller, browser WebGPU and
WebGL2 hosts, Windows DesktopHost/D3D12, combined storage, recovery and test tools.
The runtime implementation validated here is `0f951f70e896eb99637afae9449595b351672d1c`.
Later handoff edits add documentation and the locally tested hardware-browser launch
option; they do not change the compiled game or renderer. Reproduce using
[host instructions](rpg-hosts.md).

## Successful checks (2026-10-25)

| Check | Result and actual coverage |
| --- | --- |
| Emscripten 6.0.11 Release | `rpg_demo` and `rpg_host_tests` built; 27 shared controller assertions passed under Node |
| Windows Release | MSVC built `rpg_demo_windows` and `rpg_host_tests`; native shared-controller CTest passed |
| Standalone RPG / Modules | Windows, Linux and pinned Wasm suites passed; default-off and dependency configuration contracts remain intact |
| Browser WebGL2 | Edge 155.0.4283.62, headless SwiftShader requested; full input/pixel/storage/reload/shutdown test passed |
| Browser WebGPU | Same Edge version, local hardware requested with `RPG_HARDWARE=1`; full input/pixel/storage/reload/shutdown test passed |
| Native D3D12 | CI-built Windows executable run locally on **Intel Iris Xe Graphics**, `software:false`; 39 frames, two reward receipts, smoke exit code 0 |

The browser scripts drove buttons, canvas keyboard input and camera dragging, checked
pause/turn invariants, save failure retention, scene replacement, duplicate reward
rejection, focus recovery, persisted reload and explicit shutdown. WebGL2's initial
canvas contained 349,636 visible pixels / eight quantized colors; WebGPU contained
349,638 / eight. Flat unlit primitives deliberately use a small palette. Captures
were also visually inspected; they are not just nonzero-frame counters.

Native smoke injected focus, keyboard and pointer-drag events into SDL's native queue
and exercised DesktopHost routing while its window remained hidden. It also checked
equipment/reward/movement, pause recovery, save/write failure, stale queued commands
on travel, load, corrupt-load rejection and shutdown. D3D12 captures were 1099x759:
before 103 quantized colors / 293,175 visible pixels; after 104 / 297,970. The revised
two-column GUI was inspected at 150% display scaling so all controls fit. This is
automated native-event coverage, not a manual physical keyboard/mouse session.

Local evidence is retained under RPGGame's ignored `build/rpg-r9/`: `evidence/`,
`evidence-hardware/` and `native-evidence4/` (reports, screenshots, PPMs and device
diagnostics). The native executable came from the workflow artifact for the exact
runtime commit above, avoiding any claim that local MSVC was installed.

## CI results and remaining limitations

- [Runtime build, host tests and GPU attempts](https://github.com/johdav999/WebEngine/actions/runs/40208357898).
- [Standalone RPG suites](https://github.com/johdav999/WebEngine/actions/runs/40208357849).
- [Portable module SDK](https://github.com/johdav999/WebEngine/actions/runs/40208357824).
- [Earlier independent WebGL2 CI acceptance](https://github.com/johdav999/WebEngine/actions/runs/40207612522) passed on Chromium 151.0.7922.34 with SwiftShader.

**The rendered-host CI workflow is not fully green.** Software WebGPU on the Ubuntu
runner reported `GPU recovery exhausted: Device was destroyed.` and failed the
frame-readiness assertion. Local Edge with SwiftShader returned no WebGPU adapter;
the local hardware WebGPU run passed. Windows runner startup exited with access
violation `0xc0000005` before a capture, although the same compiled executable passed
on the local hardware device. The cause of that runner startup failure has not been
established. These failures remain visible; neither is relabelled as a successful
GPU test. The harness retains process diagnostics and runs browser backends separately.

No physical mobile, additional browser/driver, manual native-input, sustained
performance or real GPU-device-loss acceptance is claimed. Recovery tests interrupt
presentation/focus; they do not reset a physical driver. R10 must retain these limits
and establish reliable GPU-runner acceptance before claiming broad release coverage.

RPGGame remains pinned to `56080eca124c9a62e15b146799a8004bdad5a9b2`.
No game lore, engine pin, save migration policy or paid asset generation changed.
