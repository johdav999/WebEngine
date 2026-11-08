# Combined renderer contracts

Direct D3D12 supports foliage wind vertex/shadow shaders alongside the dedicated
non-layered unlit pixel shader. Pipeline cache identity includes both choices
independently; enabling wind must not alias an unlit or lit pipeline. The CMake
shader entry list and tools/embed_d3d12_shaders.py must contain the same ordered
entries, including unlitPS and all four wind entry points.

Browser WebGPU and WebGL2 retain their existing wind and material paths. No public
API, simulation, scene lifetime or platform dependency changes with this merge.
RPG retains API 1.14.0 and the existing atomic transaction pipeline; clothing,
foliage and simulator remain independently optional. Verify the consumer pin.

Use the repository-local Emscripten version from .emscripten-version and initialize
emsdk_env.ps1 before emcmake/build commands. Native validation requires MSVC and
the Windows SDK; native D3D12 uses the existing SDL3/DXC setup. Rebuild and run
the clothing_contracts, foliage_contracts and simulator_contracts CTest cases on
each target. Configure examples/rpg with WEBENGINE_SOURCE_DIR pointing at the
checkout and enable RPG/module/quest tests plus RPGQuests for the RPG merge checks.
Build foliage_demo and foliage_demo_windows for the browser and Windows smoke
scripts. Consult docs/foliage-dense-validation.md for scope and acceptance limits;
past validation records are not new runs, and native Dawn/mobile require separate checks.


## Combined rendering synchronization (2026-11-08)

The selected WebEngine checkout preserves upstream foliage GPU wind, compatible
WebGL2 instancing and the non-layered D3D12 unlit fast path alongside advanced
materials, clustered LOD, camera render surfaces, volumetric fog and clouds.
`ObjectUniform` contains 176 attribute bytes inside the unchanged 256-byte slot;
WebGL2 packs attributes to a 176-byte stride. Wind has its own WebGPU pipeline key
bit and D3D12 root parameters21/22; fog/grid17, diffuse GI18, optical cache19 and
cloud noise20 remain distinct. D3D12 ObjectConstants includes advanced values
and wind phase (720 bytes). The unlit pixel path composes cloud transmission and
scattering before fog. WebGL2 wind uniform caches include the material program,
and each instance's shadow flag gates cloud as well as geometric shadows.
These are presentation changes; simulation time, collision and authored identity
remain application/core owned. Existing optional module defaults are retained.

When integrating branches, preserve both shader-entry lists in the same order,
including unlitPS, four wind entries, skyVolumePS, cloudGridPS, cloudShadowPS,
fogGridPS and fogLightCachePS. Rebuild objects after virtual mesh-interface changes;
stale native objects can otherwise link against the old morphPose symbol.
For an existing Ninja browser tree, pass `-G Ninja` to emcmake rather than changing
the cached generator. Use the repository-local pinned SDK.

Build the browser and native Windows targets, run render_frame/camera_follow,
fog/sky/render_quality and clothing/foliage/simulator contracts, then render cloud
and foliage smoke fixtures on the affected backends. Use docs/clouds-validation.md
and docs/foliage-validation.md for commands and scope. Existing release evidence
is historical; report fresh checks separately. RPG sources remain independently
optional; consult docs/rpg-release-1.md and verify the consumer pin.
