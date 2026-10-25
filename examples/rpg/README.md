# Standalone RPG foundation consumer

This consumer links only `WebEngine::RPG` and its public Modules dependency.
It supplies a neutral compiled metadata validator, retains a catalogue across
composition replacement, and allocates IDs in two independent sessions. It is not
a combat implementation or rendered example. RPG stays disabled unless selected.

From the WebEngine root:

```sh
cmake -S examples/rpg -B build/rpg-sdk -DWEBENGINE_SOURCE_DIR=/absolute/path/to/WebEngine -DWEBENGINE_BUILD_RPG=ON -DWEBENGINE_BUILD_RPG_TESTS=ON -DWEBENGINE_BUILD_MODULE_TESTS=ON
cmake --build build/rpg-sdk --config Release
ctest --test-dir build/rpg-sdk -C Release --output-on-failure
```

Use MSVC x64 on Windows. For Wasm activate `.tools/emsdk`, prefix configuration
with `emcmake`, add `-G Ninja -DCMAKE_BUILD_TYPE=Release` and choose another build
directory. The emulator is selected by the SDK. No paid services or assets run.
Leaving RPG OFF builds a tiny omission executable; it must not link the kernel.

With tests enabled, CTest also configures separate default-off/all-off consumers
and asserts actionable failures for missing Modules, tests without RPG, and the
undelivered quest/demo switches. The configuration matrix checks for accidental
engine-core, physics, GUI and dependency-download setup. See
[full API/ownership contracts](../../docs/rpg.md) and
[validation](../../docs/rpg-validation.md).
