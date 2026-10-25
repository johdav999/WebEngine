# Shared RPG sample hosts (R9)

The optional neutral Training Court uses primitive local meshes and the existing
R1–R8 rules/catalogues. It is a reference consumer, not RPGGame content. The RPG
kernel remains API 1.7.0 and owns no renderer, storage, clock or navigation service.

## Build and run

Activate the pinned repository-local Emscripten SDK, then from the engine root:

```sh
emcmake cmake -S . -B build/rpg-host -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF -DWEBENGINE_BUILD_RPG=ON -DWEBENGINE_BUILD_QUESTS=ON -DWEBENGINE_BUILD_RPG_QUESTS=ON -DWEBENGINE_BUILD_RPG_DEMO=ON
cmake --build build/rpg-host --target rpg_demo rpg_host_tests --parallel 4
ctest --test-dir build/rpg-host -R rpg_host --output-on-failure
python -m http.server 8080 --directory build/rpg-host/rpg-site
```

Open localhost:8080 with `?backend=webgpu` or `?backend=webgl2`. WebGPU requires
a supporting browser in a secure context. The sample never silently changes the
requested backend. Browser storage is namespaced `webengine.rpg.v1` in localStorage.

On Windows with the README's Visual Studio and native dependencies installed:

```sh
python tools/setup_windows.py
cmake -S . -B build/rpg-host -A x64 -DBUILD_TESTING=OFF -DWEBENGINE_BUILD_WINDOWS=ON -DWEBENGINE_WINDOWS_DAWN=OFF -DWEBENGINE_BUILD_RPG=ON -DWEBENGINE_BUILD_QUESTS=ON -DWEBENGINE_BUILD_RPG_QUESTS=ON -DWEBENGINE_BUILD_RPG_DEMO=ON
cmake --build build/rpg-host --config Release --target rpg_demo_windows rpg_host_tests --parallel 4
ctest --test-dir build/rpg-host -C Release -R rpg_host --output-on-failure
build/rpg-host/Release/rpg_demo_windows.exe
```

The native sample selects direct D3D12 and uses DesktopFiles' application user
directory, `saves/rpg.save`. Fonts and runtime DLLs are copied beside the executable.
All optional RPG switches remain off by default. The standalone kernel consumer
rejects the demo switch because it deliberately does not configure host dependencies.

## Controls and scope

Use the twelve buttons to equip a blade, deliver the parcel, begin combat, move,
strike, end turns, pause, travel, save, load, recover presentation, or finish/exit.
Drag the viewport to orbit the third-person camera; use the wheel to zoom. M moves,
A strikes and Space ends the active turn. Browser shortcuts apply only to the
focused canvas; P also toggles pause. Native GUI-consumed input does not reach play.
Both actors are manually controlled; this sample supplies no opponent AI.

The parcel grants two coordinated award receipts. Duplicate delivery rejects.
Successful saves load on startup. Invalid saves retain a fresh playable session
with an error message; an explicit invalid Load retains the current live session.
The UI exposes inventory stack count, XP, turn and AP; equipment is a toggle,
not a general inventory editor. There is no paid generation or external content.

## Ownership and execution

`host_controller.hpp` owns the combined SavedGame, immutable catalogues, bounded
input queue and presentation reservation. `host_scene.hpp` owns scene-scoped
entity-to-actor/component maps and the existing OrbitCameraBehaviour/CameraController.
The hosts borrow the controller for frame/event callbacks and clear callbacks and
GUI before shutdown. Desktop views are registered before activation. Browser scene
binding owns the renderer and rebinds the registered camera on scene replacement.

Application owns frame advance. Hosts drain queued commands at idle boundaries;
simulation only advances visual playback. Presentation updates the camera even while
paused. No timer, camera movement, pause or animation completion advances a turn.
The bounded WaypointGraph uses authored grid clearance and occupied-cell checks.
The controller exclusively reserves the route/destination before final WorldStamp
validation. No motor, physics body or AI writes these presentation actors. Pending
presentation blocks further gameplay; acknowledgement releases it without repeating
costs, effects or rewards. This is not a general concurrent navigation reservation API.

Focus loss, renderer readiness loss, pause and explicit recovery snap visuals to
committed state before acknowledgement. Travel/load replace scene-scoped handles,
increment world/input epochs and discard stale queued commands. Save rejects during
pending presentation. Load validates a detached combined RPG/quest envelope plus
sample checkpoint/grid constraints, constructs the replacement scene, then publishes
the replacement session. No rendering or gameplay callback interleaves that idle swap.
Storage contains durable positions and receipts, never handles, callbacks or animation.
localStorage setItem and DesktopFiles atomic write preserve the previous save on error.

## Acceptance tools

`rpg_host_tests` exercises shared lifecycle/transaction behaviour without a renderer.
`scripts/rpg-host-smoke.cjs` uses Playwright and pngjs to drive real browser buttons,
keyboard and pointer drag, inspect pixels on both browser backends, reload saves and
test write failure. It owns a temporary browser profile and local HTTP server.
Set RPG_SITE, EVIDENCE_DIR and optionally BROWSER_EXECUTABLE for a custom build.
RPG_BACKENDS selects comma-separated backends; RPG_HARDWARE=1 uses the browser's
hardware adapter instead of the default SwiftShader test flags. This is required
for the validated local Edge WebGPU run. The two CI backend steps run independently.

Native `--smoke output-directory` captures D3D12 frames, drives shared commands and
checks storage/recovery/shutdown. Use a fresh output directory for a fresh save fixture.
The smoke test injects focus, keyboard and drag events into SDL's native queue to
exercise DesktopHost routing without taking focus from another app. It does not
establish physical pointer/keyboard acceptance. `scripts/rpg-native-smoke.py` runs
the executable with retained process/startup diagnostics; `scripts/rpg-check-frames.py`
checks captured pixels. Native rendering requires a hardware D3D12 adapter; this
engine backend deliberately has no implicit WARP fallback.
The `RPG rendered hosts` workflow builds both hosts and uploads evidence. Actual
results and device/software coverage are recorded in [R9 validation](rpg-r9-validation.md).
