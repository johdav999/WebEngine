# M4 publication validation - 22 October 2026

This scoped publication is based on f4cae84. It excludes unrelated engine
working-tree features and uses SDL/Dawn for Windows. Direct D3D12 remains a
separate development implementation. Native examples stage SDL and DXC DLLs.

Native MSVC and Node/Wasm both passed 71 module, 242 quest-kernel and 49 quest-host
checks. Kernel snapshots matched (344 bytes, f840213075084f6a). Both also passed
GUI clear tests for capture, focus, stale IDs and replacement activation.

The non-quest example passed real Windows and forced WebGPU/WebGL2 browser
checks. Full quest-host acceptance passed four native processes (seed, resume,
two cross-host imports) and both browser renderers, including actual file
download/upload, pause, travel, save failures and recovery. Final wallet remained
20 and scene count remained one; no browser errors occurred.

Paws candidate acceptance includes actual SDL mouse input, partial save and
relaunch, first-quest completion, twenty-coin reward and return to garden. Its
eight focused native rule/progression/save/scene/UI/audio suites passed, as did
the 101-artifact M0 corpus and both baseline replay tests. Save-recovery browser
checks passed both renderers; full extended beach acceptance passed WebGPU.

Evidence: consuming Paws build/evidence/m4/engine-host, engine-modules,
engine-modules-native, windows-final-seed, windows-final-resume. Native renderer
acceptance is distinct from browser GPU acceptance and physical mobile testing.
QUEST_NATIVE_BACKENDS defaults to dawn; opt into dawn,d3d12 only on a source
revision that actually supplies both native backends.
