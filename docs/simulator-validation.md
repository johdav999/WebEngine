# Simulator foundation validation

Recorded 2026-10-25 for stage 2. These results apply to the local working tree;
no release, tag, engine pin update or remote CI result is claimed.

## Executed checks

- Emscripten 6.0.11 from the repository-local SDK, CMake 4.2.1 and Ninja: clean
  standalone Release configuration and build passed. Only the simulator library,
  headless example and contract executables were configured; no `_deps` directory.
- CTest under Node: 2/2 passed (standalone example and 106 contract checks).
- Headless Edge 155.0.4283.62: the same 106 checks passed in the browser's Wasm
  runtime through `examples/simulator/browser.html`, with no console/page errors.
- Fresh disabled configuration: no simulator library, tests, example or dependency
  downloads. Tests requested without the library correctly failed configuration.
- Documentation links and simulator diff whitespace checked locally.

The checks remain active in Release; they do not rely on `assert`. Coverage includes
multi-resource failure atomicity, contending reservations, release before commit,
repeated commit/release, output capacity blocking, foreign/forgotten handles,
canonical bundle/event ordering, event backpressure, record limits, invalid input,
maximum int64 quantities, time overflow and equivalent elapsed-time partitions.
Identity wraparound is guarded in code; exhausting uint64 identities is not a
runtime test. Allocation failure publication safety is structural (candidate copy
then pointer swap), not an injected allocator-failure test.

To repeat Wasm checks after activating the pinned SDK:

```sh
emcmake cmake -S examples/simulator -B build/simulator-wasm -G Ninja -DCMAKE_BUILD_TYPE=Release -DWEBENGINE_SOURCE_DIR=/absolute/path/to/WebEngine -DWEBENGINE_BUILD_SIMULATOR=ON -DWEBENGINE_BUILD_SIMULATOR_TESTS=ON -DWEBENGINE_BUILD_SIMULATOR_EXAMPLE=ON
cmake --build build/simulator-wasm --parallel 2
ctest --test-dir build/simulator-wasm --output-on-failure
```

Serve that build directory over HTTP with Wasm MIME support, then run
`node scripts/simulator-browser-smoke.cjs` with Playwright available. Optional
`SIMULATOR_TEST_URL` defaults to `http://127.0.0.1:4191/browser.html`;
`BROWSER_EXECUTABLE` selects a browser executable. On this machine Playwright was
loaded through the bundled runtime's `NODE_PATH`, using installed Edge.

CMake warns about shared libraries with this Emscripten/CMake combination. This
library and consumer use static libraries and executables; those builds passed.
An initial CTest invocation ran before linking finished and reported a missing JS
file; the completed-build rerun passed. An initial browser run found a favicon 404;
the checked-in test page now supplies a data favicon, and the clean rerun passed.

## Remaining validation and handoff limits

Native Windows configuration was attempted but failed because CMake selected NMake
and neither nmake nor a native C++ compiler was available on PATH. Inspected Visual
Studio and Cygwin locations did not yield a usable compiler. Native compilation
and runtime are therefore unverified locally. The new simulator workflow includes
Linux and Windows/MSVC jobs plus the pinned Emscripten build; it has not been pushed
or executed remotely in this task.

This is a headless foundation. Browser Wasm execution does not test WebGPU/WebGL 2
rendering, hardware performance, mobile devices or game integration. The library
has no renderer dependency and does not change either rendering path. Existing
CafeGame engine pin, runtime and gameplay remain unchanged.

WebEngine AGENTS.md requires the installed engine-skill and CatGame mirror to be
updated. Its two `C:/Users/Johan/...` paths do not exist here; the equivalent
EJOHDAV skill locations are absent too. Their location was requested from the user.
[simulator.md](simulator.md) contains the complete delivered API, defaults, lifetime,
failure and build contracts to merge into those existing skills once located.
Those mirror updates are not claimed complete. Concurrent RPG edits were preserved.

For current release-candidate evidence (including all 764 checks), see [stage 6 release gates](simulator-release.md). The stage 2 results above are historical.
