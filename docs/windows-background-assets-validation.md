# Windows background assets validation - 2026-10-21

Validated in the shared working checkout; unrelated changes were preserved.
Native: MSVC Release, `build/windows-services`, SDL3 3.4.18, Dawn D3D12 on NVIDIA
GeForce RTX 5060 Ti (driver 32.0.16.1088). Browser: repository-local Emscripten
6.0.11 Release, `build/web-assets-release`, Chrome 155.0.8059.85.

| Check | Actual result |
| --- | --- |
| Focused native CTest | 9/9 passed: mobile_assets, assets, application, desktop_assets, desktop_services, desktop_persistence_write/read, desktop_demo_write/read |
| Native background executor | Real FBX/GLB/image/bundle decode, skin preservation, Unicode/space paths, shared resources, owner publication and eviction passed |
| Controlled asynchronous lifecycle | 120 owner updates during a held off-thread decoder, source-byte progress while Loading, per-consumer/active/queued cancellation, queue overflow, timeout with occupied slot, fresh retry and late-result rejection passed |
| Native error/lifetime checks | Missing/corrupt/oversized source, decoded result budget, invalid configuration, null/throwing decoder, expired service, manager teardown and joined service teardown passed |
| Real Windows vehicle | GLB preview adopted from background load; driving, menu, GUI cancel, failed-import preservation and actual Dawn framebuffer verification passed |
| Browser HTTP/Wasm assets | Eight check groups passed, no page errors, actual server-observed HTTP aborts; shared AssetDecodeOptions overload exercised |
| Browser rendered resources | Six scene/backend combinations passed: static Knarr, skeleton and morph on WebGPU and WebGL2; upload admission, deferred draws, recovery and identical final paused pixels |

Commands used:

```powershell
cmake -S . -B build/windows-services -DWEBENGINE_BUILD_WINDOWS=ON -DBUILD_TESTING=ON
cmake --build build/windows-services --config Release --target desktop_asset_tests webengine_windows_smoke desktop_demo_services_tests mobile_asset_tests asset_tests application_tests --parallel 6
ctest --test-dir build/windows-services -C Release -R '^(desktop_assets|assets|mobile_assets|application|desktop_services|desktop_persistence_write|desktop_persistence_read|desktop_demo_write|desktop_demo_read)$' --output-on-failure
scripts/windows-smoke.ps1 -BuildDir build/windows-services -Model tests/fixtures/skeletal_textured.glb -EvidenceDir build/evidence/background-assets-windows-final

.tools/emsdk/upstream/emscripten/emcmake.exe cmake -S . -B build/web-assets-release -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/web-assets-release --target asset_browser_tests webengine --parallel 6
# Set NODE_PATH to the test dependencies and BROWSER_EXECUTABLE to installed Chrome.
$env:ASSET_TEST_SITE='build/web-assets-release/asset-tests'
$env:EVIDENCE_DIR='build/evidence/background-assets-browser-current'
node scripts/assets-smoke.cjs
$env:MOBILE_ASSET_BUILD='build/web-assets-release'
$env:EVIDENCE_DIR='build/evidence/background-assets-render'
node scripts/mobile-assets-render-smoke.cjs
```

For Emscripten linking, TEMP/TMP/EMCC_TEMP_DIR were set to the workspace-local
`build/background-assets-emcc-temp` after the sandbox's default temporary directory
denied linker output. Native configuration/build/tests required normal Windows SDK
registry access. These are tooling permissions, not runtime feature failures.

Evidence:

- [Native CTest log](../build/background-assets-native-final.log).
- [Final Windows vehicle report](../build/evidence/background-assets-windows-final/report.json), PPM readbacks and binary hashes in that directory.
- [Final browser HTTP/worker report](../build/evidence/background-assets-browser-current/results.json).
- [WebGPU/WebGL2 render report](../build/evidence/background-assets-render/results.json) and before/after PNGs in that directory.

An intermediate demo persistence read failed because the test clicked Play sound
before the newly asynchronous WAV had published. The demo now disables that action
until readiness, and the regression test awaits the asset's terminal state. Both
final demo persistence tests passed. This was fixed without restoring synchronous
WAV loading.

Timeout tests use a deliberately held native decoder and an actual watchdog; they
do not emulate forced thread termination. Cancellation/timeout prevents publication
but a running importer or blocked OS read can retain a worker slot until return;
executor destruction joins it. No hard execution deadline, hostile hung-importer
isolation, slow/network disk benchmark, total-memory stress, GPU prewarming,
physical mobile/controller or broad clean-machine/GPU acceptance is established.
See [API and ownership contracts](windows-background-assets.md).
