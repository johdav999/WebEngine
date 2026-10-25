# Library L1 — release stabilization

Work in progress against `227b08d`. API 1.7.0 and wire 1 remain unchanged.
This milestone addresses the two R10 rendered CI failures; no RPGGame pin changes.

Direct D3D12 now checks adapter-enumeration HRESULTs and returned interfaces before
use. `WEBENGINE_D3D12_WARP=1` explicitly selects Microsoft's software adapter for
diagnostic/CI rendering. It is not a fallback: ordinary launches still require
hardware. Adapter JSON reports `software:true` for WARP. Recovery recreates the
selected mode; do not change this environment setting during a host's lifetime.

Software browser tests disable Chromium's GPU watchdog while retaining the test's
five-minute overall deadline, pixel checks and complete input/save/load scenario.
The harness records uncaptured GPU errors and device-loss reasons to expose the
first failure. Hardware runs retain normal watchdog settings.

Native WARP and browser SwiftShader checks establish software-renderer acceptance,
not physical GPU or performance coverage. Local hardware checks remain separate.
Results and the final release gate decision will be recorded after validation.
