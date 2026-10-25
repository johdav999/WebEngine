# R9 validation — shared rendered hosts

Implementation includes a neutral primitive 3D court, shared controller, browser
WebGPU/WebGL2 adapter, native DesktopHost D3D12 adapter, combined storage and CI.
See [contracts and reproduction commands](rpg-hosts.md).

Local Emscripten 6.0.11 Release build of rpg_demo and rpg_host_tests succeeded.
The shared `rpg_host` CTest passed under Node, including command rejection, reward
deduplication, reservation/playback, pause inspection, stale input, failed writes,
corrupt/stale loads, scene travel, suspension and shutdown. Windows host source
passed a C++20 syntax check against public headers using the Emscripten compiler;
this does not establish a native build or D3D12 runtime result.

Rendered acceptance results are recorded after execution. Local MSVC is not
installed; the Windows Release build and native capture checks run in the dedicated
`RPG rendered hosts` workflow. Browser software rendering and native captures do
not establish physical GPU, mobile or sustained-performance acceptance. Native
scripted commands do not establish physical pointer/keyboard interaction coverage.

RPGGame remains pinned to 56080eca124c9a62e15b146799a8004bdad5a9b2.
R10 release audit and consumer adoption remain separate work.
