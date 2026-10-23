# Direct D3D12 milestone 1 validation

Historical milestone 1 record. Current static rendering scope and new checks are
in [milestone 2 validation](windows-d3d12-milestone2-validation.md).

Validated on 2026-10-22 using Windows x64 MSVC Release, Visual Studio 18 2026,
Windows SDK 10.0.26100.0, NVIDIA RTX 5060 Ti (hardware D3D12). This record covers
the [selectable backend milestone](windows-d3d12.md), not full renderer parity.

## Native Windows

`build/windows` was built with both native backend options ON. The opt-in
`scripts/windows-backend-smoke.ps1` ran separately for Dawn and direct D3D12 with
`WEBENGINE_D3D12_VALIDATION=1`. Evidence is under
`build/evidence/d3d12-release-final/{dawn,d3d12}/`: report/verified JSON, binary SHA256
identities, PPM captures and PNG previews. Direct D3D12 reported the hardware
adapter, `desktopBackend=direct-d3d12` and validation enabled, without debug layer
ERROR/CORRUPTION messages. Dawn reported WebGPU/D3D12 and its existing capabilities.

Each native path drove the actual shared vehicle beyond 2 m/s, accepted SDL text
and mouse input in the retained GUI, resized to 960x540, applied resolution scale
.75 to produce 720x405, entered borderless fullscreen at 3440x1440 and restored
windowed rendering. Tests inspected the actual HWND style, completed framebuffer
readbacks including GUI, rejected duplicate captures and cancelled queued captures
at shutdown. Paused simulation remained unchanged. Captures were checked for
dimensions, colored world pixels and GUI changes; direct vehicle/menu previews
were visually inspected. This is local actual native-window evidence with
synthetic input, not physical keyboard/controller/human acceptance.

Focused CTest coverage runs `desktop_api`, `desktop_api_d3d12`, `render_quality`
and `adaptive_quality`. The same public consumer exercises scene transition/unload,
camera removal, invalid view/quality candidates, pause/resume, callback exceptions,
reentry rejection and close on both backends without platform/private includes.
Direct-specific coverage rejects point-light scene/view content, removes the
unsupported source and verifies the valid renderer remains usable.

An additional configuration set `WEBENGINE_WINDOWS_DAWN=OFF` and
`WEBENGINE_WINDOWS_D3D12=ON`. Its actual direct renderer acceptance passed with
debug validation. `engine_desktop.vcxproj` referenced `engine_renderer_d3d12` and
had no Dawn/engine_renderer_windows project dependency. Evidence and a retained
executable are in `build/evidence/direct-only-backend/`; the normal dual-backend
configuration was restored afterward. This establishes optional linkage, not an
uninstalled-SDK or clean-machine distribution test. The setup script still stages
Dawn alongside other SDKs. The direct-only run predates the final teardown guard
and expanded API test; the final dual-backend run covers those source changes.

## Browser

Fresh Release `webengine` and `web_assets` were built with repository-local
Emscripten 6.0.11 and frozen from dist into `build/d3d12-browser-site`. TEMP/TMP
used the workspace-owned `build/d3d12-emcc-temp`. The served Wasm SHA256 was
`EA33075374476F5AEFD8F3C0F52FA58D1090AF3E54AD76D2DDB08AC3F319EC9F`.
Chrome 155.0.8059.85 ran:

- `scripts/vehicle-smoke.cjs`: explicitly selected WebGPU and WebGL2; driving,
  wheel rendering, masks/split views/MSAA simulation invariance, save/restore,
  resize/recovery, editor tuning/undo/redo, isolated Play and blur cancellation.
- `scripts/gui-smoke.cjs`: 10 checks each on explicit WebGPU and WebGL2, including
  Unicode selection/editing, native browser events, GUI capture/focus and pixels.

Evidence: `build/evidence/d3d12-final/browser-vehicle/results.json` and
`browser-gui-webgpu/results.json`, `browser-gui-webgl2/results.json`, with adjacent
screenshots. All passed with no captured browser errors. Existing Emscripten
EM_ASM dollar-identifier warnings were present during the successful build.

## Boundaries

No sustained performance or Dawn/direct speed comparison was performed. No
second GPU/vendor, WARP, physical mobile, exclusive fullscreen, HDR display,
multi-monitor/DPI move matrix, device-loss recovery or clean-machine installer
acceptance is established for direct D3D12. Existing broad Windows graphics
acceptance remains Dawn-only. Advanced lighting/materials, skin/morph, terrain
layers, water, FX, profiling and recovery are later direct-backend milestones;
their existing browser/Dawn implementation is not evidence of direct support.
