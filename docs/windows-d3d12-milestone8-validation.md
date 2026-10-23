# Direct D3D12 milestone 8 validation

Verified 2026-10-22 on Windows x64 with the NVIDIA GeForce RTX 5060 Ti. This record
covers [device recovery, instancing and compressed uploads](windows-d3d12-recovery-performance.md).
Previous milestone evidence remains historical.

## Native Windows

MSVC Release and DXC `-O3 -WX` builds passed with Dawn/direct D3D12 both enabled,
and with Dawn disabled in the separately rebuilt `build/windows-direct-m7` tree.
The vehicle, native galleries and editor were relinked. Direct acceptance enabled
the D3D12 validation layer and reported no ERROR/CORRUPTION messages.

`windows_performance_smoke` and its independent pixel verifier passed **22 captures
each** on Dawn, direct D3D12 and the Dawn-free direct build. Direct runs removed
the actual selected device through `ID3D12Device5::RemoveDevice`, then recovered
four times through generation 5. They separately requested loss during shutdown.
Dawn exercised its existing real device-destroy/reacquisition path.

Verified:

- Direct color draws decreased **257 → 3**, with the largest mesh batch containing
  219 instances. Instanced and individual captures matched the verifier's strict
  pixel thresholds for color, independent shadows, HDR/4x MSAA and distinct complete
  material bindings. All triangle totals matched. This is a submission-count check,
  not a measured frame-time speedup.
- Color/data/normal BC7 representations uploaded separately with complete 8x4,
  4x2, 2x1 and 1x1 mips. Unsupported variants were skipped. The three BC7 chains
  occupied 240 payload bytes versus 516 for RGBA semantic mips (80 versus 172 per
  representation); the larger retained GUI/default texture cache was unchanged.
  Deliberately different encoded/fallback colors proved selection through pixels.
- Compression disable/re-enable returned to the expected representations and pixels.
  A malformed compressed chain rejected before direct publication; restoring the
  valid material retained the prior rendered output. Paused cached object uploads
  were zero bytes.
- Recovery retained scene identity, GUI/font pixels, requested quality, water/contact
  inputs, skin/morph poses and FX. Before/after paused captures matched, as did water
  after recovery from Running. Running/Paused state survived without simulation-time
  catch-up. Removed cameras did not become stale borrows.
- Direct pending captures cancelled with an error and produced no file. Dawn's
  existing capture behavior was kept separate. Resize, split views and shutdown
  while the device was unavailable also passed.

The paired Dawn/direct maximum mean absolute channel difference was
**0.0028038194 on a 0–255 scale**, below 0.75 for every capture. Grid, BC7/RGBA,
GUI and recovered water/animation/FX PNG previews were visually inspected.

Focused CTest passed **8/8**: render_quality, material, instancing, render_frame,
desktop_recovery, desktop_fallback, desktop_api and desktop_api_d3d12. The separate
Dawn-free recovery/API tests passed **2/2**. The injected-factory recovery test
verified retirement before replacement, backoff, destroyed-camera resolution,
retained policy, cancelled captures, ordinary-exception propagation, three-attempt
exhaustion, explicit retry, 120-frame budget reset and no acquisition after unbinding.
Unavailable-adapter/exhaustion tests used that seam; no driver crash was induced.

Fresh paired rendering regressions passed FX (102 cases), water (63), terrain (46),
animation (37), lighting/materials/shadows/MSAA and vehicle/native GUI lifecycle.
Paired maximum mean channel differences were 0.2222222222 for FX, 0.0759760200 for
water, 0.0192225477 for terrain, 0.0020182292 for animation and 0.0043636068 for lighting.
The vehicle runner separately drove input/GUI, resize, resolution budgets,
borderless fullscreen and capture lifecycle.

Reports, original framebuffer files, comparisons, binary/source hashes and previews
are under `build/evidence/d3d12-m8`; build/test logs use `build/d3d12-m8-*`.
An initial full-disk interruption was resolved with lossless NTFS compression of
generated evidence and removal of this run's incomplete browser snapshot. Failed
checks were rerun; no acceptance is inferred from the interrupted outputs.

## Browser

Configured/built/published Release using repository-local Emscripten **6.0.11**,
froze `build/d3d12-m8-browser-site`, and served that directory on port 8358.
The served Wasm matched the frozen output byte-for-byte:

`4248bcb5a23d436ec71c094cee8d4f37f8b3c4699b93d01d669b6ebdcb0f5412`

Desktop Chrome passed `render-quality-smoke`, `instancing-smoke` and
`render-fallback-smoke`, with no recorded browser errors. Coverage included real
WebGPU loss/reacquisition, three failed acquisition attempts and explicit retry,
GUI/font restoration, retained paused clocks, BC7 versus RGBA selection,
unsupported-compression fallback and shutdown with a pending adapter callback.
Forced WebGL2 exercised context restoration, quality, GUI, animation/morphs, FX and
ordered views; automatic API/adapter/device failures also selected usable fallback.

The focused instancing suite explicitly disables the now-enabled sky pass, keeping
its original mesh assertions: Knarr 46 → 11 draws, cubes 45 → 3, and exact matching
instanced/individual pixels with unchanged paused uploads. Native renderer changes
do not enter browser sources; its shared FX/resource APIs remain unchanged.

## Remaining boundaries

This establishes the recorded hardware's results, not broad GPU/driver acceptance,
long-running stress performance, physical mobile behavior or immunity to process/OS
failure. Recovery cannot preserve an application after process termination. BC7
requires authored pre-encoded variants; no runtime compression/transcoding or
compressed archive payload was added. ETC2/ASTC use RGBA fallback on D3D12. General
transparency remains sorted individual submission. Detailed direct GPU timings and
broader hardware/stability coverage remain future work.
