# Sky and weather validation

Verified on 2026-10-14 on Windows, using repository-local Emscripten 6.0.11,
a Release build in `build/web-sky-release`, and desktop Chrome 155.0.8059.79.
These checks used the default single-sample rendering policy. They do not
establish physical Android/iOS acceptance or sustained mobile performance.

| Check | Result |
| --- | --- |
| Native `sky`, `scene_archive`, `persistence` | All three passed on the final sky implementation |
| `scripts/sky-weather-smoke.cjs` | Passed on forced WebGPU and WebGL 2 |
| `scripts/sky-smoke.cjs` | Original gradient/panorama and per-view checks passed on both backends |
| `scripts/compositing-smoke.cjs` | 13 checks passed, including exactly one GUI pass after both views |
| `scripts/persistence-smoke.cjs` | Passed, including exact saved checkpoint pixel restoration and fresh-page restore |
| Documentation links, JavaScript syntax and focused whitespace | Passed |

Weather acceptance proves actual indexed sphere submission: 3,968 triangles,
zero collected scene meshes, and zero sky shadow draws. It exercises translation
invariance, orthographic rays, very tight near/far planes, a second camera with a
different center/orientation, per-view suppression, noon/sunset/night scattering,
procedural clouds and wind, explicit clock steps, actual lit surface changes,
resource recovery, playground controls, automatic time advancement and pause.
No browser/GPU errors were reported.

The noon and midnight mean RGB channel values were approximately 126.66 and
17.55 on both backends. Enabling clouds changed more than 428,000 pixels in the
fixture; changing wind phase changed more than 305,000 pixels. These are visual
acceptance observations, not frame-rate benchmarks. Original gradient, panorama,
wrapped-seam and bilinear-edge sample RGB values matched between backends.

Native coverage includes inward winding/normals, shared unlit geometry, camera
translation at 100 million units, affine axes, validation and atomic rejection,
day/night clock wrapping, light handle lifetimes and format-5/6 sky archives.
The persistence suite also covers legacy version-4 scenes with sky disabled.

Results and screenshots are in `build/evidence/sky-weather/` and
`build/evidence/sky/`. `build/evidence/sky-weather/build-manifest.json` records the
exact tested HTML, JavaScript, Wasm and sky-control script by byte count and
SHA-256. The tested Wasm SHA-256 is
`7fd98a3948f5bdef48b7cb2ce0a9d5c054fd7bdc04e4758ca1b81ca7ab2bf33a`.
Compositing and persistence reports are in `build/evidence/compositing/` and
`build/evidence/persistence/`.

Clouds are a 2D layer. Scattering is a bounded single-scattering approximation;
volumetric clouds, cloud shadows, multiple scattering, object aerial perspective,
HDR/EXR decoding and automatic IBL rebaking are not part of this feature. See
[sky contracts](sky.md) for APIs, defaults and ownership.
