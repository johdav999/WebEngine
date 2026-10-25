# WebEngine

## Procedural planet flight

The optional [planet stage 1 demo](docs/planet-terrain.md) flies continuously from
space to a procedural cube-sphere surface on browser WebGPU/WebGL2 and native
Windows. It combines double-precision planet positions, camera-relative meshes,
bounded quadtree refinement, parent-surface morphs and visual seam skirts. Enable
`WEBENGINE_BUILD_PLANET_DEMO=ON`, build `planet_demo` for the browser or
`webengine_planet_windows` for Windows, and use **Descend to surface** or manual
WASD/right-drag flight. Generated browser files live in `build/web-release/planet-site`.
Stage 2 adds [stable source collision, radial gravity, landing, surface walking,
jumping and takeoff](docs/planet-physics.md). Visual LOD never changes collision.
Atmosphere, oceans and persistence remain future stages. See
[stage 2 validation](docs/planet-stage2-validation.md) and the
[stage 1 record](docs/planet-validation.md).

## Multiplayer development milestone

Optional `WebEngine::Network` now supplies a shared C++ session protocol with browser
and Windows WebSocket adapters. The first [multiplayer milestone](docs/multiplayer.md)
includes a headless authoritative arena server, browser/Windows clients and isolated
test launchers in both editors. Enable `WEBENGINE_BUILD_MULTIPLAYER_DEMO=ON` in both
builds, install the pinned gateway dependency, start the server, and use **Multiplayer
test** in the editor. The launcher uses the fixed arena. See [validation](docs/multiplayer-validation.md).

[Milestone 2](docs/multiplayer-scenes.md) adds **Host selected actor** and
**Join authored scene** in both editors, a scene server and browser/Windows
clients. It transfers a bounded static mesh/camera/light scene and replicates
server-owned player instances using the selected actor's authored visuals.
Source documents remain unchanged. See [M2 validation](docs/multiplayer-scenes-validation.md).

[Milestone 7](docs/multiplayer-components.md) adds registered gameplay-component
state replication with shared editor factories/properties, Everyone/Owner only
audiences, staged validation and custom save/load/duplication. Browser and native
Windows clients apply server state to real components. The example includes Vitals
and Progress; games register their own types. M7 introduced MP07/WES3;
the current M8–M10 scene schema is MP09 with WES4 manifests.
See [M7 runtime evidence](docs/multiplayer-components-validation.md).

[Runtime prefabs and gameplay RPCs/events](docs/multiplayer-runtime.md) add
server-controlled spawning/despawning, editor template/permission authoring and
bounded invocation registries. [Authoritative physics](docs/multiplayer-physics.md)
runs rigid bodies and collisions on the server while both clients render passive
replicas. [Guest lobbies](docs/multiplayer-lobbies.md) add invitations, rosters,
server-issued tokens and recovery of the same player/owned objects after temporary
connection loss. These work through both editor launchers. Recovery is in memory
for the running service; the sample player keeps its kinematic controller.
See [current browser/Windows validation](docs/multiplayer-runtime-validation.md).

## Native Windows desktop host

Native retained GUI now supports Windows UI Automation (Invoke, Toggle, Value and
RangeValue), plus explicit SDL touch/pen contacts with pressure, DPI conversion
and capture cancellation on both native renderers. Browser semantic accessibility
and multi-touch remain available on WebGPU/WebGL2. See
[accessibility/input contracts](docs/accessibility-input.md) and
[validation](docs/accessibility-input-validation.md).

Build an offline-installable ZIP with `./scripts/package-windows.ps1` after the
Windows build. It includes a dependency-checking launcher, app-local runtimes,
per-user installer and support logs. See [distribution](docs/windows-distribution.md)
and [recorded validation](docs/windows-distribution-validation.md); clean-machine
and publisher-signing acceptance remain separate gates.

The optional [broader Windows acceptance runner](docs/windows-acceptance.md)
checks animation, terrain, water, particles, shadows, HDR post and GPU recovery
on Dawn, with framebuffer comparisons and explicit hardware coverage gaps.

The SDL3 desktop executable offers selectable Dawn and direct D3D12 backends.
Dawn shares the browser WebGPU renderer/shaders; direct D3D12 runs the vehicle
and GUI with resize/fullscreen/captures, plus static PBR materials, IBL, point/spot
lights, cascaded/spot shadows, hardware-checked 4x MSAA, sky/fog and HDR bloom/color
grading, GPU skeletal skinning, cached morph targets and layered terrain with
normal/height blending and triplanar projection, plus reflective animated water,
refraction/foam, underwater fog/caustics and contact wakes, plus billboard/plane FX,
flipbooks, ribbons/surface trails, mesh particles and soft depth fading. Milestone 8
adds general mesh instancing, semantic BC7 uploads and bounded device recovery.
See [recovery and performance contracts](docs/windows-d3d12-recovery-performance.md)
and [validation](docs/windows-d3d12-milestone8-validation.md).
Launch `./build/windows/Release/webengine_lighting_windows.exe --backend d3d12`
for a native gallery with interactive material, lighting, sky, fog and post controls.
Launch `./build/windows/Release/webengine_animation_windows.exe --backend d3d12`
for imported-rig playback/blending and independent morph controls. See
[animated rendering](docs/windows-animation-rendering.md) and
[milestone 4 validation](docs/windows-d3d12-milestone4-validation.md).
Launch `./build/windows/Release/webengine_terrain_windows.exe --backend d3d12`
for layer/relief/projection controls and independent camera/shadow detail. See
[terrain contracts](docs/terrain.md) and [milestone 5 validation](docs/windows-d3d12-milestone5-validation.md).
Launch `./build/windows/Release/webengine_water_windows.exe --backend d3d12`
for water/underwater controls and moving-object wakes. See [water](docs/water.md)
and [milestone 6 validation](docs/windows-d3d12-milestone6-validation.md).
Launch `./build/windows/Release/webengine_fx_windows.exe --backend d3d12`
for native particle/trail presets and fog/HDR/MSAA controls. See
[FX rendering](docs/windows-fx-rendering.md) and [milestone 7 validation](docs/windows-d3d12-milestone7-validation.md).
Launch with `--backend auto` (default): Dawn D3D12 → Vulkan → D3D11.
Explicit `dawn`, `vulkan`, `d3d11` and `d3d12` choices remain strict.
See [automatic fallback, diagnostics and recovery](docs/windows-graphics-fallback.md).
See [backend scope, build options and captures](docs/windows-d3d12.md) and
[separate native/browser evidence](docs/windows-d3d12-validation.md).
Run `./scripts/setup-windows.ps1`, then
`./scripts/build-windows.ps1`, and launch `./build/windows/Release/webengine_windows.exe`.
Requires Windows x64, Visual Studio C++ tools, CMake and Python 3.12+.
WASD/arrows drive; Space brakes; Shift handbrakes; R resets; P/Escape opens the
interactive pause/settings menu. Click controls or use Tab/Enter/arrows. The shared
GUI supports native Unicode text entry/selection and Ctrl+A/C/X/V clipboard
shortcuts. Edit the local driver name in Settings; use controller D-pad/A/B/Start
for menu navigation. See [desktop GUI acceptance](docs/windows-desktop-gui-validation.md).
Applications link `WebEngine::Desktop` and include `engine/desktop.hpp`; see the
[public desktop API](docs/windows-desktop.md).
Optional [Windows platform services](docs/windows-platform-services.md) add SDL
audio playback, asset-directory loaders, user saves/settings and native open/save
dialogs. The vehicle menu previews WAV sounds, opens content and saves/restores
checkpoints and settings across launches, using the shared audio/archive contracts.
See [build, controls and scope](docs/windows-native.md) and
[validation](docs/windows-native-validation.md). Browser builds remain unchanged;
`WEBENGINE_BUILD_WINDOWS` is OFF by default.

## Native Windows editor and environmental audio

Run `./scripts/build-windows.ps1`, then
`./build/windows/Release/webengine_editor.exe [scene.wescene]` for the native scene
workbench: Outline/Details, atomic transforms, component properties, quick creation,
GLB import, undo/redo, native files and isolated Play. Browser/native scene files
interoperate. Specialized graphical panels remain browser-only; see
[scope and API](docs/windows-editor.md).

Windows AudioSystem now supports local encoded music streaming, wall low-pass
filtering and prepared room reverb. The vehicle settings expose streaming, seek
and room controls. Native WAV/MP3 acceptance and browser WebGPU/WebGL2 checks are
separate; see [audio contracts](docs/windows-audio-environment.md) and
[parity validation](docs/windows-parity-validation.md).

## Formula 1 driving demo

Open **`?scene=formula`** for a closed circuit through rolling terrain, using the
supplied Formula car model. Drive with **WASD / arrow keys**, **Space** to brake,
**Shift** for handbrake, and **R** to reset. Complete the circuit to start the next
timed lap. Click the track and move the mouse left/right to steer, or use the
Mouse steering toggle to switch it off. The cursor centre straightens the wheels.
Includes a chase camera, minimap, best lap, touch controls and gamepad
mapping. The default easy preset adds smoother speed-sensitive steering, more
grip and gentler acceleration. Build/serve below; see [Formula demo](docs/formula-demo.md)
for the handling parameters and details.

Optional chunked heightfield terrain supports float samples or image heightmaps,
four-layer materials with normal maps, height blending and optional triplanar projection,
seamless chunk LOD with independent shadow detail,
sculpt/paint brushes, affected-chunk rebuilding, bounded derived-chunk streaming,
source paging with asynchronous worker chunk generation and bounded resident data,
cross-page sculpt/paint with atomic dirty-page writeback,
static collision, ground/ray queries and built-in scene saves.
Open `?scene=terrain` for the browser demo; see [Terrain](docs/terrain.md).

Hybrid terrain adds authored mesh cave/overhang sections alongside
heightfields, with cell holes shared by rendering, collision and ground queries.
Milestone 2 adds interactive hole/fill brushes, editable cave width/height/depth,
and grouped undo/redo for the opening and mesh on browser and Windows.
Milestone 3 adds bounded automatic seam preparation, matching boundary samples,
and normal/material blending, with a close-up view and blend comparison controls.
Milestone 4 adds hole-aware heightfield LOD and cave interior simplification that
locks seams and blend regions. Camera/shadow detail never changes source collision.
Milestone 5 saves explicit LOD recipes and automatically regenerates them on load.
Both demo hosts support durable checkpoints and live scene replacement, retaining
holes, seam blending, collision and editable entrance settings. The current demo
uses a continuous indexed limestone cave with a rounded arch, varying tunnel cross-sections,
three spacious chambers at different horizontal positions and 0/−3/−8 m elevation,
narrow winding tunnels and fine stone relief.
The Lower Grotto now has roughly ten times its previous volume (2.154× width,
height and length) and a sculpted rock basin beside its dry walking route.
Use **Lower grotto** for the room or **Grotto basin** for the bowl close-up.
Mesh-contact boundary loops can also derive heightfield holes automatically;
see [mesh-shaped terrain cuts](docs/terrain-mesh-holes.md).
Right-drag to look, WASD to fly, Q/E to descend/ascend, Shift to boost, and Escape
to release camera controls. Click the browser scene to give it keyboard focus.
Build the optional `hybrid_terrain_demo` browser or `webengine_hybrid_terrain_windows`
example for the same walkable cave on both targets. See [hybrid terrain](docs/hybrid-terrain.md)
for APIs, limitations, launch commands and validation.
Reusable terrain-mesh formation preparation adds smoothly joined stalagmites and
stalactites with direction, length, radius, bend and seed controls, protected tips
in render LOD, full-base collision and existing archive persistence. Both cave
demo hosts use it. See [terrain mesh formations](docs/terrain-mesh-formations.md).
Terrain-mesh lakes add smooth basins, fitted water surfaces and finite water regions
for cave ponds, with collision, LOD and saved state. See [lake contracts](docs/terrain-mesh-lakes.md).



Versioned application snapshots, custom component codecs, stable saved identities
and IndexedDB/localStorage byte adapters build on the scene archives. The inspector
offers an explicit browser checkpoint under **Scenes & assets → Browser saves**.
See [Persistence and browser storage](docs/persistence.md).

Asset identifiers, asynchronous loading, caching, progress, cancellation, unloading
and preparation before scene activation are available through `AssetManager`,
`AssetPreparation` and `Application::prepareScene`. See [asset resource contracts](docs/assets.md).
Windows also supplies an explicitly owned background file/decode executor with
bounded workers/queue, progress, timeout and cancellation using the same asset
requests and preparation API. The vehicle menu imports models asynchronously and
can cancel an import while retaining its previous preview. See
[Windows background assets](docs/windows-background-assets.md).

Optional isolated decode workers, GPU upload admission and KTX2/fallback packaging
are documented in [Mobile asset pipeline](docs/mobile-assets.md).

Reusable object definitions are available through `Prefab`: instantiate configured
actor/component hierarchies with shared assets, per-instance overrides and
application-registered component factories. See [prefab usage and contracts](docs/prefabs.md).
The [prefab asset editor](docs/prefab-editor.md) saves `.weprefab` definitions,
creates variants and updates linked instances while preserving local overrides.
Try `?scene=prefabs&editor=1`.

Selection and interaction is available through `InteractionComponent` and
`Scene::selection()`: bounds or precise triangle picking, optional visible-mesh
occlusion, action eligibility/execution, and ordered selection/focus events.
`ManipulationController` adds plane/axis dragging and pivot rotation with explicit
commit/cancel. See [the API and integration guide](docs/selection.md).

Application start/pause/resume/shutdown, scene ownership and transitions, and
input/simulation/presentation clock contracts are documented in
[Application and scene lifecycle](docs/application-lifecycle.md).

A C++20 browser 3D engine starter, compiled to WebAssembly with Emscripten. The C++ renderer prefers the WebGPU C++ API through [Emdawnwebgpu](https://emscripten.org/docs/porting/multimedia_and_graphics/WebGPU-support.html) and automatically falls back to WebGL 2 when WebGPU is unavailable or initial adapter/device acquisition fails; JavaScript hosts the canvas and forwards input.

The demo automatically loads `tests/fixtures/Knarr.fbx` through the FBX importer, including its seven embedded base-color textures and material assignments. It renders with a ground grid, a perspective orbit camera, a directional light, a moving point light, and overlay axes. Pause motion, switch lights, drag to orbit, scroll to zoom, or reset the camera.

The browser build copies the Knarr asset to `dist/assets/Knarr.fbx`, which the page fetches at startup. Use **Import FBX / GLB** to select one local binary or ASCII `.fbx` file or binary glTF 2.0 `.glb` file together with its companion images. Embedded images load automatically; external references resolve against selected images by filename, case-insensitively. Missing images produce an inspector warning and use the material color. **Override texture** applies an image to every material of the imported mesh. Files stay local; no file upload is involved. **Restore demo** returns to the original cube scene. Reload the page to load Knarr again.

## Shared Windows and browser authoring

Run `build/windows/Release/webengine_editor.exe` after `scripts/build-windows.ps1`
and choose **Authoring tools**. Terrain, animation, AI, prefab, FX and cinematic
forms use the same optional `engine_editor_gui` panel available through **Shared
authoring tools** in the browser editor. Standard asset files, independent document
history and scene assignments remain shared. See [workflows and remaining advanced
UI differences](docs/editor-authoring.md) and [validation](docs/editor-authoring-validation.md).

## Scene editor

Choose **Scene editor** or open `?editor=1` for local scene files, searchable
hierarchy/selection, transform gizmos and undo/redo. Content authoring adds GLB/FBX,
image and asset-library import, ground-plane placement, typed component inspectors,
per-slot material/texture editing, hierarchy duplication, grid/angle snapping, and
debounced IndexedDB autosave with explicit recovery. Edit mode pauses simulation
while retaining camera navigation. The structure tools add supported component
creation/removal, KeepWorld reparenting, modifier/range multi-selection, grouped
history and local-axis/uniform scale gizmos. **Play / Stop** runs an isolated built-in
runtime and returns to the same document/history. Portable `engine_editor` commands own scene edits;
HTML/SVG and browser byte adapters supply the tool interface.
The full-window workspace provides dockable/resizable Scene, Scene Outline,
Details, Quick Add and Assets panels. Drag titles to dock or float, use Window to
restore panels/reset the saved layout, and use Quick Add for primitive placement
or component shortcuts. Narrow screens stack the same panels.
In Scene, right-drag looks around; hold right mouse with WASD to fly, Q/E to move
down/up and Shift to accelerate. Middle-drag pans, Alt-drag orbits and scroll zooms.
Double-click an outline node (or press F) to frame it and set the orbit pivot.
Use **Quick Add → Terrain** to create a collidable terrain. **Details → Terrain**
provides Raise/Lower, Flatten, Smooth and four-layer Paint brushes with radius,
strength and whole-stroke undo. Creation also accepts an imported heightmap image.
Import images in **Assets**, then assign or clear each surface texture in
**Details → Terrain → Paint layer textures**, with undo/redo and scene persistence.
See [scene editor controls, APIs and scope](docs/scene-editor.md).

## Run on Windows

Prerequisites: Git, Python 3, CMake 3.22+, and Ninja on PATH. Use PowerShell from this directory:

```powershell
./scripts/setup.ps1                  # Install pinned Emscripten 6.0.11 in .tools/emsdk
./scripts/build.ps1                  # Debug WebAssembly build
./scripts/serve.ps1                  # http://localhost:8080; Ctrl+C to stop
```

Open [the demo](http://localhost:8080) in a browser supporting WebGPU or WebGL 2. WebGPU requires HTTPS or localhost; automatic mode can use WebGL 2 without WebGPU. The status and capability panel identify the selected backend. Use `?renderer=webgl2` to force fallback or `?renderer=webgpu` to require WebGPU. The fallback supports textured scenes, animation, particles, ordered camera views and the canvas GUI with basic lighting; full PBR, instancing and GPU timing remain WebGPU features; directional/spot shadows are supported on both backends. See [rendering fallback contracts and validation](docs/rendering-fallback.md).

The setup script does not install global SDK environment settings. `.tools`, `build`, and `dist` are ignored by Git. First setup downloads the compiler toolchain; the first browser build also downloads the SDK-pinned Dawn port.

```powershell
./scripts/build.ps1 -Configuration Release
./scripts/serve.ps1 -Port 8081
```

`serve.ps1` tries up to 100 consecutive ports starting at `-Port` (8080 by
default), skipping occupied or access-denied ports. Open the actual URL printed
in the terminal. Use `-PortAttempts 1` when a fixed port is required; the search
never goes beyond port 65535. Direct `serve.py` calls remain fixed-port unless
`--port-attempts` is supplied.

Both configurations have their own build outputs. Each build publishes its selected output plus the shell into `dist/`. Deploy that entire directory, including `webengine.js` and `webengine.wasm`, with `application/wasm` as the Wasm MIME type. This starter uses one browser thread and needs no cross-origin isolation headers.

## Run on Linux or macOS

Install Git, Python 3, CMake, and Ninja, then:

```sh
git clone --depth 1 https://github.com/emscripten-core/emsdk.git .tools/emsdk
.tools/emsdk/emsdk install "$(cat .emscripten-version)"
.tools/emsdk/emsdk activate "$(cat .emscripten-version)"
source .tools/emsdk/emsdk_env.sh
emcmake cmake -S . -B build/web-debug -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build/web-debug
python3 scripts/serve.py
```

## Composable camera controllers

`CameraController` composes world-space orbit, follow, tracking, group framing,
half-life smoothing, swept-sphere collision avoidance and interruptible pose/lens
transitions. Targets use actor/component handles or world points. Applications
choose behaviours and supply commands, camera delta and viewport aspect after
scene/physics updates. Collision constraints run after smoothing/transitions and
reuse optional scene queries; cameras alone do not initialize physics.

See [camera controller contracts and examples](docs/camera-controllers.md) for
following/orbit modes, framing, custom behaviours/queries, ownership, update
ordering and validation. The playground's existing parent-space `OrbitController`
remains compatible with input actions and scene archives.

## Sky backgrounds

Optional visible sky backgrounds support gradients, panoramas and Rayleigh/Mie
single-scattering atmospheres on WebGPU and WebGL 2, through either a fullscreen
draw or an inward-facing mesh sky sphere. Procedural cloud coverage/density/wind
and opt-in day/night lighting use explicit caller-owned clock steps. See
[sky settings, examples and validation](docs/sky.md); the playground material
inspector has a **Sky background** section. The playground starts with a mesh
sphere, atmosphere, moving clouds and a 180-second day/night cycle. Its controls
include cloud coverage, haze, time of day and cycle speed; Pause freezes the cycle.

## Rendering quality and browser resilience

Optional **Anti-aliasing** selects Off or 4x MSAA on WebGPU and supported WebGL 2
devices. Applications set `RenderQuality::msaaSamples` to 1 or 4 and read
`Renderer::effectiveMsaaSamples()` plus `RenderCapabilities::msaa4x`. Profiles keep
MSAA off by default. Scene layers and camera views resolve together before the
canvas GUI. See [MSAA contracts and validation](docs/rendering-quality.md#optional-msaa).

The **Rendering quality** inspector selects Low, Balanced (default) or High,
with custom resolution scale, DPR/dimension/pixel limits and shadow-map counts.
`?quality=low|balanced|high` selects the initial profile. Device capabilities and
GPU connection status are reported in the panel and performance JSON. Device
loss automatically rebuilds GPU resources while retaining scene and pause state;
exhausted recovery exposes an explicit retry. Visibility, frozen pages and
pagehide/pageshow share the existing suspension/clock contract.

Optional **Adaptive graphics quality** gradually adjusts resolution, shadows and
available mesh detail toward a chosen **4–100 ms frame-time target**. The selected
settings remain its ceiling; slower recovery and settling periods limit rapid
oscillation. Try `?scene=terrain&adaptive=1&targetMs=16.67` on either backend.
See [adaptive quality contracts and validation](docs/adaptive-quality.md).

Optional LOD reuses prepared shared meshes. Authored semantic BC7/ETC2/ASTC mip
variants can use supported devices with retained RGBA fallback; ordinary asset
imports continue to decode RGBA. See [quality APIs, lifecycle and validation](docs/rendering-quality.md).

## Mouse, keyboard and mobile input

Portable `InputBindings` maps application-defined controls through a context
stack to the existing action snapshots. The browser adapter also supports
stacked contexts and disposable virtual controls with configurable stick dead
zones, radius and axis inversion. GUI wrapping, scaling, scroll panels and
accessibility integration are described in [UI/input integration](docs/ui-input-integration.md).
Open `?gui=1&guiExtensions=1` to exercise these GUI features.

The browser host uses `web/input.js` and Pointer Events for mouse, pen and
multi-touch input. `engine::InputSystem` in `engine_core` exposes the same action
API to all controllers. The demo camera consumes `Orbit`, `Pan`, `Zoom` and
`ResetCamera`; applications can consume `Select`, `Move`, `Look`, `Interact` or
their own nonzero 32-bit action IDs.

| Action | Mouse / keyboard | Android / iOS browser touch |
| --- | --- | --- |
| Orbit | Left drag, arrow keys | One-finger drag |
| Pan | Middle drag, Shift-drag, Shift + arrows | Two-finger drag |
| Zoom | Wheel, + / − | Pinch, hold + / − buttons |
| Select action | Click | Tap |
| Reset camera | R, Reset camera button | Reset camera button |

Focus the canvas to use camera keys. Six CSS pixels separate taps from drags.
Adding/removing a finger rebases the gesture without jumping. A pointer keeps its
owner until release: scene, C++ GUI or a virtual control. GUI input and focused
text fields consume input before camera controls. Existing GUI text editing uses
its browser textarea service for Unicode, selection, composition and mobile
keyboards; gameplay never derives text from physical key codes.

Input is batched into Wasm once before each frame update. The host retains a
reusable buffer and rebuilds its typed-array view after Wasm memory growth. The
engine preserves all press/release transitions, including taps between frames,
and sums displacement once per frame. Held axes use elapsed time; pointer
displacement does not. Blur, hidden pages, viewport resizing and context changes
release held input and discard unfinished gestures. Event queues are bounded;
overflow or invalid packets cancel input instead of leaving keys stuck.

`InputActionState` supplies `pressed`, `held`, `released`, `value` (a clamped
two-dimensional held axis) and `delta` (accumulated displacement). Independent
bindings have independent source IDs: releasing one key/finger preserves another
source holding the same action. Opposing sources can produce a zero axis while
the action remains held. `beginFrame()` consumes the pending queue and expires
the previous frame's edges and deltas. `events()` retains ordered records for
that frame; `pointers()` contains scene-owned active contacts, with both local
CSS coordinates and actual canvas backing pixels for picking.

```cpp
#include "engine/input.hpp"

// Once per frame, after the device adapter has submitted input:
input.beginFrame();
const auto& movement = input.action(engine::InputActions::Move);
const auto& look = input.action(engine::InputActions::Look);
if (input.action(engine::InputActions::Interact).pressed) interact();
move(movement.value, dt); // Held input is a rate.
lookAround(look.delta);   // Pointer input is displacement.
```

The demo's browser adapter is `EngineInput.instance`. It starts in the `Camera`
context. `Gameplay` maps WASD to `Move`, E to `Interact` and scene dragging to
`Look`; `Menu` and `TextEntry` suspend scene controls. Custom contexts can have
their own mappings. Physical keyboard bindings are configurable and removable:

```js
const input = EngineInput.instance;
input.setContext('Gameplay');
input.clearBindings('Gameplay');
const unbind = input.bindKey('Gameplay', 'KeyI', EngineInput.Actions.Move, [0, 1]);
// Later: unbind();

// Application-provided HTML controls; .input-stick includes a moving thumb.
input.attachControl(document.querySelector('#move-stick'), {
  action: EngineInput.Actions.Move, kind: 'stick', context: 'Gameplay',
});
input.attachControl(document.querySelector('#interact'), {
  action: EngineInput.Actions.Interact, context: 'Gameplay',
});
```

Virtual sticks have a dead zone and a circular limit; several controls can be
operated simultaneously. Controls should be native buttons with accessible
labels and `touch-action: none`. Pointer coordinates use actual canvas dimensions,
including the renderer's DPI cap. Mouse, keyboard and touch remain available
simultaneously rather than being selected by OS detection. The starter ships
camera controls; gameplay movement, object picking and game-specific control
layouts belong to the application using these actions.

`tests/input_tests.cpp` covers action edges, multiple sources, cancellation,
malformed/overflow batches, coordinates and camera frame-rate behavior.
`node scripts/input-smoke.cjs` checks real WebGPU pixels, desktop controls,
Chromium multi-touch, virtual controls and GUI isolation. Use the browser test
environment below; optional `DEMO_URL` and `EVIDENCE_DIR` select the server and
evidence directory. Chromium mobile emulation does not establish physical Android,
iOS Safari, native keyboard or OS IME acceptance; those remain device checks.

## Directional and spot shadow maps

The demo sun casts shadows by default. **Sun shadows** toggles the depth passes;
`?shadows=off` starts with them disabled. Lighting and shadows use the same
visibility mask for each camera view, so inset views can have different casters.

```cpp
auto& sun = scene.addChild<engine::LightComponent>("Sun", engine::LightType::Directional);
sun.direction = {-0.5f, -1, -0.4f};
sun.shadow.enabled = true;
sun.shadow.center = {0, 2, 0}; // Local to the light node, transformed by its hierarchy.
sun.shadow.halfExtent = 10;   // Square XY half-size in world units.
sun.shadow.halfDepth = 20;    // Half-distance along the light direction.
sun.shadow.depthBias = 0.0005f; // Normalized shadow depth bias.
sun.shadow.normalBias = 0.02f;  // World-space geometric-normal offset on grazing faces.
mesh.castsShadow = true;
mesh.receivesShadow = true;
renderer.setShadowsEnabled(true);
```

Each view budgets up to four maps among its first eight active lights. WebGPU
uses a `Depth32Float` array and WebGL2 a depth24 array, 1024x1024 by default.
Directional lights default to an orthographic volume centered at `shadow.center`;
configure its extent and depth to cover the relevant scene. XY translation snaps
to shadow texels. The shader uses a 3×3 percentage-closer filter with a linear
comparison sampler, raster depth bias, and configurable receiver bias. Receivers
outside the volume remain lit. Shadows attenuate only the matching light's direct
diffuse/specular contribution; ambient, environment lighting and emission remain.

World-layer opaque and masked meshes cast shadows, including instanced, mirrored,
double-sided and skeletal geometry. Masked materials use the same texture, UVs,
opacity and alpha cutoff in both passes. Alpha/additive meshes can receive shadows
but do not cast; particles and Background/Overlay geometry do not participate.
Camera-frustum culling retains off-screen casters for the shadow pass. Casters use
separate instance batches without reducing color-pass batching or repacking slots.
Camera-fitted cascades and shadowed spotlights are optional on both backends; see
[lighting and HDR post-processing](docs/lighting-post-processing.md). Enabling
shadows on a point light or supplying invalid settings raises an error; lights
beyond the four-map budget stay lit.

Performance exports include `shadowPasses`, `shadowDrawCalls`, `shadowInstances`,
the shadow depth allocation, and WebGPU-only `gpu.shadowMs`. `gpu.totalMs` includes shadows
plus all color layers; `gpu.layersMs` retains the three color-layer durations.
Unchanged shadow camera matrices and instance data are not uploaded again.

`ctest --test-dir build/native --output-on-failure` includes shadow projection,
texel stability, hierarchy, participation and timing tests. With the browser test
dependencies described below, `node scripts/shadow-smoke.cjs` verifies real WebGPU
pixels, cutouts, skinning, multiple lights/views, camera masks, off-screen casters,
disabled shadows and warm uploads. Screenshots/results go to
`build/evidence/shadows/`. Legacy exact-count renderer checks use `shadows=off`
and `culling=off` to preserve their color-pass baselines.

## Reusable scene objects

`Node` provides a name, local transform, enabled state, and owned children. `Actor`, `Component`, and `Scene` share that hierarchy. Components can have their own child components or actors; a scene can own actors or components directly.

```cpp
#include "engine/scene.hpp"
#include "engine/actor.hpp"
#include "engine/mesh_component.hpp"
#include "engine/orbit_controller.hpp"
using namespace engine;

Scene scene{"My scene"};
auto& actor = scene.addChild<Actor>("Crate");
actor.transform.position = {0, 1, 0};

const auto geometry = Mesh::cube();
auto& mesh = actor.addChild<MeshComponent>("Body", geometry);
mesh.color = {0.1f, 0.6f, 0.9f};

auto& child = mesh.addChild<MeshComponent>("Attachment", geometry);
child.transform.position = {0, 1, 0};
child.transform.scale = {0.3f, 0.3f, 0.3f};

auto& camera = scene.addChild<CameraComponent>("Camera");
OrbitController controls(camera);
auto& light = scene.addChild<LightComponent>("Key light", LightType::Directional);
light.direction = {-1, -1, -1};
light.intensity = 2;
```

`addChild<T>()` returns a stable reference. Parents own children through `unique_ptr`; nodes cannot be copied. World transforms compose recursively, including component-to-component nesting. Disabling any node excludes its entire subtree from scene collection. `Scene::collect()` produces mesh and light descriptions for rendering.

`MeshComponent` instances share immutable `Mesh` geometry through `shared_ptr<const Mesh>`. Each instance has its own transform, color tint, layer, and optional `texture` override. A mesh carries UVs, materials, and indexed material ranges. Geometry and decoded `Texture` resources are uploaded once and shared across components. Expired resources are removed from the GPU caches.

```cpp
mesh.color = {1, 1, 1}; // Neutral tint for the texture's original colors.
mesh.texture = engine::Texture::loadMemory(imageBytes, "Crate base color");
// Or populate the override directly (preserves it on decode failure):
mesh.loadTexture(imageBytes);
mesh.texture.reset(); // Use the mesh's imported material textures again.
```

The shared decoder supports PNG, JPEG, TGA, and BMP in native builds and WebAssembly using pinned [stb_image v2.30](https://github.com/nothings/stb/tree/013ac3beddff3dbffafd5177e7972067cd2b5083). Source pixels use a top-left UV origin and RGBA8 storage. Material slots choose the GPU interpretation: base color and emission are sRGB, while metallic–roughness, occlusion and tangent-space normal maps are linear. Color mips average in linear light, normal mips renormalize vectors, and odd dimensions retain edge texels. Cube, plane, sphere and FBX geometry provide UVs and tangents. Explicit alpha modes control mesh transparency; particle effects retain their dedicated shaders.

`CameraComponent` supports perspective and orthographic projection and derives its view from its world transform. `OrbitController` operates in its camera parent's coordinate system and can be used with another camera. A `LightComponent` supports directional, point or spot lighting with color, intensity, range and authored cone half angles. Position and direction inherit the hierarchy. The starter shader supports the first eight enabled, visible lights and warns if more are supplied.

Camera lens setters validate the complete update before changing state:

```cpp
camera.setPerspective(pi / 3, 0.1f, 100.0f); // Vertical FOV in radians; 0 < near < far.
camera.setOrthographic(10.0f, 0.0f, 100.0f); // Vertical world height; 0 <= near < far.
camera.setViewport({0.5f, 0, 0.5f, 1});     // Right half of the render target.
camera.setVisibilityMask(0x2);
mesh.visibilityMask = 0x2;                 // Lights also have visibilityMask.
camera.lookAt({0, 0, 0});                  // World target; default world up is +Y.
const auto pixels = camera.pixelViewport(targetWidth, targetHeight);
const auto ray = camera.screenPointToRay({mousePixelX, mousePixelY}, pixels);
const auto screen = camera.worldToScreen(worldPoint, pixels);
const auto point = camera.screenToWorld(screen.pixel, screen.depth, pixels);
const auto& clipping = camera.frustum(pixels.width / pixels.height);
```

`Viewport` is normalized to the render target; `PixelViewport` is an actual pixel rectangle with a top-left origin and downward Y. Conversion methods take that pixel rectangle directly. Convert CSS pointer coordinates to canvas backing pixels before picking. Aspect comes from the actual rectangle, including its width/height fractions. Depth uses WebGPU's `[0,1]` range. `ScreenPoint` contains pixel coordinates, depth, `inFront`, and `visible` (inside the clip volume with an enabled camera; this does not test occlusion). A point on the perspective eye plane returns NaN coordinates/depth and `visible=false`. Offscreen pixels may be unprojected. Perspective rays originate at the camera eye; orthographic rays originate on the near plane and are parallel.

`view()`, `projection(aspect)`, `viewProjection(aspect)`, `inverseViewProjection(aspect)`, and `frustum(aspect)` return cached references, valid until a later camera query changes their inputs. `revision()` observes direct transform/lens writes and ancestor changes; lens mode, viewport, visibility mask, effective enabled state, and the last queried aspect also advance it. Compare revision values for changes rather than counting increments. Legacy `fieldOfView`, `nearPlane`, and `farPlane` writes remain supported and are validated on camera queries. `worldPosition()`, `forward()`, `right()`, and `up()` expose world-space position and normalized transformed axes. Scale remains part of the inherited view transform; nonuniform parent scale can skew these axes. `lookAt()` accounts for parent transforms, preserves position/scale, and rejects coincident targets, zero up vectors, and parallel target/up directions.

Visibility masks select meshes and lights by overlapping bits, independently of `RenderLayer`; components default to group `1` and cameras default to all groups. Filtering one component does not suppress differently masked children. Disabling a node still suppresses its subtree, and a disabled camera or ancestor submits no scene objects. `Scene::collect(mask)` and `RenderFrame::collect(scene, instancing, mask)` support the same filtering outside the renderer. Camera selection remains with `Renderer(scene, camera)`; no per-camera main flag or controller settings are added. Use `Renderer::setViews()` to compose ordered cameras into one canvas; see the multi-camera example below.

Multi-camera compositing uses one renderer, one surface acquisition, and one GPU submission:

```cpp
Renderer renderer(scene, mainCamera); // Existing single-camera use stays supported.
mainCamera.setViewport({0, 0, 1, 1});
overviewCamera.setViewport({0.68f, 0.05f, 0.28f, 0.32f});
overviewCamera.setOrthographic(18, 0.1f, 100);
const std::array views = {
    RenderView{&mainCamera},
    RenderView{&overviewCamera, std::array<float, 4>{0.04f, 0.07f, 0.1f, 1}},
};
renderer.setViews(views);
// RenderView{&overlayCamera, std::nullopt} draws over existing color.
// Renderer(scene) starts with no views; setViews({}) clears the canvas.
```

Views draw in list order. Each view completes Background, World, and Overlay before
its successor. `clearColor` clears only that camera's viewport; `std::nullopt` loads
earlier color. Depth starts fresh for each camera/layer, so later cameras can draw
over earlier geometry regardless of its depth. GUI renders once over the completed
canvas. This is direct viewport compositing; it does not provide camera render
textures, whole-view opacity, postprocessing, or shared depth between cameras.

Cameras remain caller-owned and must stay alive until removed from the renderer's
view list. `setViews()` validates and copies the list atomically. Disabled cameras
(including disabled ancestors) are skipped without clearing their rectangles.
The target is initialized to the default background each frame, including when
all views are disabled or the list is empty. Viewport aspect, visibility masks,
transparent sorting, billboard axes, and staging/upload caches are independent
per view. Mesh, material, texture, environment, and skeleton resources stay shared.
A [WebGPU scissor rectangle](https://developer.mozilla.org/en-US/docs/Web/API/GPURenderPassEncoder/setScissorRect)
clips draws and viewport clear triangles; normalized rectangle edges round by
pixel centers, so odd-width split screens have no gap. GPU layer timings sum
across active views; draw/upload counters count actual work across the compositor.

The playground's **Camera views** selector offers Single camera, Split screen,
Overview inset and an oblique orthographic inset for inspecting water reflections.
`tests/render_view_tests.cpp` checks planning, masks, disabled
ancestors, atomic validation, pixel rounding, and independent cached staging.
`node scripts/compositing-smoke.cjs` verifies real WebGPU pixels for split/inset
views, load-color overlays, ordering, independent depth, viewport clipping,
particles, resize, disabled/empty views, upload reuse, and public layout controls.
Use the same browser/dependency environment as the other smoke checks; evidence
is written to `build/evidence/compositing/`.


Frustum planes have inward unit normals ordered left, right, bottom, top, near, far. `containsPoint`, `intersectsSphere`, `intersectsAabb`, and `intersectsTransformedAabb` provide visibility tests. The renderer automatically culls meshes against each camera's frustum using that view's viewport aspect ratio. Static local bounds are cached on shared mesh resources; skeletal bounds follow the current bone palette and are cached until its revision changes. Updating animated bounds scans skinned vertices on the CPU.

Culling covers opaque and transparent meshes in every layer, preserves traversal order and original instance slots, and never hides a mesh's children. Off-screen geometry remains in `SceneFrame` and `RenderFrame::batches()` for shadow casting; camera passes use `RenderFrame::cameraBatches()` and transparent passes respect `MeshDraw::cameraVisible`. This reduces camera submissions while retaining staging and resource uploads required by shadows. Particles retain their existing behavior.

Use `MeshComponent::frustumCulling=false` for custom vertex deformation or `Renderer::setFrustumCullingEnabled(false)` to disable culling globally (`?culling=off` in the demo). Bounds with no usable positions conservatively remain visible. Mesh resources are immutable after publication; call `Mesh::invalidateBounds()` after editing static vertex positions in place before reuse. Camera-free `Scene::collect()` stays unculled; supply a `Frustum*` to scene/frame collection for visibility classification. Performance JSON reports `visibleMeshes` and `culledMeshes` alongside the original `meshes` count, summed across views.

`frustum_culling_tests` covers clipping planes, camera/aspect changes, transformed and animated bounds, masks, hierarchy, transparent meshes, stable object uploads and warm frame capacities. `scripts/frustum-culling-smoke.cjs` compares WebGPU pixels and submissions with culling on/off for two camera positions. Run it against a browser build using the same Playwright setup as the browser acceptance script; evidence is saved under `build/evidence/frustum-culling/`.

## Reflective animated water

Open `?scene=water&renderer=webgpu` or `?scene=water&renderer=webgl2`, or choose
**Open water demo** in the inspector. Drag to orbit, scroll to zoom and use
**Pause motion**, **Reset camera**, surface presets and **Return to demo**.

`MaterialShader::Water` adds three mesh-local Gerstner waves, moving normal-map
ripples, Fresnel reflection and light highlights through ordinary mesh materials.
WebGPU can reflect `Scene::environment`; WebGL 2 uses configurable horizon/zenith
reflection colors. Set `Material::water` or `MaterialInstance::water`, and supply
an explicit clock with `Renderer::setWaterTime(seconds)` before rendering.
Use a subdivided local-XZ mesh for geometric waves. Water bounds account for
displacement; shared assets, transforms and physics remain independent.
Opt into depth-aware refraction/absorption and shoreline foam through
`WaterSettings::refractionStrength` and `foamStrength`; both default to zero.
The demo includes a sloping lakebed, submerged markers and independent effect
presets. Both backends capture current-view opaque World color/depth, support
1x/4x MSAA and preserve ordered cameras, resize and recovery. Opt into nearby-object
reflections with `planarReflectionStrength` (default zero). A mirrored camera captures
opaque/masked World objects above the transformed water plane, once per view;
coplanar water shares it. The demo includes colored landmarks and planar-off/still
mirror comparisons. `underwaterStrength` (default zero) enables depth-based fog
and color absorption inside the water footprint on both backends. Configure
`underwaterFogDensity`, `underwaterColor`, `underwaterMaxDistance` and existing
`absorption`; use a double-sided surface to see its underside. The demo's View
selector provides underwater/deep-water views and independent fog/absorption
presets. Optional `causticsStrength` (default zero), `causticsScale`, `causticsSpeed`
and `causticsDepth` add animated depth-faded light patterns on submerged receivers.
The demo enables them and offers caustics-off/still comparisons. Underwater composition runs after World transparency and before Overlay/GUI.
Submit up to 16 transient `WaterInteraction` values with `Renderer::setWaterInteractions`
for contact rings and directed wakes. Applications supply world-space contacts,
headings and emission times; the demo includes moving sources and an Object
interactions selector. Both backends perturb water normals and crest highlights
without changing geometry, collision or picking.
See [water APIs, defaults, limitations and validation](docs/water.md).

## Materials and image-based lighting

Use **Open material graph** in the Materials inspector, or open
`?scene=materials&graph=1`, to author node-based materials with live 3D preview,
typed connections, color/value math, UV transforms, checker/noise patterns, local
image textures, undo/redo and JSON save/load. Procedural branches bake into the
existing PBR texture slots when edited. See [Material graph editor](docs/material-graph.md)
for controls, architecture, limits and verification.

The four material stages are implemented: independent shared assets/slot instances,
GPU bindings and render-state variants, metallic–roughness PBR and import mapping,
then baked environment lighting and a live browser inspector.

`Mesh::materials` contains `shared_ptr<const Material>` assets. A `MeshPart` indexes
one slot. Build an asset, then stop mutating it after publishing. Components share
geometry and asset references; `MaterialInstance` holds optional scalar/map/state
overrides plus tint, opacity and UV transforms. Primitive meshes without explicit
parts still have slot zero. `setMaterial(slot, instance)` validates before changing
an override; `clearMaterial(slot)` restores the mesh asset. Failed edits leave the
previous state intact. Successful `loadFbx` calls clear stale slot overrides.

```cpp
#include "engine/material.hpp"
auto oak = std::make_shared<engine::Material>();
oak->name = "Oak";
oak->baseColorTexture = engine::Texture::loadMemory(woodBytes);
oak->normalTexture = engine::Texture::loadMemory(normalBytes);
oak->roughness = 0.75f;
std::shared_ptr<const engine::Material> sharedOak = oak; // Publish; freeze oak.
engine::MaterialInstance appearance(sharedOak);
appearance.tint = {0.7f, 0.9f, 1.0f};
appearance.roughness = 0.5f;
mesh.color = {1, 1, 1}; // Legacy component tint still multiplies the material.
mesh.setMaterial(0, appearance);
```

The contract follows [glTF metallic–roughness conventions](https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html):
base-color and emissive factors are linear; roughness uses G and metallic uses B
of the shared map; occlusion uses R and affects indirect lighting. Tangent-space
normal maps use +Y and expose `normalScale`. Material defaults are white,
nonmetallic, roughness 0.6, opaque and single-sided. Optional map overrides distinguish
inheritance (disengaged) from removal (engaged null). Missing maps share white or
flat-normal fallback textures. One source image can have distinct cached GPU
uploads for color, data or normal interpretation.

`AlphaMode::Opaque` ignores texture/material alpha and writes depth. `Mask` tests
combined alpha against `alphaCutoff` and writes depth. `Blend` and `Additive` use
the camera-sorted FX queue, depth-test without writing depth, and output premultiplied
alpha. Modes are per slot. `doubleSided` disables backface culling and corrects
backface normals. Mirrored instances select the appropriate front-face winding;
tangents and bitangents follow skinning and world transforms. Tangent generation
averages matching corners and splits mirrored UV orientations, retaining skin
weights. It is not a MikkTSpace implementation; author normal maps against this
basis or check imported assets visually.

PBR uses GGX direct highlights and Fresnel with energy-conserving diffuse lighting.
`MaterialShader::Unlit` uses base color/emission without lights; particles retain
dedicated effect shaders. Pipelines are cached by alpha/shader/culling/skinning/
winding state, independent of texture identity. Material parameter buffers and
bind groups are shared by effective binding, excluding per-instance color and
opacity. Unchanged scenes perform no material, object, camera or bone writes.
Expired bindings/resources are reclaimed while reusable frame capacities remain.
Performance JSON exposes material/pipeline counts and material write bytes.

FBX imports map available base color/factor, roughness, metalness, emission,
normal, occlusion, opacity, double-sided and unlit properties through ufbx,
including its Blender PBR mapping. Separate scalar textures are packed into G/B
using their red channel (linear nearest resampling when resolutions differ).
Glossiness textures are inverted; ufbx already converts scalar glossiness to
roughness. Transparent base-color alpha infers Mask for binary alpha and Blend
for partial alpha; scalar opacity below one selects Blend. Decode/packed-map bytes
share the import budget. Knarr now decodes 21 source images and generates seven
metallic–roughness maps, with 117,440,512 bytes of cumulative decode/packing work;
this is not a live-memory measurement. Unsupported alternate UV transforms/sets,
layered/procedural maps, separate opacity maps and transmission produce warnings.

`Scene::environment` defaults to `Environment::studio()`, an HDR procedural sky
with a broad light source. `Environment::bake(name, radianceCallback)` accepts
linear HDR radiance and precomputes a GGX specular cube mip chain, diffuse
irradiance cube and split-sum BRDF LUT. These upload as filterable float16 textures.
`Environment::fromEquirectangular(image)` accepts sRGB RGBA8 panorama images.
Set `environmentIntensity` and Y-axis `environmentRotation` (radians), or clear
`environment` to disable image-based lighting. Reflection detail depends on the
baked cube resolution (default 32, configurable power of two from 4 to 128).
Lighting parameters change without rebaking/reuploading images.

Open `?scene=materials` or click **Material showcase**. Select an object and slot;
edit PBR/unlit parameters, alpha/culling, UV transforms or local texture maps.
**Reset selected slot** restores its mesh asset. **Return to scene** retains the
imported model and its edits. **Override texture** remains a whole-mesh convenience
and updates base-color overrides in existing instances too; direct `mesh.texture`
assignment fills slots without explicit material/map overrides. **Choose panorama**
replaces environment lighting; **Studio sky** restores the default. All files stay
local. Use **Save scene** to retain inspector edits and embedded assets across sessions.
See [scene and asset archives](docs/scene-archives.md).

The showcase covers shared oak instances, polished/rough copper, cutout cloth,
alpha transparency, animated normal mapping and an unlit surface. This renderer
uses ordinary mesh-origin sorting for blended surfaces; it does not implement
refractive glass, per-triangle sorting, shadow maps or a material graph editor.
Panorama file loading supports PNG/JPEG/TGA/BMP rather than HDR/EXR file decoding;
HDR environment radiance is available through `bake`.

Native `material_tests` checks atomic slot overrides, resource sharing, alpha
routing, batching boundaries, texture semantics, mirrored UV seams, float16
encoding and HDR convolution invariants. Browser acceptance uses actual WebGPU
pixels and the inspector:

```powershell
node scripts/material-smoke.cjs
```

It also verifies static/skinned/mirrored tangent equivalence and double-sided
backfaces with isolated `?materialTest=1` fixtures. Screenshots and results are in
`build/evidence/materials/`; the scoped validation record is in
`docs/materials-validation.md`.

## Actor identity, components and lifecycle

Actors reuse `Node` for names, transforms, enabled state and child ownership.
`Scene::spawn<T>()` (or the existing `addChild<T>()`) registers an actor immediately
and assigns a scene-scoped `ActorId` with a slot and generation. Resolve it through
`scene.findActor(id)`; stale, foreign-scene and pending-destruction handles return
null. IDs are runtime handles, not persistent save identifiers. Detaching or moving
an actor to another scene retires its old handle; same-scene reparenting preserves it.

```cpp
class RotatingActor : public engine::Actor {
public:
    explicit RotatingActor(std::string name):Actor(std::move(name)) {
        setTickEnabled(true); // Actor updates are opt-in.
    }
protected:
    void tick(double seconds) override { transform.rotation.y+=float(seconds)*0.25f; }
};

auto& actor=scene.spawn<RotatingActor>("Crate");
auto& mesh=actor.addComponent<MeshComponent>("Body",Mesh::cube());
actor.addTag("selectable");
const ActorId handle=actor.id();
scene.update(deltaSeconds); // Lifecycle, actor/component ticks and animations once.
if(auto* live=scene.findActor(handle)) {
    if(auto* body=live->getComponent<MeshComponent>()) body->color={0.2f,0.6f,0.9f};
}
actor.destroy(); // Safe even from tick(); hidden and unresolvable immediately.
scene.flushPendingDestroy(); // Or let the next scene update flush automatically.
```

`addComponent<T>()`, `getComponent<T>()`, `getComponents<T>()`, `hasComponent<T>()`
and `removeComponent(component)` use the existing owned hierarchy. Queries include
disabled and nested components, including a skeletal mesh's skeleton/animation,
but stop at child actors. `Component::owner()` returns the nearest actor ancestor;
components attached directly to the scene have no actor owner. The skeletal mesh's
required skeleton/animation children cannot be removed or moved independently;
remove the containing skeletal mesh instead. Query results are non-owning pointers
and remain valid only until their components are removed.

Override `onSpawn`, `onDespawn`, `onEnable`, `onDisable`, `tick`, and `fixedTick`
on actor subclasses. Lifecycle hooks are `noexcept`. The first scene update calls
spawn once, then enable when active in the hierarchy. `ActorState::Active` means
spawned, including temporarily disabled actors. Public `enabled` writes and ancestor
changes are reconciled at the next update; toggling tick alone does not disable an
actor. Cleanup runs children first, before derived destructors, and also runs during
scene teardown. Cleanup hooks must not add children to the departing subtree.
Actors created during a tick begin their lifecycle on the next update.

Call `scene.update(deltaSeconds)` once before rendering. `updateAnimations()` is a
compatibility alias, so call one or the other. `fixedUpdate(stepSeconds)` is an
explicit positive fixed step; it does not advance animation clocks or provide a
physics accumulator. Actor tick opt-in governs both tick hooks. `tags()`, `addTag`,
`removeTag`, `hasTag`, and `Scene::findActorsWithTag()` support unique actor labels.

Node APIs now include `isActiveInHierarchy()`, `setEnabled`, validated
`setLocalTransform`/`setWorldTransform`, `adoptChild`, `detachChild`, `removeChild`
and `reparent(newParent, ReparentMode::KeepLocal/KeepWorld)`. Cycles and singular
or sheared local TRS conversions are rejected before committing transforms.
World preservation can fail under rotated nonuniform parents when the required
local transform contains shear. Detach/reparent require an idle hierarchy and a
subtree without pending removal; child removal in a scene is deferred. Public
`visit()` does not flush removals itself. Scene updates flush after traversal;
an explicit `flushPendingDestroy()` must also run outside traversal. Raw actor
references must not be used after flushing destruction.

## Component lifecycle, updates and settings

`Component` inherits the existing `Node` name, transform, enabled state and child
hierarchy. Adding a component to a scene registers it immediately: `id()` returns
a scene-scoped `ComponentId`, and `isRegistered()` reports that registration.
`scene.findComponent(id)` returns a non-owning pointer, or null for a stale,
foreign-scene or pending-removal handle. Detachment and cross-scene reparenting
invalidate the old handle; reparenting within one scene preserves it. Runtime
handles are not persistent save identifiers.

```cpp
class MovementComponent : public engine::Component {
public:
    explicit MovementComponent(std::string name):Component(std::move(name)) {
        setTickEnabled(true);
        addTag("movement");
    }
    std::string_view typeName() const noexcept override { return "MovementComponent"; }
protected:
    void tick(double seconds) override { transform.position.x+=float(seconds); }
    void onAttach() noexcept override { /* Initialize scene dependencies. */ }
    void onDetach() noexcept override { /* Release subscriptions/resources. */ }
};

auto& movement=actor.addComponent<MovementComponent>("Movement");
const auto handle=movement.id();
scene.update(deltaSeconds); // Runs component lifecycle, ticks and animation once.
movement.setFixedTickEnabled(true);
scene.fixedUpdate(1.0/60.0); // Calls an overridden fixedTick independently.
auto* mesh=movement.getComponent<MeshComponent>();
movement.destroy(); // Queues removal; findComponent(handle) now returns null.
scene.flushPendingDestroy(); // Deletes the component and its descendants when idle.
```

The first `Scene::update` or `fixedUpdate` calls `onAttach` and sets
`isInitialized()`. Disabled components initialize too, but receive `onEnable`
only when active in the hierarchy. `onEnable`/`onDisable` reconcile ancestor and
component enabled changes at scene updates. Detachment immediately calls
`onDisable` when needed, then `onDetach`, with descendants cleaned up first.
Removing a component before its first update runs neither attach nor detach.
Reattachment initializes it again; changing owners within one scene does not.

Variable and fixed ticking are independently opt-in, controlled by
`tickEnabled()`/`setTickEnabled()` and
`fixedTickEnabled()`/`setFixedTickEnabled()`. Disabled and pending-removal
subtrees do not tick. Components created during hooks or ticks begin on the next
update. `AnimationComponent` opts into variable ticking and uses the same path;
fixed updates do not advance playback. `updateAnimations()` remains an alias
for the full scene update, so call either it or `update()` once per frame.

Lifecycle and `onOwnerChanged(previous, current)` hooks are `noexcept`. Ownership
notifications run after adoption, detachment or reparenting updates the hierarchy,
including hierarchies outside a scene. A moved component subtree is notified
only where its nearest actor ancestor actually changes. Hooks may queue removal
or append children, but cannot erase/reparent nodes or reenter a scene update.
Cleanup cannot add children inside the subtree being detached, and scene shutdown
rejects all additions. `destroy()` requires a scene and respects protected
implementation children such as a skeletal mesh's skeleton and animation player.

`getComponent<T>()`, `getComponents<T>()` and `hasComponent<T>()` forward to the
owning actor's queries. Results include the calling component, disabled components
and nested components, exclude pending-removal subtrees, and stop at child actor
boundaries. Without an actor owner, results are null/empty/false. Queries have
const and mutable overloads. Component tags use `addTag`, `removeTag`, `hasTag`
and `tags`; empty tags are rejected and duplicates are ignored.

`serialize()` produces a version-1 `ComponentSnapshot` containing the stable
`typeName`, name, local transform, enabled state, both tick flags and component
tags. `deserialize(snapshot)` validates the version, matching type, finite,
nonsingular transform and unique nonempty tags before changing any settings.
It preserves identity, owner, children and initialization; enabled transitions
are reconciled by the next scene update. `validate()` checks the current common
settings, including legacy direct transform writes. Invalid settings throw
`std::invalid_argument` and invalid snapshots leave existing settings intact.
These virtual functions provide a common-settings persistence boundary: mesh
assets, camera/light settings, animation clocks, child hierarchy and a file/JSON
scene format are not included. Custom types should override `typeName()` with a
stable label and extend persistence separately for their own properties.

Native `component_tests` covers lifecycle, independent ticks, owner changes,
queries, deferred removal, generation reuse, protected children, cleanup mutation,
scene shutdown and snapshot validation/recovery.

## FBX mesh importer

`FbxImporter` uses vendored [ufbx v0.23.1](https://github.com/ufbx/ufbx/tree/26a482ae66871d7de36eb722aa060bce95bce274), pinned to a commit with its license included. It triangulates polygon faces, reads corner normals (or generates missing normals), converts coordinates to right-handed Y-up meters, and merges all mesh-node instances into one engine `Mesh`. Parent, pivot, and geometry transforms are baked into positions; normals use the corresponding inverse-transpose transform. Mirrored nodes have their triangle winding corrected. It imports per-face material assignments, base-color values, corner UVs (including seams and named texture UV sets), UV transforms, wrapping, and embedded or explicitly resolved base-color images. FBX's bottom-left UVs are converted to the engine's top-left convention once.

```cpp
#include "engine/mesh_component.hpp"

engine::MeshComponent component{"Imported", nullptr};
component.loadFbx(bytes); // std::span<const std::byte>, works in native code and WebAssembly

// Native builds resolve relative images under the model directory,
// with a basename fallback for relocated exports:
component.loadFbxFile("assets/model.fbx");

// Or get shared geometry without an existing component:
auto geometry = engine::FbxImporter::loadMemory(bytes);

// Memory imports can resolve companion images from your own asset store:
engine::FbxImportOptions options;
options.resolveTexture = [&](std::string_view path) {
    return assets.readBytes(path); // Return vector<byte>, or {} when missing.
};
component.loadFbx(bytes, options);
```

These calls throw a descriptive exception if parsing or conversion fails. A component keeps its previous mesh on failure, and importing preserves its transform, color, layer, and children. Imported geometry owns its buffers and remains valid after the input bytes are released. The renderer detects the new shared mesh resource and uploads it once.

The importer also retains skin bindings, bone hierarchy and animation clips for `SkeletalMeshComponent` (see below). Source lights, cameras and general node hierarchy are not recreated as engine actors. Polygonal geometry is required; NURBS/subdivision evaluation is outside this importer. `Mesh::warnings` reports missing external textures, unavailable UV sets, and unsupported texture arrangements. Corrupt supplied images fail the import and preserve the previous mesh. Shared FBX texture references are decoded once; material ranges are grouped to avoid a draw call per face. Native companion resolution never reads outside the model directory, including through symlinks.

Default `FbxImportOptions` limits are 32 MiB of input, one million vertices before optimization, three million output indices, and 128 MiB for each of the parser's temporary and result allocators. A single source mesh's corner count and triangulation scratch buffer are also bounded. Textures are limited to 32 MiB encoded, 4096 pixels per dimension, 64 MiB decoded per image, and 128 MiB decoded across the import. Browser companion selection additionally limits the total encoded image bytes to 128 MiB. Custom options can lower these budgets; 4096 is the decoder's compiled maximum dimension.

The demo additionally centers the imported geometry, places it above the ground, and uniformly fits it within four world units for preview. The reusable importer preserves the file's converted meter scale. File parsing currently runs synchronously in the browser's Wasm thread; large imports can pause animation while parsing. Import errors appear beside the picker and leave the current scene rendering.

## GLB mesh importer

`GlbImporter` loads binary glTF 2.0 with vendored [cgltf 1.15](https://github.com/jkuhlmann/cgltf/tree/v1.15). It produces the same immutable `Mesh`, PBR `Material`, `Skeleton`, sampled clips, and morph targets used by FBX. Use the existing **Import FBX / GLB** picker, material inspector, morph sliders, and skeletal playback/blending controls.

```cpp
#include "engine/glb_importer.hpp"
#include "engine/skeletal_mesh_component.hpp"

engine::SkeletalMeshComponent model{"Model", nullptr};
model.loadGlb(bytes);                       // Native or WebAssembly
model.loadGlbFile("assets/character.glb");  // Native only
model.animation().play(0);
auto shared = engine::GlbImporter::loadMemory(bytes);
```

`GlbImportOptions` and `GlbImportMeasurements` share the FBX option/timing contract. Memory imports perform no implicit IO; `resolveTexture` supplies companion images. Native file loading resolves URI-decoded image paths only inside the model directory, including symlink checks. Both APIs retain ownership independently of the input bytes, and failed component imports preserve the previous mesh. The parser has a bounded allocator; geometry, decoded textures, clips, and morphs enforce their respective budgets.

Supported conversion:

- The default scene (or first scene) merges its mesh instances in right-handed Y-up meters, baking world transforms and correcting mirrored winding. Indexed/non-indexed triangles, strips, fans, interleaved and sparse vertex accessors, normalized attributes, missing normals, and regenerated tangents are supported.
- Metallic-roughness PBR retains all five texture slots, factors, normal scale, occlusion strength, emission, alpha mask/blend, and double-sidedness. PNG/JPEG images may be embedded in BIN, base64 data URIs, or explicit companions. Repeated texture references share decoded pixels. Unlit, emissive strength, quantized mesh attributes, and a shared `KHR_texture_transform`/UV set are supported. glTF UVs already have the engine's top-left origin.
- Skinning retains ancestor transforms, per-instance inverse binds, normalized strongest-four weights from `JOINTS_0/1` and `WEIGHTS_0/1`, and rigid bindings for unskinned parts. Translation, quaternion rotation, and scale animation support LINEAR, STEP, and CUBICSPLINE source interpolation. Clips are resampled at the configured rate (30 Hz by default) into the existing runtime, including both endpoints. As with FBX, runtime interpolation between sampled frames approximates source curves and STEP boundaries. Existing limits are 256 hierarchy joints, 1024 bindings, and 64 clips.
- Morph position/normal offsets, sparse targets, default weights, generated tangent offsets, and optimization preserve matching vertex streams. Morph-weight animation is warned about and omitted, matching the current FBX runtime limitation.

Scope: `.gltf` JSON/external geometry buffers, Draco/meshopt compression, GPU-instancing extensions, sparse index accessors, and sheared skeletal local transforms are rejected. Unsupported required extensions fail explicitly. Non-triangle primitives and vertex colors are warned about; advanced optional material lobes use metallic-roughness fallback. Per-map UV sets/transforms differing from the material's primary texture are warned about and omitted. Mirrored-repeat samplers approximate repeat; sampler filtering follows the renderer. Cameras/lights and original node actors are not recreated.

`python scripts/generate-glb-fixtures.py` recreates the original deterministic fixtures. `glb_importer_tests` covers material maps, skin deformation through the runtime palette, four interpolation clips, sparse morphs, instances/reflections, UV transforms, data URIs, companions, malformed input, budgets, and failure preservation. `scripts/glb-import-smoke.cjs` verifies the real picker and visible GPU pose changes, companion resolution, failure preservation, static replacement, and FBX regression. Set `NODE_PATH`, `BROWSER_EXECUTABLE`, and `DEMO_URL` as for the other browser checks; screenshots and results go to `build/evidence/glb`.

## Imported mesh optimization

FBX and GLB imports run a lossless meshoptimizer pass by default: weld only bit-identical
position/normal/UV/tangent tuples, reorder triangles separately within each material
batch for vertex-cache reuse, then order vertices by first indexed use. UV seams,
hard normals, triangle winding, material boundaries, texture references, and
source coordinates are preserved. Unreferenced vertices are removed; geometry
is not simplified or quantized. Import budgets apply before optimization.

`MeshOptimizer::optimize(Mesh&)` is reusable for other indexed meshes; invalid
indices or incomplete/overlapping material ranges throw without changing buffers.
The public CPU indices remain `uint32_t`. GPU uploads pack indices as `Uint16`
when the maximum referenced index is at most 65,535, otherwise `Uint32`, and pad
uploads to four-byte alignment. Memory measurements reflect the packed GPU data.

Knarr keeps all 40,956 triangles and seven textured material batches while
reducing 86,544 vertices (including tangent seam splits) to 38,621 (55.4% fewer). Its GPU geometry payload drops
from 4,645,584 to 2,099,544 bytes (54.8% smaller), excluding textures. These are
geometry savings, not a measured FPS improvement. Optimization adds one-time
import work, shown separately in the Performance panel and JSON export together
with vertex counts before/after. Set `FbxImportOptions::optimizeMeshes=false` or
use `?optimizeMeshes=off` in the demo to compare imports; GPU index packing still
chooses the smallest safe format in both modes.

Pinned meshoptimizer v1.3 sources and MIT license are in `third_party/meshoptimizer/`;
the browser distribution includes its license. Native optimizer tests compare
every oriented full-attribute triangle in every Knarr material batch, exercise
UV/normal seams and the 16/32-bit boundary, and check cache/fetch locality.
`node scripts/mesh-optimization-smoke.cjs` uses the browser test environment below
to compare deterministic WebGPU canvas pixels with optimization enabled/disabled,
verify exact geometry payload counters, and save screenshots/JSON evidence.

## Layered renderer

The C++ `Renderer` takes a scene and ordered camera views (the original scene/camera constructor creates one view). It traverses the scene per active view and submits three ordered WebGPU render passes per view:

| Layer | Behavior | Demo |
| --- | --- | --- |
| `Background` | Clears color and depth; optional background meshes | Dark backdrop |
| `World` | Loads color, clears depth, depth-tests world geometry | Ground, grid, cubes, light marker |
| `Overlay` | Loads color, clears depth, depth-tests within the overlay | Axes visible above world geometry |

Assign `mesh.layer = RenderLayer::Overlay` to render a component above world objects. Depth is independent between layers. Each pass draws opaque meshes, then its sorted alpha queue, then additive effects. Particles belong to World and use its existing depth buffer; FX never clear world depth or write depth. Overlay still clears depth after world FX. Static and skinned meshes support opaque, alpha and additive pipelines. Shadow maps, additional asset formats, and an ECS are extension points.

## Particles, flipbooks and reusable effects

The **Visual effects** panel plays a campfire (flames, smoke and sparks), a
one-shot explosion (fireball, smoke and sparks), or a fountain with drifting mist. Effects start stopped so the
existing demo remains unchanged until **Play / restart** or a preset selection.
**Pause / Resume** freezes all emitter clocks and particles. **Stop emission**
lets living particles drain; **Clear** removes them immediately. The scene's
global **Pause motion** also freezes FX. Example textures are small procedural
placeholders, not imported EmberGen simulations.

```cpp
#include "engine/effect_presets.hpp"

auto& fire = actor.addChild<engine::EffectComponent>("Campfire", engine::campfireEffect());
fire.transform.position = {0, 0.1f, 0};
fire.play();
// Call scene.update(deltaSeconds) once per frame, then render.
fire.pause();
fire.resume();
fire.stop();       // Drain; stop(true) clears.
fire.play();       // Restart with the definition's seed and retained pool storage.
```

`ParticleEmitterComponent` is usable directly or as an `EffectComponent` child.
An immutable, shared `ParticleEmitterDefinition` specifies a bounded pool, seed,
spawn rate, ordered timed bursts, duration/looping, randomized position/velocity/
lifetime/rotation, gravity, angular velocity, and piecewise-linear size/RGBA
curves over normalized particle age. Components have independent particle pools
and playback clocks. `play(false)` resumes the emitter clock; `play()` restarts.
Disabled ancestors freeze simulation and suppress rendering. Visibility masks
follow the same camera filtering as meshes. `setDefinition()` validates before
replacing the definition and restarting that emitter.

```cpp
auto smoke = std::make_shared<engine::ParticleEmitterDefinition>();
smoke->name = "Smoke";
smoke->texture = engine::Texture::loadMemory(pngBytes, "Smoke atlas");
smoke->flipbook = {8, 8, 64, 1}; // columns, rows, used frames, cycles per lifetime
smoke->blend = engine::BlendMode::Alpha;
smoke->spawnRate = 12;
smoke->velocityMin = {-0.1f, 0.4f, -0.1f};
smoke->velocityMax = { 0.1f, 0.8f,  0.1f};
smoke->size = {{0, 0.3f}, {1, 1.5f}};
smoke->color = {{0, {0.4f, 0.4f, 0.4f, 0.5f}}, {1, {0.4f, 0.4f, 0.4f, 0}}};
auto asset = std::make_shared<engine::EffectAsset>();
asset->name = "Smoke effect";
asset->emitters.push_back({smoke, {}}); // Each entry has its own local transform.
auto& smokeEffect = actor.addChild<engine::EffectComponent>("Smoke", asset);
```

Publish definitions/assets as `shared_ptr<const ...>` and do not mutate their
backing objects afterward. Multiple effect instances share definitions/textures
but retain independent playback. Add more emitter entries to layer smoke, sparks
and fire into one effect. Required emitter children cannot be removed independently;
remove the effect component through the normal deferred lifecycle. Effect assets
are shared C++ resources. The scene editor now provides [FX authoring](docs/fx-authoring.md),
curves/timing, isolated previews and versioned .wefx import/export; no JSON loader is included. The common component snapshot does not persist FX assets or live particles.

`ParticleSpace::Local` follows the component hierarchy, including size scaling.
`World` captures position, velocity and size scale when a particle spawns; subsequent
emitter movement leaves existing particles behind. Gravity uses the chosen
simulation space. Particles use pooled CPU records, analytic constant-gravity
integration, and a reused death-time heap to enforce capacity at subframe spawn
times. Existing particles advance once per update, rather than once per emitted
particle. Pool saturation drops emissions. A bounded per-update emission budget
also drops events after very large deltas or excessive rates.

The renderer uploads compact 80-byte particle instances and generates camera-facing
quads in WGSL. Compatible consecutive particles share instanced draw calls.
Alpha particles and transparent meshes sort together by camera-space center,
back to front; texture batching never changes that order. Additive particles draw
after alpha. Large intersecting transparent meshes can still need triangle sorting
or order-independent transparency; sorting uses mesh origins. FX use premultiplied
blending and the current renderer's display conversion. Optional HDR compositing and bloom are described in [lighting](docs/lighting-post-processing.md);
collision and fluid simulation are not included. Optional soft-particle depth fading,
shaped emission, drag/wind/gusts, vector motion curves and velocity-stretched
billboards are described in [particle FX contracts](docs/fx.md).

Flipbooks use top-left frame order, left to right then top to bottom. Grid cells
must divide image dimensions evenly and the frame count must fit the grid.
WGSL selects the frame from normalized age; cycles above one repeat playback.
Sampling uses a half-texel cell inset and mip level zero to avoid neighboring-frame
bleeding. This can alias at a distance; padded per-frame mips/texture arrays are a
future quality extension. Existing decoder size limits apply to imported atlases.
The **Import flame flipbook** controls apply a PNG and its grid settings to the
first emitter of the selected demo effect; invalid data preserves its previous
texture and particles. Selecting a preset restores its original definitions.

Performance JSON includes visible particles, particle draw calls, transparent mesh
draws, particle buffer growth and instance upload bytes. Unchanged paused instances
reuse their GPU buffer. FX draw/triangle counts and GPU payload are included in the
existing totals; GPU World timing includes FX. CPU frame storage includes sorted
FX queues/staging but excludes per-emitter pools and simulation scratch.

`node scripts/fx-smoke.cjs` checks real WebGPU pixels for alpha blending, world
occlusion, cross-emitter sorting, additive overlap, overlays, mesh opacity and
flipbook frames. It also exercises public controls, PNG import/error recovery,
paused upload reuse and responsive layout, saving screenshots and results in
`build/evidence/fx/`. It uses the same `NODE_PATH`, `BROWSER_EXECUTABLE` and local
server setup as the existing browser tests. Renderer fixtures are enabled only
with `?fxTest=1`; normal application pages cannot invoke them.

The renderer owns GPU buffers, pipeline, bind groups, and resize-dependent depth texture. Camera and lighting uniforms upload when changed; an instance vertex buffer carries individual model matrices, inverse-transpose normal matrices, and colors. GPU errors and device loss stop rendering and appear in the shell. The scene must outlive its renderer; cameras must outlive their entries in the renderer view list.

## Scene and asset saving

**Save scene** downloads a self-contained `.wescene` file; **Open scene** restores it.
Files include the complete built-in scene hierarchy, shared meshes/materials/textures,
camera views, lighting, morph weights, skeletal animation and root-motion state,
effects with live particles, and physics configuration/body velocities. The browser
also retains orbit controls, motion state and imported MP3/WAV audio with its position.
No original FBX, GLB, panorama or companion image is needed to reopen a save.

**Export assets** writes a reusable `.weassets` library; **Import assets** retains
its resources and adds mesh instances to the displayed scene. Opened scenes support
material/environment editing, animation/morph controls and further saving. **Return
to playground** restores the suspended original scene. The saved scene tree shows
actual loaded nodes, including disabled subtrees.

The portable `SceneArchive` API offers memory and native file save/load plus asset
library extraction. Files are versioned, checksummed and bounded; loading validates a
separate candidate before replacement, and native file writes atomically replace the
destination. Unsupported custom C++ node types fail explicitly. Application behavior,
GUI callbacks and physics solver internals need application-specific persistence.
Material graph results are saved as their material parameters and baked texture maps;
use the graph editor's JSON save/load for editable graph source.

See [format, API and scope](docs/scene-archives.md). `scene_archive_tests` verifies
round trips, malformed input, shared references and native files;
`node scripts/scene-archive-smoke.cjs` verifies physical downloads, exact WebGPU pixels,
embedded audio, fresh-page loading and failure recovery. Optional
`NATIVE_ARCHIVE_PATH` adds a native-to-browser compatibility check.

## Files

| Path | Purpose |
| --- | --- |
| `include/engine/math.hpp` | Column-major transforms, affine inverse, WebGPU projection |
| `include/engine/node.hpp`, `src/node.cpp` | Child ownership, inherited transforms, enabled subtrees |
| `include/engine/actor.hpp`, `src/actor.cpp` | Reusable actor object |
| `include/engine/component.hpp`, `src/component.cpp` | Base component object |
| `include/engine/mesh.hpp`, `src/mesh.cpp` | Shared geometry and mesh factories |
| `include/engine/mesh_optimizer.hpp`, `src/mesh_optimizer.cpp` | Lossless welding, cache/fetch ordering, GPU index packing |
| `include/engine/texture.hpp`, `src/texture.cpp` | Shared RGBA textures, image decoding, and limits |
| `include/engine/mesh_component.hpp`, `src/mesh_component.cpp` | Mesh instances, colors, and render-layer assignment |
| `include/engine/fbx_importer.hpp`, `src/fbx_importer.cpp` | Binary/ASCII FBX conversion and native file loading |
| `third_party/ufbx/` | Pinned parser, source provenance, and upstream license |
| `third_party/stb/`, `src/image_decoder.c` | Pinned image decoder and license |
| `include/engine/camera_component.hpp`, `src/camera_component.cpp` | Camera projections, cached matrices/frustum, viewport conversion and picking |
| `include/engine/light_component.hpp`, `src/light_component.cpp` | Directional and point light components |
| `include/engine/orbit_controller.hpp`, `src/orbit_controller.cpp` | Reusable camera orbit controls |
| `include/engine/scene.hpp`, `src/scene.cpp` | Scene root and render-frame collection |
| `include/engine/render_frame.hpp`, `src/render_frame.cpp` | Reusable draw/layer/light lists and instance staging |
| `include/engine/skeletal_mesh_component.hpp`, `src/skeletal_mesh_component.cpp` | Mesh, skeleton and animation component integration |
| `include/engine/skeleton_component.hpp`, `src/skeleton_component.cpp` | Joint hierarchy and per-instance GPU bone palette |
| `include/engine/animation_component.hpp`, `src/animation_component.cpp` | Playback, loop, stop, quaternion blending and crossfades |
| `include/engine/animation.hpp`, `src/animation.cpp`, `src/fbx_animation_import.cpp` | Shared animation data and FBX skin/clip conversion |
| `include/engine/renderer.hpp`, `src/renderer.cpp` | C++ WebGPU initialization, resources, ordered render layers |
| `include/engine/performance.hpp`, `src/performance.cpp` | Bounded CPU/GPU timing windows, counters, JSON summaries |
| `src/main.cpp` | Demo scene, animation, frame loop, exported input functions |
| `shaders/mesh.wgsl` | Directional/point diffuse lighting and tone mapping |
| `web/` | Browser shell, input, controls, startup errors, metrics |
| `scripts/` | Local SDK setup, builds, development server, optional browser checks |

Shader edits trigger CMake regeneration and rebuild the embedded shader. `build/web-debug/compile_commands.json` can be used by clangd or a C++ editor.

## Skeletal meshes and animation

Select a skeletal mesh in **Details → Animation library & state machine** to
organize named clips by folder/tags, import same-rig animations, author a visual
parameter/condition graph, and preview it on an isolated Play scene. Apply is
undoable; save `.weanim` for library organization/layout and save the scene for
the assigned controller/playback. See [animation authoring](docs/animation-authoring.md).

[Animation retargeting](docs/animation-retargeting.md) reuses clips across compatible
characters with different proportions and bind orientations. **Retarget clips**
loads FBX/GLB or `.weanim`, offers named joint mappings and translation scaling,
and bakes destination clips with undo, preview and reusable `.weretarget` profiles.
Try `?scene=retarget&editor=1` for the short blue and tall gold characters.

Optional [animation controllers and clip events](docs/animation-control.md) add
typed parameters, ordered state transitions, parameter-driven blends and queued
markers to the existing player. `consumeOutput()` drains owned events and root
motion together; gameplay owns marker responses and collision-aware movement.

[Inverse kinematics](docs/inverse-kinematics.md) adds weighted two-bone limb placement,
pole targets, optional tip alignment, bounded look-at and a physics ground-probe
helper. Constraints run after animation layers; each character owns its targets.
Try `?scene=ik&editor=1`, select **IK character**, and open the animation editor's
**Inverse kinematics** panel. Preview supports live target editing and diagnostics.

[Animation layers and blend trees](docs/animation-layers.md) add 1D speed and 2D
Cartesian direction samples, optional phase synchronization, and ordered masked
override/additive poses. Author them in the animation editor, drive weights with
Number parameters and use `restartLayer(name)` for one-shot overlays. Base
locomotion alone owns markers/root motion; scene and library saves retain the
composition.

`SkeletalMeshComponent` extends the texture/material/layer behavior of
`MeshComponent` and owns child `SkeletonComponent` and `AnimationComponent`
objects. Each component has its own header and C++ implementation. Immutable
meshes, skeleton definitions and clips can be shared; playback clocks and poses
belong to each component. Put skeletal mesh components under actors normally.

```cpp
auto& actor=scene.addChild<Actor>("Character");
auto& mesh=actor.addChild<SkeletalMeshComponent>("Animated mesh");
mesh.loadFbxFile("character.fbx"); // Native example; browser uses loadFbx(bytes).
mesh.animation().play(0,true,1.0);  // Clip, loop, playback speed.
scene.updateAnimations(deltaSeconds); // Call before renderer.render().
mesh.animation().blend(0,1,0.5f); // Two clips with independent duration wrapping.
mesh.animation().setBlendWeight(0.75f);
mesh.animation().crossFade(1,0.4); // Fade from current pose, including an interrupted fade.
mesh.animation().pause();
mesh.animation().resume();
mesh.animation().stop(); // Restore original bind pose and release clip playback state.
```

Root-motion extraction is opt-in and includes translation and quaternion rotation.
Choose the locomotion joint by its index in `mesh.skeleton().skeleton()->joints`;
imported rigs can have static FBX ancestors above that joint. Extraction anchors
the selected joint's position and orientation to its bind pose in mesh coordinates,
applies the same rigid correction to every hierarchy root, and keeps scale and
relative limb animation in the skin pose. Assets and sampled clips stay immutable.

```cpp
auto& player=mesh.animation();
player.enableRootMotion(rootJointIndex); // Default index is zero.
player.play(0,true);
scene.update(deltaSeconds);
const auto movement=player.consumeRootMotion(); // Translation + rotation; consumes once.
// For a mesh with identity placement beneath the actor and no physics controller:
actor.setWorldTransform(actor.worldMatrix()*movement.matrix());
player.disableRootMotion(); // Restore the normally animated root pose.
```

Gameplay consumes movement after the scene update and decides how to apply it,
including collision/physics handling. The delta is in the skeletal mesh's local
coordinates, before its component placement; convert through that placement when
it differs from the actor's coordinates. Applying a rotation beneath nonuniform
scale can require shear, which `setWorldTransform` rejects. Extraction itself
does not move actors. Pending deltas compose in chronological order across updates;
`rootMotionDelta()` inspects them without consuming. Turning loops accumulate their
endpoint transform, including multiple loops in one update, instead of emitting
the wrap back to frame zero. Non-looping clips extract only to their endpoint.
Zero-duration clips, paused playback and disabled scene subtrees emit no movement.

Play, blend, seek, blend-weight changes, crossfade, stop and extraction toggles clear
pending movement, so playback controls never emit a teleport. Seeking rebases the
next update at the requested time. Stop keeps the extraction setting for the same
rig; replacing the skeleton clears it on the next update or playback configuration.
Blending interpolates each clip's interval delta, with independent duration wrapping.
Crossfades retain the existing frozen outgoing pose and weight target movement by
the average fade weight over the update, including updates past the fade endpoint.
As with pose blending, blended/faded rotation uses the shortest quaternion arc;
use normal frame-sized updates for blended movement. Unblended loop extraction is
independent of update subdivision. Warm extraction updates allocate no heap blocks.

The browser inspector has clip selectors, play/pause/stop, loop and speed settings,
a blend slider, and a crossfade duration/target. Settings for loop/speed apply
when starting playback/blending. **Load animated FBX demo** imports the original
two-bone column with two opposing bending clips. Open `?scene=skeleton` to load
it directly. The usual FBX picker also accepts animated ASCII/binary FBX files
and their companion images. Failed imports retain the current working scene.
Non-loop playback holds the final pose; stop restores bind pose. Global **Pause
motion** also freezes animation updates.

FBX import retains ancestor hierarchy, mesh-to-bone bind matrices, normalized
four strongest skin weights, and named animation stacks. It bakes evaluated
local TRS poses (including FBX pivots, animation layers and transform inheritance)
at 30 Hz, with both endpoints, then discards parser data. At runtime C++ samples
poses, blends translations/scales and shortest-arc quaternion rotations, resolves
the hierarchy, and builds bone matrices. **Vertex deformation and normal skinning
run on the GPU** in browser WebGPU and native Dawn/direct D3D12, using a shared
immutable joint/weight vertex stream and a
read-only bone storage buffer. Skeletal-only animation needs no per-frame CPU vertex
deformation or mesh upload. Active morph targets use the cached CPU deformation
path described below. Actor/component placement happens after skinning; preview fitting
keeps bind-space geometry intact. Each palette uploads only when changed.

The skin pipeline uses 14 vertex attributes and three vertex streams, within
baseline WebGPU limits. Static meshes retain their existing pipeline and batching.
Skeletal draws batch only when they share the same pose palette; independently
animated components keep separate draw submissions. Mesh optimization welds and
remaps all skin attributes together with positions/normals/UVs.

Import limits are 256 hierarchy joints, 1,024 bindings (including identity), 64
clips and 32 MiB sampled poses, in addition to existing geometry/parser limits.
`FbxImportOptions::animationSampleRate` (1–120 Hz) and `maxAnimationBytes` are
configurable. More than four influences are reduced and reported as a warning;
dual-quaternion skins use linear skinning with a warning. Multiple skin deformers
on one mesh, inconsistent shared-bone bind poses and sheared local joint transforms are rejected.
GPU clip evaluation is not implemented.

### Morph targets

`Mesh::morphTargets` stores named additive mesh-local position offsets and optional
normal/tangent offsets. Position streams must match the vertex count; empty normal
or tangent streams preserve the base attributes. Publish the mesh as an immutable
shared resource, then control weights independently on each `MeshComponent` or
`SkeletalMeshComponent`:

```cpp
auto asset = std::make_shared<Mesh>(*Mesh::cube());
MorphTarget stretch;
stretch.name = "Stretch";
for (const auto& vertex : asset->vertices)
    stretch.positions.push_back({0, vertex.position.y > 0 ? 1.0f : 0.0f, 0});
asset->morphTargets.push_back(std::move(stretch));
auto& object = scene.spawn("Morph object").addComponent<MeshComponent>("Mesh", asset);
object.setMorphWeight("Stretch", 0.75f); // Index overload also available.
object.resetMorphWeights();             // Restore the resource's default weights.
```

Finite negative weights and extrapolation above one are allowed. Targets add to the
base vertices without accumulation across updates. Normals are normalized and tangents
orthogonalized, retaining tangent handedness. Set weights from gameplay/animation ticks
to animate shapes. Mesh replacement resets weights when next accessed or collected.
Malformed streams and nonfinite weights are rejected; finite weights that overflow
vertex data are rejected when evaluating the pose, preserving the previous pose buffer.

Morphing currently runs on the CPU when weights change, then uploads a cached vertex
stream. Unchanged weights perform no deformation or vertex upload. Zero weights use
the shared base GPU mesh and retain ordinary static instancing. Active instances keep
separate pose buffers; indices, materials and skin weights stay shared. Deformation
precedes GPU skinning in color, transparent and shadow passes. Culling uses matching
morphed/skinned bounds. Optimization and tangent seam splitting retain all target
attributes. Scene archives retain target assets and per-instance weights.

FBX ASCII/binary imports expand sparse shape offsets over every converted corner,
apply geometry transforms and meter conversion, preserve default effective weights,
and derive tangent offsets. Missing FBX normal offsets preserve base normals. Node
instances receive separate named targets; duplicate names gain numeric suffixes.
FBX in-between key shapes are exposed as independent targets with their default
effective weights and a warning. Automatic progressive-channel interpolation and
FBX morph-weight animation playback are not implemented; animated channels report
a warning. Import limits default to 128 targets and 64 MiB decoded offset streams,
configurable through `maxMorphTargets` and `maxMorphBytes`.

The inspector exposes imported target sliders and **Reset weights**. **Load morph
demo**, or `?scene=morph`, loads the original two-shape column. Native `morph_tests`
cover instance isolation, normals/tangents, bounds, skinning, welding/seams, ASCII/
binary import, conversion, budgets, replacement and archive round trips.
`node scripts/morph-smoke.cjs` exercises actual WebGPU color/shadow pixels, sliders,
combined shapes, transparency, skinning, culling and unchanged-buffer reuse.

The panel/JSON expose skinned instances, cached palettes, bone write calls/bytes
and `gpuSkinBytes` (a subset of `gpuBufferBytes`). CPU asset payload includes skin
vertex streams but excludes skeleton/clip metadata and pose scratch buffers.
Native tests cover root-motion extraction and consumption, turning loops, scaled
ancestors, transitions and allocation-free updates, plus binary/ASCII import, bind pose, blending/fades, loop/stop,
disabled subtrees, failure recovery, and weight-preserving optimization; 1,000
warm animated updates/collections allocate zero ordinary heap blocks.
`node scripts/skeletal-animation-smoke.cjs` verifies real WebGPU deformation,
exact blend endpoints, bind-pose stop, playback/fade controls and buffer release.

## Replay and ghost recording

Open **`?scene=replay`** to record a driving session, review poses/telemetry/events
on a scrubbable timeline and race its non-colliding ghost. The Formula demo and
native Windows vehicle app use the same portable capture/playback policy, with
`.weghost` files shared across compatible scenes. Review pauses live physics;
recorded commands are diagnostics and are not rerun through the solver.
See [workflow, APIs and limits](docs/replay-recording.md).

## Cinematic sequencer

Open **`?scene=cinematic`** to author camera shots, object motion, skeletal
animation, sound and FX together on a scrubbable timeline. Drag clips/keys, edit
exact values, undo/redo and save `.wecin` assets. Isolated preview preserves the
source scene; scrubbing and frame stepping are silent. The sequencer panel also
works with scenes in the editor. See [usage, APIs and limits](docs/cinematic-sequencer.md).

## Audio system

The reusable C++ `AudioSystem` provides shared immutable sounds, simultaneous
voices, loops, variable playback rate, hierarchical buses, volume/mute controls, interruptible fades and
scene-handle-based positional emitters/listeners. The optional browser backend
uses Web Audio with explicit activation and host suspension. Open **Audio system**
in the inspector for overlapping effects, looping music, bus controls, fade-out,
positional playback and local MP3/WAV sound imports.

See [audio contracts, usage and validation](docs/audio.md) for lifetime rules,
PCM decoding, frame ordering, browser activation and persistence scope, and
[environmental audio](docs/environmental-audio.md) for wall muffling, room reverb
and streamed music. Open `?scene=audio` for comparisons and local music streaming.

The vehicle course also provides **Driving audio and rate preview** in Details:
RPM/load loops, contact-slip/gravel and impact examples, isolated preview and
separate saved tuning. See [driving audio](docs/driving-audio.md).

## MP3 and WAV audio

The inspector's **Import audio** loads one local `.mp3` or `.wav` without uploading it.
**Play** starts at the selected position, **Pause** holds it, and **Stop** silences
playback and returns to zero. Click or drag the waveform, or use the position
slider, to seek while stopped, paused or playing. The waveform supports arrow
keys (one second, or ten with Shift), Home and End. The playhead and elapsed /
total time follow the browser's audio clock; reaching the end holds the final
position, and Play starts again from zero. Scene motion controls do not pause audio.

`web/audio.js` provides the reusable `EngineAudioPlayer`; `web/audio-panel.js`
binds the inspector and exposes its instance as `window.EngineAudio`:

```js
const player = new EngineAudioPlayer();
await player.loadFile(file); // A File named .mp3 or .wav; decode failure preserves the current track.
player.setPosition(12.5);    // Seconds, clamped to [0, duration].
await player.play();        // Call from a user gesture to allow browser audio output.
player.pause();
player.stop();
console.log(player.snapshot()); // Name, state, position, duration, channels, loading, contextState.
await player.dispose();     // Stops playback, releases samples, closes its owned AudioContext.
```

The player uses the browser's [Web Audio decoder](https://developer.mozilla.org/en-US/docs/Web/API/BaseAudioContext/decodeAudioData)
and retains decoded samples for playback and waveform peaks across all channels.
Imports are limited to 32 MiB encoded, 15 minutes and 128 MiB retained PCM.
The browser allocates the full decoded buffer before the latter limits can be
checked; this is intended for bounded clips rather than streaming large music
libraries. Successful replacement stops the previous track; failed replacement
preserves it. Concurrent imports keep the newest request, and Stop/Pause cancel
a pending Play. No C++ decoder dependency or Wasm audio copy is required.
An optional `{ context }` constructor argument uses an existing AudioContext
without closing it on disposal. `change` events report load/playback transitions;
read `position` for the live clock. `peaks` contains interleaved minimum/maximum
values for up to 512 waveform bins.

`node scripts/audio-smoke.cjs` uses the same browser/dependency setup as the
other smoke checks. It imports original stereo MP3 and PCM WAV tones and checks waveform
rendering, pointer/keyboard/range seeking, nonzero Web Audio output, pause/stop
silence, completion/replay, malformed-import recovery, pending-play cancellation,
concurrent-import ordering, disposal and mobile layout. Evidence is saved to
`build/evidence/audio/`. These checks establish browser audio output samples;
speaker/headphone audibility remains a manual device check.

## Instanced rendering

Instancing is enabled by default for shared mesh components, including imported
FBX meshes. Each ordered layer groups consecutive draws with the same mesh,
index range, effective material binding, skin pose and mirrored winding. Transforms, inverse-transpose normals and
tints remain individual instance attributes. Texture/scalar overrides, render state and material
ranges split batches. Draws are never sorted across intervening resources or
layers; noncontiguous object slots also split a batch to preserve original order.

`MeshComponent::allowInstanceReordering` is an opt-in runtime batching hint for
opaque objects whose equal-depth precedence may change. Contiguous opted-in runs
within a layer group matching parts/materials into contiguous GPU instance slots;
the original `SceneFrame` indices remain intact. Use `RenderFrame::instanceDraw()`
to resolve a GPU slot to its draw. Order-sensitive and transparent components end
each run. This runtime hint defaults to false when loading a scene archive.
The ship stress fleet enables it: 100 and 1,000 fully visible Knarr ships each
use seven color draws plus seven draws per shadow map. The scene totals are
10 color-only draws or 17 with one shadow map. Geometry counts remain unchanged
when detail settings are fixed. `scripts/ship-instancing-smoke.cjs` compares both
fleet sizes with instancing disabled, including exact rendered pixels and shadows.

The GPU reads the existing reusable 256-byte object slots as an instance vertex
buffer (160 bytes of attributes). `DrawIndexed` supplies the batch's instance
count and first-instance slot. Changed-only uploads, resource sharing and cached
transforms continue to work; batching does not duplicate mesh geometry.
`Renderer::setInstancingEnabled(false)` disables batching; the browser accepts
`?instancing=off` for a visual/submission comparison.

The performance panel and JSON distinguish actual `drawCalls` from
`submittedInstances`, `instancedDrawCalls` and `maxInstancesPerDraw`. Triangle
counts include every instance. The Knarr demo submits **11 draws instead of 46**
(46 instances, 41,414 triangles); restoring the cube demo submits **3 instead of
45** (45 instances, 530 triangles). `node scripts/instancing-smoke.cjs` compares
exact canvas pixels with batching on/off, including rotated nonuniform instances
and point-light visibility changes. These are submission reductions, not FPS claims.

## Performance measurements

The [detailed CPU timeline](docs/profiling-timeline.md) records nested physics,
animation, AI, loading and rendering work, with inclusive/self timings, slow-frame
selection, filtering and trace export. Open **Performance → Detailed CPU timeline**,
or use `?scene=physics&timeline=1`. Capture is optional and off by default.

The renderer retains a `RenderFrame` with reusable scene draw/light vectors,
per-layer index/batch lists, and instance staging bytes. `Scene::collect(frame)`
clears previous references and refills caller-owned storage while preserving
capacity; the existing value-returning `collect()` remains available. Each layer
visits its own instance batches once, preserving traversal/material order and global
instance-slot addresses. Only changed active instance ranges are uploaded. Buffers grow
when needed and retain their peak capacity through smaller/empty scenes and
measurement resets. Old mesh/texture references are released before GPU cache
cleanup so frame reuse does not keep obsolete assets alive.

**Timing and memory details** shows reserved CPU frame storage and CPU/GPU growth
events for the current frame and since reset. CPU growth counts vector capacity
changes in scene draws, lights, layer lists, object staging and dirty ranges; GPU growth counts
instance buffer creations/replacements. These scoped counters do not count
all heap or WebGPU allocations. Command encoders, render passes and swapchain
views still follow WebGPU's per-frame lifecycle. Native tests count zero heap
allocations across 1,000 warm collections and instance preparations, verify live
transform updates, and exercise growth, empty scenes, layer changes and release.
`node scripts/frame-reuse-smoke.cjs` verifies stable capacities over 120 browser
frames, grows to 159 material draws, restores/reimports without further growth,
and checks active upload sizes and asset release. Optional `FRAME_BASELINE` is a
pre-change deterministic Knarr canvas PNG for exact pixel comparison; use the
same browser and viewport. The check measures capacity reuse and rendering correctness.

Nodes cache local/world matrices and inverse-transpose normal matrices. Exact
snapshots of public `Transform` fields detect direct writes; changes to a parent
world matrix refresh descendants while unrelated subtrees retain their caches.
Normal matrices are computed once per mesh component, shared by its material
draws. The camera reuses its inverse and caches projection by FOV, aspect and
clip planes. Scene traversal and public `worldMatrix()` calls share caches.

Object staging compares all 160 instance-attribute bytes (model, normal, tint, shadow receiving) against
the prior contents of each draw slot. This handles changed transforms/tints and
slot changes after visibility, material-count or scene changes. Adjacent dirty
slots are coalesced; a write includes padding between slots but omits the final
96 padding bytes. Recreating the GPU object buffer forces all active slots to
upload, including unchanged objects. Camera/light uniforms upload only when their
1,136 bytes change. Mesh/texture bindings still update when resources change even
if the object's instance data stays identical. Resetting measurements preserves caches.

**Timing and memory details** and JSON report object slots/writes/bytes, separate
camera/light writes/bytes, and local/world/mesh-normal rebuilds during collection.
Camera inverse work outside collection is excluded from those rebuild counts;
per-node cache metadata is excluded from payload estimates. Native tests cover
direct writes, parent propagation, nonuniform normals, tints, sparse ranges,
visibility, lens changes, invalid-transform recovery, and buffer invalidation.
`node scripts/cache-upload-smoke.cjs` verifies real WebGPU output and writes using
a controlled animation clock. On Knarr, steady animation uploads 160 object bytes
for the moving marker plus 1,136 camera/light/environment/shadow bytes: **1,296 bytes/frame**. The rotating cube demo uploads seven adjacent moving slots in
1,696 object bytes plus 1,136 globals: **2,832 bytes/frame**. Static/paused scenes
perform zero uniform writes and no scene matrix rebuilds; camera orbit/reset
updates only globals. The controlled-clock checks establish upload reduction
and rendering correctness, not an FPS benchmark.

Direct D3D12 now provides [detailed GPU profiling](docs/windows-gpu-profiling.md):
fenced timestamp samples, nested pass durations, rolling statistics and on-demand
Perfetto/Chrome trace export. Use `WEBENGINE_GPU_TIMING=0` for native profiling-off
comparisons; browser WebGPU retains `?gpuTiming=off`.

### Screen-size mesh detail and shadow budgets

The fleet demo prepares three shared static mesh LODs once per asset. The renderer
selects them by projected bounds in each camera viewport, with 10% hysteresis.
Distance, object scale, FOV and orthographic cameras are accounted for. Shadow
maps choose detail independently in texels and cull casters outside each light's
volume while retaining off-camera casters. Near-plane intersections retain full
camera detail. Animated meshes retain their original geometry.

Expand **Hansa ship stress test → Mesh detail and shadows** to compare full versus
automatic camera/shadow detail, casting versus receiving, 256–2048 shadow-map
resolution, and one versus nine comparison samples. **Sun shadows** disables all
shadow work. Controls reset timing windows; pause motion and keep camera/count
fixed for comparisons. The default is 1024 and nine samples.

Engine callers can prepare a mutable static asset with
`MeshOptimizer::generateLods(mesh)` before publishing it as `shared_ptr<const Mesh>`.
The original and LOD levels retain shared material resources. LOD buffers add fixed
asset memory, not one copy per ship. Derived LOD caches are not serialized by the
current archive format. [Implementation, profiling and validation](docs/mesh-detail-validation.md)
include the reusable API contracts and test procedure.

The inspector's **Performance** panel reports rolling mean, nearest-rank p95, and (in JSON) maximum durations across the latest 120 submitted frames. It shows frame interval separately from CPU engine work. Expand **Timing and memory details** for simulation/host update, scene collection, preparation/uploads, command encoding, and CPU queue-submission time. The CPU total includes renderer bookkeeping and resizing, but excludes the inspector's aggregation/display work and GPU execution. Frame intervals include browser scheduling and synchronous import stalls; the first callback and callbacks after a visibility change have no interval sample.

Draw calls, submitted instances/triangles, passes, actual queue-write calls/bytes, resource cache counts, and memory estimates are collected from the render path. Shared resources are counted once. Instance uploads occur only when their shader data changes or the GPU instance buffer is recreated; camera/light uniforms upload when changed. CPU asset payload counts vertex/index buffers and retained decoded image pixels. GPU payload estimates include mesh/instance buffers, all mip levels, uniforms, timing readback buffers, and nominal four-byte depth pixels. They exclude surface images, object/allocator overhead, query-object storage, temporary import allocations, and driver overhead. Wasm capacity is the allocated linear-memory buffer, not live heap usage.

If the adapter supports `timestamp-query`, the engine requests it and measures each layer and shadow map render-pass duration. Sampling is limited to 10 Hz with three asynchronous readback slots; rendering never waits for a slot or mapped results. GPU statistics use a separate 120-sample window (up to roughly 12 seconds), expose sample age, and sum the color layers and directional shadow passes across active views. They exclude GPU transfers, queue waiting, presentation, and profiler readback. Browser timestamp precision may be quantized. Unsupported/disabled timing is reported as unavailable with `null` durations, not zero. Add `?gpuTiming=off` to disable GPU sampling for an instrumentation-overhead comparison.

**Last asset load** records successful and failed import attempts, including file read/fetch, copy into Wasm, model parsing, image decoding/copying, geometry/material conversion, lossless mesh optimization, and preview fitting. Vertex counts before/after optimization are included. Import wall time excludes GPU resource creation on the following render frame; **Last resource preparation** retains the most recent new-resource CPU preparation duration, including mip generation and queue writes. The C++ importer can fill caller-owned `FbxImportMeasurements` via `FbxImportOptions::measurements` in native or browser builds.

The inspector refreshes at most four times per second. **Reset samples** clears CPU/GPU windows and cumulative upload counters while retaining cached resources; pending GPU results from before the reset are ignored. **Export JSON** downloads the current summaries, build mode, browser, scene, measurement definitions, up to 240 inspector snapshots, and the latest 20 asset attempts. The same report is available programmatically with `window.EnginePerformance.snapshot()`.

For comparisons, use the same Release build, viewport, browser/GPU, scene, camera, and light settings. Let asset uploads finish, reset samples, then capture a stable run. Include cold-load measurements separately when testing importer changes. Debug builds include runtime checks and should be compared separately.

`scripts/performance-smoke.cjs` checks exact Knarr submission/cache/upload counts and mip payload, rolling statistics, asset timings, asynchronous GPU samples, unavailable/disabled GPU fallback, reset, resource release, failed imports, and a physically saved JSON export. Use the same `NODE_PATH`, `BROWSER_EXECUTABLE`, and local server setup as the browser acceptance script below. Optional `EVIDENCE_DIR` selects its output directory.

## In-engine GUI

Open **In-engine GUI demo** or `?gui=1` for C++ labels, a text input, image,
buttons and panels drawn directly inside the WebGPU canvas. The retained widget
tree provides absolute/anchored and horizontal/vertical layout, nested clipping,
pointer capture, keyboard focus, Tab navigation, a font atlas and single-line
text editing through an invisible browser textarea service. GUI gestures are
consumed before camera/game input. The normal playground keeps its HTML inspector.

The portable API is in `include/engine/gui.hpp`; browser integration is in
`include/engine/gui_browser.hpp`. See [GUI usage and architecture](docs/gui.md)
for examples, packaging, performance accounting and the first-version text scope.
`tests/gui_tests.cpp` runs with the native core suite;
`scripts/gui-smoke.cjs` checks actual canvas pixels and browser interaction.

Open `?gui=1&guiSettings=1` for a settings preview with `Checkbox`, a stepped
horizontal `Slider`, and an application-defined painted/interactive widget.
Portable widget hooks share system-owned clipping, scaling, focus and capture;
native browser checkbox/range semantics share the C++ values. See
[custom GUI widgets](docs/gui-widgets.md) and `scripts/gui-widgets-smoke.cjs`
for usage and WebGPU/WebGL 2 acceptance.

Open `?gui=1&guiTheme=1` for dark/light theme switching, component defaults, local
overrides and hovered/focused/pressed/checked/disabled appearances. Shared
`GuiTheme` values and partial widget state styles also reach custom painters.
See [GUI themes](docs/gui-themes.md) and `scripts/gui-theme-smoke.cjs` for
portable APIs and both-renderer/DPR acceptance.

Open `?gui=1&guiLayout=1` for minimum/maximum size constraints, content-sized
text/nested panels, group/cell alignment and a two/three-column grid. Layout is
staged before bounds publication and shares clipping, scrolling and input across
both renderers. See [measured GUI layout](docs/gui-layout.md) and
`scripts/gui-layout-smoke.cjs` for usage and acceptance.

## Hansa ship stress test

Use **Hansa ship stress test** at the top of the demo inspector. Enter an integer
from 0 to 10,000 and press **Render ships**, or choose a preset. The Knarr ships
form a centered grid across the ground, scaling to fit without overlapping. The
camera frames the fleet. **Return to scene** restores the previous imported model.
Opening Physics, Materials, importing another FBX, or restoring the starter scene
clears the fleet.

All copies share the bundled Hansa Knarr mesh and textures. The existing FPS and
Performance panels report rendering costs; JSON exports include the fleet count.
Large counts intentionally stress the GPU, especially with shadows enabled.
`tests/ship_stress_tests.cpp` covers layout, counts up to 10,000, invalid-count
recovery and cleanup. `scripts/ship-stress-smoke.cjs` checks browser controls,
actual canvas pixels, shared GPU assets, submissions and scene transitions using
the same Playwright environment as the other browser smoke scripts.

## Game AI

Optional `engine_ai` provides authored waypoint graphs, static collision-baked
navmeshes and bounded A* search,
command-based path following, capsule clearance and bounded automatic obstacle
recovery, handle-safe sight queries and a simulation decision clock. Games own
goals, state/memory/action rules, ground support/access and cancellation.
Open `?scene=ai` or **Game AI → Open AI demo** to try patrol, target following,
last-known-position investigation, movable blockers, alternate routes, unreachable
goals and retry. See
[AI contracts](docs/ai.md) and [recovery validation](docs/ai-recovery-validation.md).
Open `?scene=navmesh` for arbitrary click-to-move destinations, walkable polygons
and full planned paths. See [navmesh contracts](docs/navmesh.md) and
[validation](docs/navmesh-validation.md). Versioned `NavigationSource` invalidation
stops active routes when collision changes; explicit revision-checked rebaking
replans original goals without resetting deadlines or retry limits. The demo
includes passage and floor-bridge controls. See
[navigation update validation](docs/navigation-updates-validation.md).
Optional **Crowd & changing obstacles** adds coordinated passing and tracked-body
footprint rebakes over those same navigators and motors. Open `?scene=crowd` for
head-on passing, eight opposing characters and a movable gate. See
[crowd/dynamic navigation contracts](docs/crowd-navigation.md) and
[validation](docs/crowd-navigation-validation.md). Incremental/worker rebuilding
and built-in AI runtime checkpoints remain extensions.

Reusable `StateMachine` instances execute immutable state/transition graphs
with named game behavior/condition bindings, explicit enter/update/exit and bounded
work. The AI demo uses this runtime. In **Scene editor → Details → AI state
machines**, open the graphical editor to drag/connect states, edit guards/delays,
set entry and priority, undo/redo, save/open `.weai` assets and preview the actual
runtime independently of the scene. **Use in AI demo** runs supported authored
bindings. See [state machines and editor](docs/ai-state-machines.md). Actor-bound
`AiControllerComponent` assignments add typed binding forms, scene save/load,
codec-aware Play reconstruction and a live state/time/transition debugger. See
[actor AI controllers](docs/ai-controllers.md).
Graphs can declare typed per-agent blackboards; runtime updates preserve active
state/timers, parameter sources bind live inputs, and the Play debugger watches
values and transition guards. See [AI blackboards](docs/ai-blackboards.md).
[Hierarchical states](docs/ai-hierarchy.md) add nested groups, parent interrupts,
per-ancestor timers, collapsible graph editing and active-path debugging.
[Typed gameplay events](docs/ai-events.md) add bounded queues, event-triggered
guards, state-scoped cancellation and editor/Play injection with payload traces.
[State task sequences](docs/ai-tasks.md) add reusable wait, event-wait, navigation and facing tasks, application task factories, cancellation/timeouts and graphical sequence editing with live Play diagnostics.

## Physics

Optional [movement controllers](docs/movement.md) provide a device-independent
character motor with grounding, slopes, steps, jumping, wall sliding and per-step
movement results. Collision-free XYZ movement is also available.

Jolt Physics runs inside the C++/WebAssembly engine with reusable rigid-body and
collider components, a scene-owned physics world, fixed steps, render
interpolation, collision events/layers, forces/impulses, filtered raycasts,
convex-shape overlaps/sweeps, and sensor enter/exit events. Use
the optional [physics joint APIs](docs/physics-joints.md) for hinges, sliders,
damped distance springs and movable point grabs with world-owned handles and
automatic endpoint cleanup. Use
**Physics playground** in the inspector or open `?scene=physics` to try falling
boxes/spheres, click/tap impulses, and collider outlines. See
[Physics usage and architecture](docs/physics.md) for the API, ownership contract,
dependency setup, and verification commands. The first CMake configure downloads
pinned Jolt 5.3.0; the browser remains single-threaded.

## Verification

`tests/camera_tests.cpp` covers validated perspective/orthographic lenses, WebGPU depth endpoints, pixel/world round trips, offset viewports, picking rays, all six frustum planes, sphere/AABB tests, parent-aware aiming, cache revisions, invalid-input recovery, and mesh/light visibility masks. `tests/render_view_tests.cpp` and `scripts/compositing-smoke.cjs` cover multi-view compositing.

Native core tests need a C++20 compiler, CMake, and Ninja; no SDK or GPU is required:

```sh
cmake -S . -B build/native -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build/native
ctest --test-dir build/native --output-on-failure
```

Tests cover nested transforms, scene/actor/component ownership, disabled subtrees, shared meshes, triangle winding, inverse-transpose normals, parented lights, and camera projection/orbit behavior. Importer tests add binary and ASCII fixtures, instancing, units/axis conversion, geometry offsets, mirrored scales, missing-normal generation, malformed indices, singular transforms, import limits, native path loading, and preservation of component state on failure. Texture tests cover image orientation, UV seams, shared embedded images, external resolution, material ranges/tints, resource overrides, image limits, corrupt-image recovery, and Knarr's seven textured materials. A default native build provides the core and tests. The opt-in Windows target also renders the vehicle proof through SDL3/Dawn; see [native Windows](docs/windows-native.md).

`scripts/browser-smoke.cjs` is an optional acceptance check using Playwright and pngjs. Supply those packages through `NODE_PATH` or install them in a separate test environment. With the development server running and a WebGPU-capable Chrome/Edge installation:

```powershell
$env:NODE_PATH = 'path/to/test/node_modules'
$env:BROWSER_EXECUTABLE = 'C:/Program Files/Google/Chrome/Application/chrome.exe'
node scripts/browser-smoke.cjs
```

It checks real canvas changes for orbit/zoom/reset and both light toggles, frozen animation while paused, binary/ASCII FBX uploads, mesh replacement and repeated imports, import-error recovery, demo restoration, layer submission, responsive resizing, startup errors, and console/GPU validation errors. Texture checks verify that Knarr's seven images affect rendered pixels, overrides/reloads preserve geometry, embedded and companion versions render identically, missing-image fallback is visible, and corrupt images preserve the current scene. Screenshots and JSON results go to `build/evidence/`. This is a scoped test of that browser/GPU, not a browser compatibility matrix or performance benchmark.

GitHub Actions builds native core tests and a release browser artifact. Browser rendering acceptance is separate from CI compilation.

Scene-wide exponential height fog is optional on both browser backends, with per-view opt-out, material/particle distances and archive support. Use **Height fog** in the playground. See [fog settings and composition](docs/fog.md).


### Ribbon trails

FX now supports bounded camera-facing ribbon trails on WebGPU and WebGL2, including
local/world paths, lifetime width/color curves, texture repetition, breaks, fog,
soft depth fades and archive v19 continuation. In Scene editor -> Details ->
Particle FX choose **Ribbon trail**, then **Open preview** for isolated source motion.
See [API and limitations](docs/trails.md) and [validation](docs/trails-validation.md).

Driving Stage 6 adds optional surface-following contact strips. Applications supply
world positions/normals; the engine owns bounded distance sampling, gaps, fading
and camera-independent geometry on WebGPU/WebGL2. Particle FX -> **Surface trail**
provides immutable authoring and isolated sloped preview. Archive v28 retains
definitions and bounded live continuation. See [contracts](docs/surface-trails.md)
and [validation](docs/surface-trails-validation.md).


### Ground-aligned particles

Use `ParticleAlignment::Plane` with `planeNormal` and `surfaceOffset` for ground rings,
sloped contact flashes and wall-aligned effects. Existing flipbooks, curves, rotation,
fog and soft fading work on both renderers. In Scene editor -> Details -> Particle FX,
choose **Ground ring**, or set Appearance -> Alignment to Plane / supplied normal.
[API and limitations](docs/plane-particles.md) · [Validation](docs/plane-particles-validation.md).
Ribbon state began in v21; current scene/effect/application archives use v34; host and worker codecs must match.


### Mesh particles

Emit shared static meshes for debris, leaves and fragments with
`ParticleRenderMode::Mesh`, `particleMesh`, seeded 3D rotation and spin. The existing
mesh renderer supplies materials, lighting, fog, opaque instancing and transparent
ordering. Scene editor -> Details -> Particle FX -> **Mesh debris** previews the
feature; import GLB/FBX geometry through Assets and select it under Mesh particles.
[API and limits](docs/mesh-particles.md) · [Validation](docs/mesh-particles-validation.md).
Animated-particle state began in v23; current archives use v34, including matching host/worker codecs.

### Particle collision and projected decals

Enable an emitter's Collision settings for bounded sphere sweeps with Bounce,
Slide, Kill or Settle responses. Consume its queued impacts in application code.
DecalComponent projects textures onto opaque scene triangles using a clipped box
volume; floors, slopes and terrain chunks can receive persistent marks. Scene
structure can add a decal, Details edits its properties/texture, and transforms
control the projector. FX previews have an isolated collision floor.
[API and limits](docs/fx-contacts-decals.md) · [Validation](docs/fx-contacts-decals-validation.md).
Configuration and settled state persist; queued impacts and projection caches are transient.


### Wheeled vehicles

Open `?scene=vehicle` for the handling course or `?editor=1&scene=vehicle` to author
it. Optional VehicleComponent supports a four-wheel chassis, suspension, automatic
drivetrain, brakes, commands/telemetry and interpolated wheel visuals through Jolt
on both WebGPU and WebGL 2. Editor properties, undoable presets, duplication, isolated
Play and v24 scene/application persistence use existing engine paths.
[Vehicle API, defaults and limits](docs/vehicles.md) · [Validation](docs/vehicles-validation.md).


Driving Stage 2: [physical surfaces, compound collision and impact observations](docs/physical-surfaces.md),
including editor history, archive v25 and WETP3 persistence. See
[validation](docs/physical-surfaces-validation.md) and use
`?scene=vehicle&surfaceTest=1` for the three-lane application fixture.


Driving Stage 3 adds [optional gamepad input and composed follow cameras](docs/driving-controls.md),
including typed camera authoring, isolated preview controls and archive v26.
Use `?editor=1&scene=vehicle`; see [validation](docs/driving-controls-validation.md).

## Splines and procedural routes

Optional immutable Hermite splines support arc-length sampling and bounded nearest
queries. Configurable cross-section sweeps provide transported/banked frames,
distance UVs, bounds and optional static collision shared by both renderers.
Details -> **Splines and swept routes** offers grouped point/tangent/profile edits,
snapping, surface selection, duplication, collision preview and explicit terrain
point sampling. Scene v27 preserves source resources/sharing and rebuilds geometry
under budgets. Open `?editor=1&scene=route` for the banked driving example.
See [contracts and validation](docs/splines-routes.md).

### Event-driven FX composition

Particle FX now includes **Impact chain** and **Event composition**: Birth/Impact/Death child effects, optional fading projected decals, shared spawn/active/depth limits and isolated previews. Existing effect history, .wefx and scene persistence retain the complete graph through archive v29. See [API and ownership](docs/fx-composition.md) and [validation](docs/fx-composition-validation.md).


Driving Stage 7 adds spotlights with optional shadows on WebGPU/WebGL2, sharing eight light
slots per view. Light type/direction/range/half angles, selected cone outlines,
history/Play and v30 persistence are integrated in the editor. Open
`?editor=1&scene=spotlight` for the headlight example, or `?scene=lighting` to compare
shadowed headlights, a torch and cascaded sunlight with HDR bloom/color grading. See
[spotlight contracts](docs/spotlights.md) and [validation](docs/spotlights-validation.md).

Driving Stage 8 adds optional general static region streaming: immutable manifests,
separate visual/collision readiness, staged publication, bounded admissions and body
pins, editor history/bounds/source files and isolated vehicle traversal preview.
Archive v31 saves authored references and selected restoration requirements, omitting
resident caches. Open `?editor=1&scene=regions`; applications own loaders, predictive
movement gating and restore handoff. See [contracts](docs/region-streaming.md) and
[validation](docs/region-streaming-validation.md).

## Optional compiled code modules

Use `WebEngine::Modules` and the [external plugin template](examples/modules/README.md)
for optional reusable C++ code plugins. The library has no rendering or platform
dependency and supports native Windows and Wasm. Explicit typed registration,
dependency/version validation and staged immutable publication preserve module
ownership and independent session state. It is source-linked, with no dynamic
DLL/Wasm loading. See [API/lifetimes](docs/modules.md) and
[native/browser reproduction](docs/modules-validation.md).

## Optional quest kernel

`WebEngine::Quests` builds on the compiled module system with validated content,
typed extensible objectives/conditions, sequential stages and branches, scoped
events, bounded candidate transactions and versioned snapshots. Application
sessions own runtime state; game policy owns rewards, puzzle rules and saves.
The same portable library runs in browser Wasm and Windows. Enable
`WEBENGINE_BUILD_QUESTS`; see [API and ownership](docs/quests.md),
[standalone/custom-module sample](examples/quests/CMakeLists.txt) and
[native/browser validation](docs/quests-validation.md). The delivered M2 kernel
does not migrate Paws or supply its Windows application.

The [M3 quest host sample](examples/quests/README.md) adds a shared browser/Windows
controller with persistent Save/Load, pause-aware Application timing, scene travel
and portable file exchange. Progress lives outside presentation scenes; one save
joins the sample wallet and quest receipts. See [ownership/storage](docs/quests-host.md)
and [actual host verification](docs/quests-host-validation.md).


Multiplayer [milestone 3](docs/multiplayer-prediction.md) adds automatic local movement prediction and
server reconciliation to authored-scene Host/Join sessions on both runtimes.


Multiplayer [milestone 4](docs/multiplayer-presentation.md) now interpolates remote players and smooths
local reconciliation corrections automatically in authored-scene Host/Join clients.


Multiplayer [milestone 5](docs/multiplayer-replication.md) adds per-peer delta snapshots, periodic
keyframes, baseline recovery and distance interest filtering to authored-scene
Host/Join on browser and Windows. Rebuild scene server/clients together for MP05;
saved scenes and source editor documents remain compatible.


Multiplayer [milestone 6](docs/multiplayer-authoring.md) adds saved ownership/relevance settings,
server-owned mesh actors and isolated-session diagnostics to both editors.
Scene clients/server now use MP06; saved archive and package envelopes are unchanged.

### Joined terrain cliffs

The optional hybrid terrain demo includes an open-path, steep limestone cliff
joined to the heightfield through matching grid edges, heights, normals and
triplanar materials. **Cliff / terrain join** opens its close view on browser and
Windows. The portable builder returns an immutable cut/mesh pair with protected
LOD and full-detail collision. See [cliff contracts](docs/terrain-cliffs.md) and
[validation](docs/terrain-cliffs-validation.md).

Shared game hosts can consume [WebEngine::Browser and the desktop integration seams](docs/consumer-hosts.md).

The [compiled module SDK handoff](docs/module-sdk.md) covers reusable non-quest
plugins, an independent custom quest-binding consumer, native/Wasm CTest and
feature-omission builds. These consumers configure without renderer downloads.

## Optional RPG foundation

`WebEngine::RPG` provides the portable R1 foundation: typed IDs, validated catalogue
metadata, immutable module bindings and independent session ownership. It depends
only on Modules and C++20, and defaults off. The
[standalone consumer](examples/rpg/README.md) builds without core/renderer/physics
downloads on native Windows/Linux and Wasm. See [API and ownership](docs/rpg.md)
and [validation evidence](docs/rpg-validation.md).

R2 adds [characters, resources, modifiers, progression and atomic transactions](docs/rpg-characters.md),
with [native/Wasm validation](docs/rpg-r2-validation.md).
Turn-based encounters, snapshots, RPGQuests and rendered hosts remain in the [release architecture](docs/rpg-library-architecture.md)
and [ten implementation prompts](docs/rpg-library-release-1-prompts.md). RPGGame
owns The Shattered Sea's rules, balance, world, story and third-person presentation.

R3 delivers [inventory and equipment transactions](docs/rpg-inventory.md),
with [cross-platform validation](docs/rpg-r3-validation.md).
The neutral inventory definitions cover weapon/consumable and tool/loadout policies.

R4 adds [abilities, statuses and candidate RNG](docs/rpg-abilities.md)
with [native/Wasm evidence](docs/rpg-r4-validation.md). The ability sample
provides random damage, item-consuming healing and non-damage resource effects.

RPG R5: [encounter lifecycle, initiative, turn budgets and cooldown phases](docs/rpg-encounters.md);
[Windows/Linux/Wasm validation](docs/rpg-r5-validation.md). Tactical actions and rendered
hosts remain later milestones.

RPG R6: [tactical actions, immutable spatial quotes, bounded reactions and
presentation recovery](docs/rpg-tactics.md); [validation](docs/rpg-r6-validation.md).
Host route reservation and frozen-writer requirements are documented for R9.
