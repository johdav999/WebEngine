# Scene archive validation — 2026-10-10

Implementation: `SceneArchive` memory/native file APIs, `.wescene` scene documents,
`.weassets` libraries, browser local-file controls and staged metadata/audio loading.
The release browser build was published to `dist`; its Wasm hash matches the
isolated `build/web-archive-release/site` build.

| Check | Result |
| --- | --- |
| Full native regression suite | 26/26 passed in `build/native-physics` |
| Final targeted archive tests | Passed after root-motion and const-scene/failed-save coverage |
| Browser archive acceptance | 9 checks passed against the published `dist` server on port 8080 |
| Audio regression | 16 MP3/WAV checks passed, including nonzero output samples and concurrency |
| Browser → native | Native loader opened and re-serialized the final 91,736,487-byte browser scene |
| Native → browser | Native 64-bit fixture opened in 32-bit WebAssembly with animation/root motion, physics and shared resources |

Browser: Windows Chrome 154.0.8037.99, actual WebGPU rendering, release build.
The browser test intercepted the external Google Fonts CSS with an empty stylesheet
to avoid restricted-network font failures; engine/canvas pixels used the real GPU.
Frame time was controlled for reproducible images. Inspector HTML overlays were
hidden only during canvas pixel comparisons. Mobile coverage is a 390×844 viewport,
not a physical Android/iOS device test.

The nine archive checks establish edited textured Knarr/material/environment/inset
view restoration with zero changed canvas pixels; malformed-scene preservation;
staged audio decode failure preservation; resumed playground motion; physical
download reopening in a fresh page without loading Knarr's FBX; exact restored
skeletal deformation/paused state; usable asset-library imports; native-to-browser
compatibility; and enabled controls in the mobile layout. There were no browser
JavaScript/console/GPU errors in the acceptance run.

Evidence: `build/evidence/scene-archive/results.json`, rendered before/after PNGs,
physical `.wescene`/`.weassets` downloads, and
`build/evidence/scene-archive/audio/audio-results.json`. The Knarr-containing library
is 90,670,798 bytes. Assets are uncompressed; these are file sizes, not peak-memory
or performance measurements.

Scope remains the built-in engine data model described in
[scene-archives.md](scene-archives.md). Custom C++ behavior, editable material graph
source, application GUI/input callbacks and deterministic physics solver checkpoints
are outside this archive. Audio resumption requires a fresh Play gesture; speaker
audibility and physical mobile acceptance were not established by these tests.
