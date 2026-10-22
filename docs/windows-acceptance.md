# Broader Windows acceptance

## Profile and boundaries

The optional `windows_acceptance` target runs the shared C++20 engine and WebGPU
renderer on SDL3/Dawn D3D12, Windows x64 MSVC Release. It is opt-in because it
briefly changes its own window size, monitor and fullscreen state. It never
changes global display settings, sleeps the computer or resets a display driver.
It needs an interactive Windows desktop. It is not registered as an unattended
CTest. The same engine, CPU assets, feature contracts and WGSL serve the browser.

`examples/windows_acceptance_scene.hpp` contains portable application content.
The driver in `tests/windows_acceptance.cpp` owns SDL/Win32 automation. No test
input or scene policy enters reusable rendering/simulation systems. The gallery
uses the existing binary FBX skeletal fixture, generated chunked terrain and
existing water, particle, material/light and post-processing APIs. It exercises
opaque/skinned/shadow/transparency/reflection/refraction/underwater/HDR/MSAA paths
in a combined scene, then transitions to another scene and unloads the gallery.

## Repeat the checks

```powershell
./scripts/build-windows.ps1 -BuildDir build/windows
./scripts/windows-acceptance.ps1 -BuildDir build/windows
# Optional physical controller: neutralize, move stick + right trigger together,
# press A, disconnect and reconnect. Virtual devices do not count.
./scripts/windows-acceptance.ps1 -BuildDir build/windows -ControllerSeconds 90
# Run acceptance first, then leave the animated gallery open. Escape quits; F11
# toggles fullscreen. Verification/report generation finishes after closing it.
./scripts/windows-acceptance.ps1 -BuildDir build/windows -Interactive
```

An explicit `-EvidenceDir` must be new. The runner records executable/SDL/DXC
SHA-256 hashes, revision and dirty-file inventory before launch. A dirty checkout
is not identified by HEAD alone. Native reports contain selected adapter info,
display/controller inventory, per-capture state/counters and case outcomes.
The verifier writes PNG previews, `verified.json` and `index.html` alongside PPM
readbacks. Failed runs are retained and never silently merged into new evidence.

The controller option records physical SDL event timestamps and checks real axes,
buttons, disconnect/reconnect, mapped engine actions and cancellation. It is an
operator exercise, not a controller emulator or a complete compatibility matrix.
DPI changes are exercised by moving the actual window across attached displays;
without different scales the physical transition remains `not-run`. Numeric
100/150/200% coordinate checks are separate from a physical monitor transition.

## Acceptance ledger

| ID / flow | Priority | Acceptance and evidence | Status |
| --- | --- | --- | --- |
| WIN-ACC-001, rendering | P2 | Native feature controls visibly differ; actual skinning, particle and three shadow passes execute. Water clock, bloom, underwater and 1x/4x MSAA each affect pixels. | Verified below. |
| WIN-ACC-002, window/lifecycle | P1 | Actual resize/fullscreen/minimize/restore; explicit pause survives; injected sent Win32 power messages suspend/reset timing/resume. | Verified below. |
| WIN-ACC-003, hardware | P2 | Physical controller actions/disconnect/reconnect and a window transition between differently scaled displays. | Open; hardware unavailable. |
| WIN-ACC-004, recovery | P1 | Destroy Dawn device, observe recovering, new device generation, render same paused CPU scene with HDR/4x MSAA and preserved settings; compare pixels. | Verified below within the induced-loss scope. |
| WIN-ACC-005, recovery intermittency | P2 | Repeat lost-device/readback stress while recording focus, suspension and adapter state; establish why the exploratory run2 capture timed out. | Open investigation; later functional runs pass. |
| System sleep/driver reset | P2 | Operator sleep/hibernate/modern standby, wake and physical TDR/adapter loss. | Separate hardware acceptance; never inferred from injected messages/device destruction. |

Pixels use paired feature controls with fixed camera, clocks and other content.
Minimum changed-pixel assertions avoid driver-specific golden screenshots.
Recovery permits fewer than 200 pixels differing by >5 and mean channel error
below .15. Counters independently establish skin/particle submissions and two
sun cascades plus a spot shadow. GUI-free captures cannot pass on text alone.
Captured frames also include underwater, native fullscreen and scene replacement.

The host fixes exercised here are Windows power-notification routing and display
scale conversion for GUI/camera pointers and native IME areas. The diagnostic
`allowGraphicsFaultInjection=false` default and `simulateGraphicsLoss()` keep
intentional device destruction opt-in and restricted to idle host boundaries.
No OS/GPU types enter the public host API.

## Recorded run — 2026-10-21

The final native run is `build/evidence/windows-acceptance-release/`, with 20
actual swapchain readbacks, PNGs, HTML review page, `native-report.json`,
`verified.json`, native log and binary provenance. Selected adapter: NVIDIA
GeForce RTX 5060 Ti (Blackwell), D3D12 driver 32.0.16.1088, hardware adapter.
Display: MAG 342CQR E2, 3440x1440 at 100% scale. Fullscreen used the actual display
extent; resize captures include 800x600. The executable was built from the shared
dirty checkout with unrelated work preserved; use the recorded binary hashes.

| Comparison | Pixels differing by >12 in any channel |
| --- | ---: |
| Animation clip left/right | 607 |
| Raised/flat terrain | 3,906 |
| Water on/off | 51,800 |
| Particles on/off | 733 |
| Lights on/off | 128,004 |
| Shadows on/off | 43,841 |
| Water clock advancement | 5,629 |
| Post-processing on/off | 614,397 |
| Bloom on/off | 2,711 |
| 1x/4x MSAA | 2,109 |
| Underwater effects on/off | 391,809 |

WIN-ACC-001/002/004 passed. Device generation increased, the scene and explicit
pause survived, and the recovered frozen frame differed in only 33 pixels by >5,
mean channel delta .00302. Two sun cascades plus one spot shadow, skinned geometry
and 44 particles were actually submitted. Running and explicitly paused
applications both survived minimize/restore; paused power-notification routing
and fresh resume timing passed. A subsequent scene transition unloaded the
gallery and rendered replacement content. Repeated complete native runs also
passed; this is functional evidence, not a sustained performance benchmark.

The focused native regression selection covered 18 tests: terrain, water,
render_quality, application, shadow, input, gui, fx, animation, render_frame,
camera_follow, desktop_api/text/services, desktop_persistence_write/read and
desktop_demo_write/read. Seventeen passed on the first run. Water's old test
required global archive version 31 despite the checkout writing version 34;
the test now checks its minimum version while retaining exact round trips and
legacy compatibility. Its focused rebuild/recheck passed. Logs are
`build/windows-acceptance-ctest.log` and `build/windows-acceptance-water-recheck.log`.
The updated native vehicle/input/menu smoke passed separately in
`build/evidence/windows-acceptance-vehicle/`.

The interactive option also passed an owner-window Escape check after entering
its live animation loop, including the handoff from deterministic timestamps to
the host's monotonic clock. That repeat captured 19 frames in
`build/evidence/windows-acceptance-interactive-final/` (it retains the gallery
instead of unloading it for the transition screenshot). Its readiness message is
flushed for redirected-console/operator tooling. The imported GLB vehicle variant
has separate evidence in `build/evidence/windows-acceptance-vehicle-glb/`.

The current browser target rebuilt with repository-local Emscripten 6.0.11. Its
frozen site is `build/windows-acceptance-browser-site/`, with hashes in
`build/evidence/windows-acceptance-browser-provenance.json`. Serial
`lighting-post-smoke.cjs` passed all 10 lighting/HDR/MSAA/resize/recovery checks
per backend; `vehicle-smoke.cjs` also passed on WebGPU and forced WebGL2 with
empty error lists. Evidence is `build/evidence/windows-acceptance-browser-lighting/`
and `build/evidence/windows-acceptance-browser-vehicle/`.

WIN-ACC-003 remains open: the operator confirmed no physical controller, and only
one 100% display was available. No physical mixed-DPI change, controller input/
disconnect, OS sleep/hibernate/modern-standby wake, TDR or adapter removal was run.
The report therefore sets `fullHardwareAcceptance=false`, even when automated
checks pass. Injected WM_POWERBROADCAST tests do not establish physical sleep.

An exploratory run (`build/evidence/windows-acceptance-run2/`) timed out waiting
for post-recovery readback after Dawn reported ready. Its cause was not established;
focus suspension can legitimately prevent presentation, but that run did not yet
record suspension at timeout. Later runs include timeout state diagnostics and
passed, including identical recovery comparisons. Keep this as an open P2
intermittency/stress investigation (WIN-ACC-005), not a demonstrated recovery fix
or universal reliability claim. Earlier failed exploratory reports are retained.
