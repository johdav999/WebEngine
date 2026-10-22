# FX authoring

Enter **Scene editor**, then open **Details → Particle FX**. Choose Campfire,
Explosion or Fountain and mist and **Create effect**, or select an existing effect,
one of its managed emitters, or its owning actor. A new effect gets a top-level
actor at scene-local origin. The component inspector can select among multiple
owned effects. The first owned effect is used when selecting an actor.

## Editing and reusable assets

The FX section edits one instance by publishing a new immutable EffectAsset and
new immutable definitions for changed emitters. Shared presets and other instances
retain their resources. Add, duplicate, remove and rename emitters; rename the
effect resource separately from the scene node. An effect must keep 1–64 emitters.

Grouped controls cover shape/surface references, position/velocity ranges, lifetime,
rate, capacity, seed, looping, delay/duration, drag/wind/gusts, rotation, simulation
space, local emitter transforms, blend, alignment/stretch, soft distance and flipbooks.
All units and validation retain [runtime FX contracts](fx.md); rotations are radians.
Import images/models through Assets first, then select them as the texture or static
emission mesh. Invalid atlas grids, ranges and non-finite values preserve the last
valid effect and show an error. This editor does not generate textures or atlas mips.

The emitter timeline shows each start delay and cycle duration. Click a row to
select it, drag horizontally to change its start delay, or enter exact seconds in
Emission & timing. A delay applies once before the first emission cycle, not before
each loop. Bursts remain ordered times within the emitter cycle, excluding delay.
A delayed emitter reports playing before it emits. Stop/pause/restart use the existing
emitter semantics; restart resets the delay.

Size, RGBA, vector velocity/force and burst editors show key tables and curve plots.
Drag a plotted point to change its time/value; release commits one undo command.
RGBA/vector channels can be edited individually. Add/remove keys or edit exact
numbers in the table. Keys must retain valid ordered times; duplicate times are
rejected for age curves. Bursts use seconds and positive integer counts. Escape,
blur and pointer cancellation discard an unfinished graph/timeline gesture. There
is no undo history entry until commit. Each successful form/key edit is a command.

**Export .wefx** downloads a self-contained versioned effect resource archive.
**Import effect** replaces the selected effect, or creates an effect actor if none
is selected, through normal history. A .wefx uses the engine asset archive envelope
and contains exactly one EffectAsset entry; referenced textures and surface meshes
are embedded once by typed resource identity. It is not JSON, a scene, a file-system
path resolver or a bundle of executable callbacks. Browser import accepts at most
64 MiB; decoded resources also obey ArchiveLimits. Failed/corrupt imports preserve
the current scene and valid assets. Async reads reject changed documents, selection
and revisions. Duplicate an effect actor to reuse its immutable definition with
independent instance state, or export it for another scene.

## Isolated preview

**Open preview** switches the existing canvas to an independent scene with its own
camera, ground and effect state. It does not tick the authoring scene. Existing
camera navigation controls operate on a separate preview OrbitController. Controls:

- Pause / Resume freezes or advances preview particles and clocks.
- Restart resets seeded playback and delay, retaining the current pause state.
- Step 1/60 s advances once, then stays paused.
- Isolate selected emitter hides/freezes other preview emitters and restarts preview.
- Close preview / Escape rebinds retained authoring views before releasing preview
  playback. Camera transforms, scene bytes, selection and history remain authoring state.

Committed FX edits and history replay replace/restart only the preview effect,
preserving its stable camera address and pause state. Valid emitter isolation is
retained across edits; changing the target effect clears isolation. Source effect
edits themselves reset the edited instance's playback to its configured initial
state; Undo restores the prior subtree including live particles/clocks exactly.

Diagnostics show preview time, living particles, dropped emissions, particle draw
calls and particle upload bytes. Render counters are the latest frame's existing
renderer counters, so they can lag one frame. They describe the preview scene, not
per-emitter GPU time or an FPS improvement. WebGL retains its existing individual
particle draws and does not report WebGPU particle-buffer counters.

Preview uses the host's presentation delta (capped to 0.1 seconds per frame), while
single steps are explicit. Host suspension freezes its clocks and avoids hidden-time
catch-up. Resize and renderer recovery keep CPU preview state. Authoring Play,
scene save/open/recovery and exit editor are gated until preview closes, since the
renderer then borrows preview views. Autosave defers capture until preview closes;
it captures committed authoring data. Effect export remains available in preview.
A separate normal Play/Stop clones the authoring scene and advances configured
emitters through the existing Application runtime.

## Portable APIs and ownership

`EditorSession::selectedEffect`, `createEffect(asset,position)` and
`setEffectAsset(asset)` live in optional engine_editor. The session borrows its idle
scene as before. Effect edits stage a fully validated component subtree, preserve
root saved identity/common state and same-index emitter saved identities/common flags and tags, replace
at the same sibling position, and retain the alternate subtree in bounded history.
Runtime component handles change on replacement; resolve them again. Undo/redo
checks parent world pose and supported subtree fingerprints, including effect and
emitter definitions. Later conflicting writers are rejected. Effects or their emitters with additional
custom children are rejected to avoid discarding unrelated children.

`EffectPreview` in engine_editor owns its Scene, camera, transient component and
clocks. Hosts bind `scene()`/`camera()` and release borrowed renderer bindings before
destroying the preview owner. `start`, `restart`, `pause`, `advance`, `step`, `isolate`
and `stop` have no source-scene callbacks or subscriptions. `advance` accepts finite
0–0.25 seconds; `step` requires positive seconds in that range. It is a particle
preview, with no game callbacks, physics simulation policy, audio or effect graph.

Portable `EffectArchive::save/load` lives in engine_core and reuses SceneArchive's
validation/resource-budget contracts. Archive format 18 appends startDelay to
ParticleEmitterDefinition; versions 1–17 default delay to zero. Scene files, asset
libraries, .wefx files, cloning and editor Play retain delay. Deploy matching host
and worker codecs. Preview scenes, isolation, camera/controller and editor gestures
are transient and are not persisted in authored effect resources.

Browser exports and snapshots live in src/editor_browser.cpp; src/main.cpp owns
renderer rebinding before preview teardown. web/editor-fx.js owns DOM/SVG, file reads,
local drafts and downloads, with document-generation checks. Docking moves the same
FX controls into Details; it does not recreate their state.

## Validation

Use the repository-local pinned Emscripten SDK via
`./scripts/build.ps1 -Configuration Release`. Build native targets editor_fx_tests,
editor_tests, editor_content_tests, editor_structure_tests, editor_terrain_tests,
fx_tests, scene_archive_tests and persistence_tests. Run matching CTest names
editor_fx, editor, editor_content, editor_structure, editor_terrain, fx, scene_archive,
persistence. Windows native tests may need the configured compiler bin on PATH.
Run `node scripts/editor-fx-smoke.cjs` against the exact staged Release site with
Playwright/pngjs, BROWSER_EXECUTABLE and DEMO_URL; repeat with DEVICE_SCALE_FACTOR=2.
Use existing editor-structure and FX milestone suites as affected regressions.
See [validation evidence](fx-authoring-validation.md). Desktop browser evidence
does not establish physical mobile acceptance or sustained mobile performance.

Ribbon authoring adds the Ribbon trail preset/group and optional preview source
motion. See [ribbon trails](trails.md); current exports use archive v21.

Surface trail authoring adds a World-only contact-strip preset and the Trails
settings group. Isolated preview supplies sloped contacts and airborne gaps;
diagnostics include live points and existing segment/upload counters. Current
exports use v29 (SurfaceTrail added in v28); see [surface trails](surface-trails.md) for application submission,
receiver policy, bounds and live continuation.

Ground ring and Plane alignment/normal/offset controls are documented in
[plane particles](plane-particles.md). Existing immutable history and preview ownership apply.

Static [mesh particles](mesh-particles.md) are now delivered with shared materials,
3D spin and archive v21 state. Soft fading remains a quad/ribbon capability.

Optional particle collision and independent projected decals are now delivered; see [contact/decal contracts](fx-contacts-decals.md) for responses, queued output, authoring, budgets and persistence. Rigid-body particles and PBR decal channels remain outside this increment.

Event-driven child effects and finite fading decals are now implemented; see [composition contracts](fx-composition.md). Archive v29 adds graph dependencies and live composition/decal continuation while retaining the earlier fields above.
