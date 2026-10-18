# Recast Navigation 1.6.0

Unmodified `Recast/`, `Detour/` and `License.txt` from the upstream `v1.6.0` release:
https://github.com/recastnavigation/recastnavigation/tree/v1.6.0

Source archive: https://codeload.github.com/recastnavigation/recastnavigation/zip/refs/tags/v1.6.0

Archive SHA-256: `8B50C62177249554B226514B307E6C529934A62B3926822777BF6ABB8906A155`.

WebEngine's `cmake/recast.cmake` compiles the upstream source directly. Recast and
Detour are private dependencies of optional `engine_ai`; no upstream demo, crowd,
tile-cache, renderer, platform adapter or upstream build script is compiled.
Engine-owned adapters are in `src/navmesh.cpp`. See `License.txt` (zlib license).
