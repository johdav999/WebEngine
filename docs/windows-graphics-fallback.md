# Windows graphics fallback

Windows now defaults to `DesktopBackend::Automatic`. In a Dawn-enabled build,
the bounded candidate order is:

1. Dawn using D3D12 (`DesktopBackend::Dawn`, CLI `dawn`).
2. Dawn using Vulkan (`DawnVulkan`, CLI `vulkan`).
3. Dawn using D3D11 (`DawnD3D11`, CLI `d3d11`).

All three use the existing shared C++ WebGPU renderer, WGSL, Scene resources,
retained GUI and Application. This is an alternate native graphics API path;
there is no second engine or browser embedded in the desktop window. The browser
retains its existing Automatic WebGPU → WebGL2 renderer fallback.

```powershell
./scripts/build-windows.ps1
./build/windows/Release/webengine_windows.exe --backend auto
./build/windows/Release/webengine_windows.exe --backend vulkan
./build/windows/Release/webengine_windows.exe --backend d3d11
```

The vehicle defaults to `auto`; applications get the same policy through default
`DesktopSettings`. Explicit `dawn`, `vulkan`, `d3d11` and `d3d12` selections remain
strict, so testing or a known application requirement cannot silently select a
different API. The separate direct-D3D12 implementation remains explicitly
selectable and keeps its own feature contract. A build with Dawn compiled out
uses direct D3D12 as its sole Automatic candidate; it has no alternate API fallback.
Direct D3D12 now performs bounded same-backend device recovery before reporting a
terminal error; see [direct recovery](windows-d3d12-recovery-performance.md).

## Recovery and ownership

Automatic mode handles synchronous backend creation failures and terminal errors
reported during idle graphics service. Dawn's existing bounded same-device/API
recovery runs first; only a terminal error advances to the next API. Acquisition
retains the shared renderer's existing timeout and late-callback invalidation.
Candidates are tried at most once until an explicit retry. Content validation and
application callback exceptions continue to propagate; they are not a reason to
hide invalid data by trying another renderer.

The private `FallbackGraphics` owner destroys the retired renderer/device/surface
before constructing a replacement for the same SDL HWND. It retains scene-scoped
camera handles and resolves them again; removed cameras cannot become stale raw
borrows during recovery. Application still owns the same source scene. GUI state,
render quality, shadow policy and scene-scoped water interaction inputs remain
available to the new renderer. Scene replacement clears water inputs. Pending
framebuffer captures cancel with an error; start a new capture after recovery.

The host cancels held inputs and resets clock baselines when the backend changes,
and after an interval waiting for graphics. Elapsed clock totals, explicit pause,
scene handles and simulation state remain intact. There is no outage-time catch-up
or second simulation step. Public GUI/window/scene APIs keep their existing owners.
Do not retain a `capabilities()` reference across a step that may replace graphics;
query the active capabilities again at the next owner-thread boundary.

`backend()` reports the current candidate, including while it initializes.
`diagnostics().ready` establishes whether it is usable. Diagnostics expose
`requestedBackend`, `backend`, and `graphicsAttempts` (backend plus retained error).
Automatic `renderState` JSON includes `desktopBackend`, `graphicsAttempts` and the
actual adapter API/vendor/device/software flag. Presentation frame counts remain
monotonic across replacement; per-device GPU caches/performance samples restart.
`error` stays empty when an alternate backend succeeds. Exhaustion returns a
combined terminal error naming each failed candidate. `retryGraphics()` then
starts a fresh bounded chain from the preferred backend.

Vulkan requires a usable installed Vulkan loader/driver; D3D11 requires the
Windows D3D11/FXC runtime and an adapter meeting the shared renderer's requested
limits. No Vulkan SDK, ANGLE, SwiftShader download or mandatory software renderer
is added. The pinned Dawn loader requires normalized absolute runtime directories.
The native adapter passes the trusted Windows system directory, including its
trailing separator, instead of changing global DLL search paths or searching the
working directory. See the pinned [Dawn loader](https://github.com/google/dawn/blob/67d4d6e64b0755d638f76549794692badfdbc8ab/src/dawn/common/DynamicLib.cpp)
and [runtime search paths](https://github.com/google/dawn/blob/67d4d6e64b0755d638f76549794692badfdbc8ab/src/dawn/native/Instance.cpp).

## Diagnostics and acceptance

Opt-in tools may set `allowGraphicsFaultInjection=true` and call
`simulateGraphicsFailure()` at an idle boundary. It queues a diagnostic terminal
failure of a ready Automatic candidate; replacement occurs at the next service
boundary. It does not reset the OS/driver. Explicit backends return false.
`simulateGraphicsLoss()` remains the separate real selected-device loss/recovery
exercise (Dawn destroy or direct D3D12 removal when supported). Both require opt-in and reject calls from a host frame callback.

Run the focused native policy test, public host tests and real gallery:

```powershell
cmake --build build/windows --config Release --target desktop_fallback_tests desktop_api_tests windows_fallback_smoke --parallel 6
ctest --test-dir build/windows -C Release -R '^(desktop_fallback|desktop_api|desktop_api_automatic|desktop_api_vulkan|desktop_api_d3d11)$' --output-on-failure
./build/windows/Release/windows_fallback_smoke.exe build/evidence/windows-fallback
python tools/verify_windows_fallback.py build/evidence/windows-fallback
./scripts/windows-backend-smoke.ps1 -Backend Vulkan
./scripts/windows-backend-smoke.ps1 -Backend D3D11
```

Missing optional runtimes make explicit API tests fail; they are not silently
skipped. See [measured validation and hardware limits](windows-graphics-fallback-validation.md).
