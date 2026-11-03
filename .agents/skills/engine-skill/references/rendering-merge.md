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
