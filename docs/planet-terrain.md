# Planetary terrain: stages 1 and 2

Optional `engine_planet` / `WebEngine::Planet` supplies portable CPU preparation
and view-dependent refinement of a procedural cube-sphere. The same C++ flight
example runs on browser WebGPU/WebGL2 and the native Windows desktop host.
Stage 1 renders one continuous planet from space to the surface. Stage 2 adds
[stable collision, radial gravity, landing and surface movement](planet-physics.md).
Planetary atmosphere, oceans, authored terrain and persistence remain future work.

## Build and run

Use the repository-local Emscripten version from `.emscripten-version` (6.0.11):

```powershell
& ./.tools/emsdk/upstream/emscripten/emcmake.exe cmake -S . -B build/web-release -G Ninja -DCMAKE_BUILD_TYPE=Release -DWEBENGINE_BUILD_PLANET_DEMO=ON
cmake --build build/web-release --target planet_demo planet_tests --parallel 4
python scripts/serve.py --port 8168 --directory build/web-release/planet-site
```

Open `http://localhost:8168/?backend=webgpu` or
`http://localhost:8168/?backend=webgl2`. Each preference is explicit; inspect the
effective backend in `window.planetState.backend` (0 = WebGPU, 1 = WebGL2).
Serve the generated `planet-site` directory, which contains its own HTML, JS and
Wasm; building this example does not publish over the playground's `dist/`.
If Windows sandbox temporary-directory permissions prevent linking, set `TEMP`
and `TMP` to an existing task-local build directory before invoking the build.

With the Windows SDKs installed as described in [Windows setup](windows-desktop.md):

```powershell
cmake -S . -B build/windows -DWEBENGINE_BUILD_WINDOWS=ON -DWEBENGINE_BUILD_PLANET_DEMO=ON
cmake --build build/windows --config Release --target webengine_planet_windows planet_tests --parallel 6
./build/windows/Release/webengine_planet_windows.exe --backend dawn
# Or select the direct D3D12 renderer:
./build/windows/Release/webengine_planet_windows.exe --backend d3d12
```

`WEBENGINE_BUILD_PLANET` defaults ON and builds a separate library. Set it OFF to
omit the library and its tests. `WEBENGINE_BUILD_PLANET_DEMO` defaults OFF and
requires the library; the Windows example also requires `WEBENGINE_BUILD_WINDOWS`.
The module adds no dependencies to `engine_core`.

## Flight controls

Click the view to focus it. WASD moves relative to the flight direction; Q/E moves
radially down/up; Shift boosts speed by five. Right-drag changes heading and pitch
relative to local planetary up. Speed adapts from 5 m/s near the ground to
planet-scale speeds in space. Escape releases held input and stops a guided tour.
Focus loss, pointer cancellation and browser visibility loss cancel held controls.
Native F11 toggles fullscreen.

**Land on surface** follows a continuous 28-second powered descent to 4 m above
the source, then gravity and registered collision complete touchdown.
**Take off to space** flies outward to 5,733.9 km altitude over 28 seconds. Manual movement or looking
takes over from the tour. **Stop tour** holds position. **Orbit bookmark** explicitly
repositions to the initial orbit and is separate from the continuous flight tests.

Stage 2 replaces the former radial clearance clamp with a fixed-resolution source
collider and a swept sphere motor. WASD walks on supported terrain; Space jumps,
E takes off, and G toggles thrusters. Source collision and the currently displayed
coarse triangle approximation can differ while terrain is still refining. Read
[planetary physics](planet-physics.md) for exact collision, gravity and ownership contracts.

## Portable contracts

Include `engine/planet.hpp` and link `WebEngine::Planet`.

```cpp
engine::PlanetTerrain terrain;
engine::PlanetPosition eye{0, 0, 12'000'000};
engine::PlanetView view{eye, {0,0,-1}, engine::pi/3, 900};
auto draws = terrain.update(view, deltaSeconds);
for (const auto& draw : draws) {
    // Ordinary MeshComponent, retained by the caller:
    component.mesh = draw.patch->mesh;
    actor.transform.position = draw.patch->anchor.relativeTo(eye);
    component.setMorphWeight(0, draw.parentWeight);
    actor.enabled = draw.horizonVisible;
}
```

`PlanetPosition` uses double precision in a planet-centred frame. `relativeTo`
subtracts doubles before narrowing to engine float `Vec3`. Patch vertices are
small float offsets from a double anchor. The example projects anchors relative
to the eye every presentation frame and keeps its camera at local zero. This is
an example-owned render-origin projection, not a general scene/physics rebasing
API. Other objects must use the same origin deliberately.

`PlanetTile` identifies `(face, level, x, y)`. `direction(tile,u,v)` maps a face
sample in `[0,1]` to a unit direction. Six matching cube grids avoid pole
singularities. Height uses one deterministic smooth 3D source independent of face,
detail selection, residency and origin; surface normals sample that same source.
Textures are prepared per patch from elevation/slope and share edge samples.
There is no user-supplied height provider or procedural seed in this stage.

Defaults are radius 6,371,000 m, height scale 12,000 m, 16 cells per patch,
maximum level 20, at most 240 leaves, a 3-pixel error target and a 0.35-second
refinement morph. Settings validate completely before terrain construction.
Allowed radius is 1,000..1e10 m; height scale is 0..5% of radius; cells are powers
of two from 4..32; maximum level is 0..24; leaves 6..1024; target 0.5..32 pixels;
morph duration 0.05..5 seconds. Views require finite positions/forward vectors,
distance 1..1e14 m from the planet centre, forward squared length at least 0.5,
FOV 0.1..3 radians and viewport height 1..16384. Update delta is finite 0..1 second.
Invalid view/delta data is rejected before changing accepted state.

## Refinement, seams and bounds

Six roots cover the planet permanently. Refinement estimates projected geometric
error from curvature and procedural frequency bands; it prioritizes the largest
visible error. This is an estimate with a hard residency budget, not a guaranteed
pixel error bound. Existing mesh frustum culling handles the actual camera cone.
A conservative planet-sphere horizon test avoids refining hidden regions.

A split prepares four complete immutable child meshes before replacing the parent
leaf. Each child's `parent` morph target starts on the actual parent's triangle
surface and decays toward the finer source mesh. Children cannot split again until
their morph completes. Stable sibling groups merge with hysteresis; at a full
budget, a less important group can merge to admit a more important refinement.
Coarsening selects the parent directly; it does not run a reverse morph.

Matching face coordinates share source samples. Radially extruded, outward-facing
skirts cover differences between adjacent refinement levels. They are visual seam
coverage, not welded topology or collider geometry. Normals and heights remain
source based. Maximum detail/patch budgets can leave visible coarse approximation
or material changes after rapid travel; there is no disk/network streaming stall.

The owner prepares at most four meshes (one split) per `update`; construction
prepares six roots. All work is synchronous CPU work. Neither preparation count nor
leaf limits establish a frame-time guarantee. The resident tree retains ancestors
for future merges, with `(4 * leaves - 6) / 3` nodes for a complete six-root forest.
Callers retaining old `PlanetDraw` values also retain those immutable meshes;
release old frames to keep external memory bounded. CPU/GPU upload cost, driver
memory and consumer references are outside the tree count.

## Ownership and rendering boundary

`PlanetTerrain` owns its CPU tree and immutable resources. The caller owns clocks,
views, scene actors and residency projection. No callbacks, scene references,
workers, renderer types or browser APIs cross the public interface. The example
uses a bounded pool of ordinary scene-owned actors and mesh components, borrows
them while its Application-owned scene lives, and releases renderer bindings before
shutdown. Input travels through existing `InputBindings` and Application snapshots.
Simulation/source positions remain independent of visual refinement and morphs.

The current demo uses adaptive near/far clipping with the ordinary depth buffer.
It does not implement logarithmic/reversed depth, multi-frustum compositing,
planet-wide shadowing or simultaneous centimetre-scale foreground and distant
celestial rendering. Planet-aware atmosphere/water and persistent edits remain
future work. The existing flat [heightfield terrain](terrain.md) is unchanged.

See [stage 1 validation](planet-validation.md) and [stage 2 validation](planet-stage2-validation.md)
for checks and runtime evidence.
