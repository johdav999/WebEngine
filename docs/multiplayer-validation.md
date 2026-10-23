# Multiplayer validation

The focused protocol suite runs the same sources as native C++ and Node/Wasm:

```powershell
build/windows/Release/network_tests.exe
build/windows/Release/multiplayer_arena_tests.exe
./.tools/emsdk/node/24.19.0_64bit/node.exe build/web-release/network_tests.js
./.tools/emsdk/node/24.19.0_64bit/node.exe build/web-release/multiplayer_arena_tests.js
```

It covers strict binary parsing/truncation/versioning, complete-snapshot atomicity,
duplicate IDs, payload budgets, ownership, sequence rejection, command queue limits,
disconnect cancellation and reconnect identity changes.

The arena suite independently checks fixed-step movement, diagonal normalization,
world bounds, stale-input expiry, interaction distance/cooldown and handle-safe
despawn. The smoke driver also runs `network_transport_tests` against its actual
gateway, covering native handshake/snapshots, remote close, reconnect, send budgets
and cancellation during connection setup.

`scripts/multiplayer-smoke.cjs` requires Playwright, pngjs, Chrome and the pinned
gateway `ws` dependency. It starts/stops its own loopback server and HTTP server,
opens the real browser editor and clicks its multiplayer launcher, then opens the
native editor and activates its retained GUI launcher. Each client uses the same
C++ session/protocol implementation. Browser pairs are WebGPU/Windows Dawn and
WebGL2/Windows direct D3D12.

```powershell
$env:MULTIPLAYER_WEB_BUILD='build/web-release/site'
$env:MULTIPLAYER_NATIVE_BUILD='build/windows/Release'
node scripts/multiplayer-smoke.cjs
```

For the generated package, set both `MULTIPLAYER_WEB_BUILD` and
`MULTIPLAYER_HOST_SOURCE` to `dist`; set `EVIDENCE_DIR` to a separate output folder.
The ordinary driver uses current `web/` host sources with generated Wasm; the
package run reads every browser file from the generated distribution.

The driver checks authoritative movement and beacon interaction observed by both
clients, complete late-join state, invalid endpoint preservation, malicious ownership rejection, browser reconnect
with fresh identity, remote despawn, editor document retention and server shutdown.
It records browser PNGs, native client/editor PPM captures, native/editor JSON,
driver JSON and process logs under `build/evidence/multiplayer` (override with
`EVIDENCE_DIR`). Native host initialization is allowed to finish independently of
the ten-second connected-client measurement window; startup is not a performance
acceptance claim.

Desktop browsers do not establish physical Android/iOS acceptance. These tests establish local loopback
behavior, not internet latency, production load, TLS deployment or cheat resistance.

## Verified 2026-10-22

Final packaged run: `build/evidence/multiplayer-final/report.json`. Chrome 155.0.8059.85 on Windows; native Release builds and repository-local Emscripten 6.0.11. All browser files were served from the freshly generated `dist` package.

| Runtime | Verified result |
| --- | --- |
| Native protocol and arena | Both focused CTest suites passed. |
| Node/Wasm protocol and arena | Both matching executable suites passed. |
| Headless C++ server + Node gateway | Admitted mixed clients, advanced authority, rejected forged ownership, supported fresh joins and exited after gateway shutdown. |
| Native WinHTTP transport | Handshake/snapshots, remote closure, three reconnects, oversized-send rejection and three pending-connect cancellations passed. |
| Browser webgpu + Windows dawn | Mixed authoritative movement and interaction, late join, reconnect/despawn, invalid endpoint preservation, editor launchers and retained source documents passed; 1864 browser pixels changed in the movement capture. |
| Browser webgl2 + Windows d3d12 | Mixed authoritative movement and interaction, late join, reconnect/despawn, invalid endpoint preservation, editor launchers and retained source documents passed; 1554 browser pixels changed in the movement capture. |

Both native editors and clients produced framebuffer captures. Browser clients used keyboard/UI input; native client movement/interaction was scripted through the shared session, while editor launch used the actual retained GUI action. Native physical-key/controller input was not separately injected in this suite.

Server executable dependency inspection showed only Windows/C++ runtime libraries, with no SDL, Dawn or D3D12 dependency. `artifact-hashes.json` beside the report identifies the delivered client/server binaries and network sources. Earlier source-host runs are retained separately under `build/evidence/multiplayer`; the final package report is the handoff evidence.

Initial browser launcher ordering and native startup timeouts were corrected before these passing runs. Native host initialization was slow on this machine; no startup-latency or sustained-performance acceptance is claimed.
