# Shared input validation

Validated on 2026-10-09 with the local Release WebAssembly build, installed
Google Chrome, Playwright and real WebGPU rendering. Native tests used the
existing MinGW C++20 Debug build. Changes remain in the current checkout.

## Results

| Check | Result | Evidence |
| --- | --- | --- |
| Native core suite, including new input tests | 15/15 passed | `ctest --test-dir build/native --output-on-failure --timeout 60` |
| Release WebAssembly build | Passed | `scripts/build.ps1 -Configuration Release`; published to `dist/` |
| Shared input browser acceptance | 10 groups passed; no browser errors | `build/evidence/input/result.json` |
| Existing browser rendering/import regression | 14 groups passed; no browser errors | `build/evidence/input-regression/` |
| Existing in-engine GUI acceptance | 10 groups passed | `build/evidence/input-gui-regression/` |
| Transform/upload cache regression | 5 groups passed; no browser errors | `build/evidence/input-cache-regression/cache-upload-results.json` |

`scripts/input-smoke.cjs` verifies actual rendered changes and action packets for
keyboard orbit, keyboard/mouse pan, fast reset taps, repeat suppression, wheel,
selection taps, blur cancellation, text focus, menu context, key rebinding and
browser queue overflow recovery. Chromium's touch protocol verifies one-finger
orbit, two-finger pinch/pan, stationary finger addition, two-to-one transitions,
cancellation, high-DPI picking coordinates, touch zoom buttons, and independent
virtual stick/button ownership. GUI coverage includes Unicode text and isolated
pointer/keyboard routing. Desktop/mobile screenshots are in
`build/evidence/input/desktop.png` and `build/evidence/input/mobile.png`.

Native input tests cover ordered press/release edges within one frame, independent
sources, opposing axes, displacement accumulation/expiry, multiple pointer IDs,
cancellation, malformed/overflow batch rejection, frame-rate-independent pointer
deltas, time-scaled held axes, perspective/orthographic pan and reset targets.

Input is consumed before scene update and remains responsive while scene animation
is paused. The existing direct camera exports and C++ GUI reset callback also route
through queued actions. The cache regression confirms that static scenes still
perform zero uniform writes and camera changes update only global camera/light
data. This establishes upload behavior, not an input-latency or FPS benchmark.

## Remaining device acceptance

Chromium mobile emulation is not physical Android or iOS Safari validation. Check
the deployed HTTPS build on supported target devices for pinch/pan, simultaneous
controls, background/resume, orientation changes, browser edge gestures and native
keyboard/IME behavior. The existing WebGPU startup gate still applies independently
of the cross-device input adapter.

The starter exposes Select/Move/Look/Interact actions and virtual control adapters.
It does not add object picking, a player movement system or a game-specific mobile
HUD. Applications attach those consumers/layouts to the shared input API.
