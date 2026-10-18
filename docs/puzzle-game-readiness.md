# Desktop and mobile browser 3D puzzle-game readiness

Reviewed on 2026-10-10 against current source, feature contracts and fresh validation.
Scope: a single-player game with modest level-sized scenes, mouse/keyboard and
touch controls. Mechanical physics puzzles, multilingual text-heavy games and
support for older devices introduce additional requirements listed below.

## Assessment

The engine has the essential reusable systems to start a small 3D puzzle game on
WebGPU-capable desktop and mobile browsers. No missing foundational engine module
was identified for that scope. It is not yet proven as a complete game on physical
phones. The largest outstanding work is an integrated game example and a real
device/browser acceptance matrix, followed by extensions driven by its mechanics.

Do not schedule scene transitions, character movement, camera controllers,
selection/actions, prefabs, audio, browser persistence, touch controls or quality
budgets as missing systems: these are implemented. The architecture's outdated
selection roadmap entry was corrected during this review.

## Existing mechanisms

| Need | Current implementation | Contract/source |
| --- | --- | --- |
| Runtime and level changes | Owned scenes, start/pause/resume/shutdown, staged transitions, asset preparation and separate clocks | [Application](application-lifecycle.md), [application.cpp](../src/application.cpp) |
| Safe puzzle-object lifecycle | Hierarchy ownership, generation-checked handles, guarded traversal and deferred destruction | [Scene](../include/engine/scene.hpp), [actor contracts](../README.md#actor-identity-components-and-lifecycle) |
| Reusable objects | Shared resources, hierarchy prefabs, per-instance overrides and application factories | [Prefabs](prefabs.md), [prefab.cpp](../src/prefab.cpp) |
| Desktop and touch input | Actions, context stacks, pointer records, contact ownership, gestures and disposable virtual controls | [Input](input-validation.md), [UI/input](ui-input-integration.md), [input.js](../web/input.js) |
| Player and camera movement | Optional collision-aware/free-XYZ motor, slopes/steps/jumps/moving ground; orbit/follow/tracking/framing/smoothing/camera collision | [Movement](movement.md), [cameras](camera-controllers.md) |
| Puzzle interaction | Bounds/proxy or CPU triangle picking, optional visible-mesh occlusion, plane/axis drag/pivot rotation with commit/cancel, selection/focus, eligible actions and ordered events | [Selection](selection.md), [selection.cpp](../src/selection.cpp), [manipulation](../include/engine/manipulation.hpp) |
| Collision and triggers | Static/dynamic/kinematic bodies, filtered rays/convex overlaps/sweeps, contacts and sensor enter/exit | [Physics](physics.md), [physics API](../include/engine/physics.hpp) |
| Presentation | FBX/GLB, PBR materials, lighting/shadows, skeletal animation/controllers/markers, morphs and effects | [README](../README.md), [animation control](animation-control.md) |
| HUD and menus | Retained widgets, wrapped layout/labels, scaling, scrolling, text entry and browser accessibility descriptions | [GUI](gui.md), [UI/input](ui-input-integration.md) |
| Audio | Independent/looping voices, buses, mute/volume/fades, spatial emitters/listeners and browser activation | [Audio](audio.md), [browser audio](../web/audio-system.js) |
| Saves and content loading | Application/scene archives, custom codecs and saved IDs; IndexedDB/localStorage byte adapters; asynchronous HTTP loading, cancellation/cache and scene preparation | [Persistence](persistence.md), [assets](assets.md) |
| Quality and resilience | Resolution/DPR/shadow budgets, LOD/culling/instancing, capability reporting, GPU recovery, composed page suspension and performance instrumentation | [Rendering quality](rendering-quality.md), [renderer.cpp](../src/renderer.cpp) |

## Outstanding work, by priority

### 1. Complete game integration and physical-device acceptance — high priority

The browser executable remains a rendering/feature playground. Its frame input
callback operates the orbit camera; physics-demo selection kicks bodies. It does
not demonstrate the new selection/action API as a complete puzzle interaction
flow, nor a playable level with goal, reset/undo, progression and restored game
behaviour. See [main.cpp](../src/main.cpp).

Build a small application/example with one representative mechanic: start/menu,
load level, mouse/touch interaction, puzzle state change, feedback/audio, completion,
retry/undo, next level and save/reload. Route GUI/camera/scene contacts explicitly
and rebuild action callbacks after loading. These rules belong in the application;
the example should expose any further reusable mechanisms actually needed.

Run it on supported physical Android and iPhone/iPad devices, in portrait and
landscape, including browser chrome changes, touch cancellation, page switching,
audio activation/resume, save/reload and GPU/resource recovery where available.
Measure representative cold loads, CPU/GPU frame times, memory and a sustained
play session. Desktop Chrome and Chromium mobile emulation do not prove these.

### 2. Browser/device coverage policy — conditional release blocker

The renderer now prefers WebGPU and automatically selects a basic-lighting
WebGL 2 fallback when the API is missing or initial adapter/device acquisition
fails. See [fallback contracts and scoped evidence](rendering-fallback.md).
HTTPS remains required for WebGPU deployment.
WebGPU support alone does not establish that this engine renders correctly on a
particular browser/GPU combination.

Current primary-source guidance confirms Safari 26.0 introduced WebGPU
([WebKit](https://webkit.org/blog/17333/webkit-features-in-safari-26-0/)); Chrome's
Android rollout also depends on OS/GPU support
([Chrome](https://developer.chrome.com/blog/new-in-webgpu-121)). These are platform
capabilities, not engine acceptance evidence.

Define minimum supported browser/OS/GPU combinations and test acquisition on real
devices. The implemented fallback expands the rendering options but does not
establish acceptance on additional physical devices. Define whether basic lighting
without PBR/shadows is an acceptable product presentation on WebGL 2 devices.

### 3. Mobile content/loading budgets — important for larger content

The [mobile asset pipeline](mobile-assets.md) now offers optional isolated
FBX/GLB/image decoding workers, per-frame GPU asset admission and offline semantic
mip/KTX2 fallback-bundle tooling. Custom decoder callbacks and owner hydration
still do synchronous work. Budgets defer visual draws while resources arrive;
scene preparation does not prewarm GPU resources. One oversized asset can exceed
a frame's soft byte/time target to ensure progress.

BC7/ETC2/ASTC selection retains RGBA fallback; Basis transcoding and a compressed
scene archive codec remain absent. Resource eviction is explicit, with no automatic
memory-budget/LRU policy. Static quality profiles exist; measured adaptive quality
and physical mobile performance acceptance remain separate work. Start with
representative content, explicit unloading and measured real-device budgets.
Telemetry does not account for all allocator, physics, browser or driver memory.

### 4. Precise and occlusion-aware selection/manipulation — mechanic-dependent

Following this review, selection now has optional CPU triangle picking and
visible-mesh triangle occlusion, including noninteractive walls. Original mesh
parts, morphs, skeletal poses and render interpolation are supported. The default
bounds/proxy path remains compatible. `ManipulationController` supplies world
plane/axis translation and pivot rotation for non-physics actors with guarded
ownership, explicit commit/cancel and stale-handle checks.

Applications still route captured mouse/touch input and cancellation, draw feedback,
and implement snapping and move legality. Geometric picking does not evaluate
texture alpha/shader displacement or GPU pixels, and triangle traversal is linear
after bounds rejection. Accelerated/GPU picking and physics-driven grabs remain
extensions if representative assembly content needs them. See
[contracts and integration](selection.md) and [new validation](selection-validation.md).

### 5. Physics joints/constraints — implemented; game integration remains

The portable [physics joint API](physics-joints.md) now supplies world-owned hinges,
sliders, distance springs and movable point grabs, with validated settings and
automatic removal when an endpoint body is removed or rebuilt. Applications own
joint definitions, input/target mapping, grab cancellation and puzzle rules.
Motors, break thresholds, rope ranges and built-in joint persistence remain
extensions. Kinematic movement remains available for scripted doors/platforms and
simple piece placement.

### 6. Content authoring and secondary services — lower priority

Prefabs are in-memory C++ definitions; there is no definition-file authoring format
or full level editor. Scene archives already persist supported instantiated content.
Add a validated application content format/editor when the level-production workflow
requires it; code-authored initial levels remain viable.

General timers/tweens, gameplay command history, grid/pathfinding, gamepad input,
networking and offline/PWA packaging are not delivered general services. They are
not universal requirements for this scope. Undo/reset/progression/hints/autosave
belong to game state/policy and can use existing lifecycle/persistence contracts.
Extract generic utilities only from concrete consumers. Physics saves are not
deterministic solver checkpoints, so undo should not assume exact solver rewind.

GUI text lacks shaping/bidi/font fallback and grapheme-aware pointer selection.
This becomes a significant requirement for languages needing those features, not
for a basic icon/Latin-text puzzle HUD. Runtime audio and action executors are also
not automatically restored by archives; applications reconstruct them deliberately.

## Fresh validation in this review

| Check | Result/evidence |
| --- | --- |
| Rebuilt current native Debug core/tests | Passed: `cmake --build build/native -j 6` |
| Full native CTest suite | 38/38 passed, zero failures: `ctest --test-dir build/native --output-on-failure --timeout 60`; [log](../build/native/Testing/Temporary/LastTest.log) |
| Current browser Release target | Passed with repository-local Emscripten 6.0.11: `cmake --build build/web-quality-release --target webengine -j 6` |
| Chrome WebGPU quality/recovery | Eight groups passed, no reported page/console errors: [results](../build/evidence/puzzle-readiness/quality/results.json) |
| Desktop and Chromium touch input | Ten groups passed, no reported page/console errors: [results](../build/evidence/puzzle-readiness/input/result.json) |

Browser validation uses a private site in `build/puzzle-readiness-site`, staged from
current `web/` and the rebuilt executable; it does not replace `dist/`. Wasm SHA256:
`B7A9DA0B5025574476F6B0F6683FE81AAAF0967F20E07493A78661DAE8E938C5`.
Quality checks ran in installed Chrome 154.0.8037.99 with real WebGPU rendering.
They exercised controlled `GPUDevice.destroy()`, bounded acquisition failures,
synthetic page lifecycle and BC7 on this adapter; they do not prove real OS GPU
crash behaviour, ETC2/ASTC execution, physical mobile performance or Safari/Firefox.

Earlier feature documents retain their historical failed-suite notes. This fresh
full-suite result supersedes those failures for the current native checkout only;
it does not turn their historical runs into successful runs or prove game integration.

## Recommended next step

Implement the small playable example first, using the delivered services. Establish
a real-device performance/support baseline with its actual assets. Then implement
game manipulation and physics joint flows if its selected mechanic requires them;
broaden rendering backends only if the supported-device policy requires it.
