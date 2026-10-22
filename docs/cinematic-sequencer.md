# Cinematic sequencer

Open **`?scene=cinematic`** for the greeting example: a moving, waving character,
two camera shots, a streamed sound cue and a fountain accent on an eight-second
timeline. The **Cinematic sequencer** panel is also available in the normal
inspector and the scene editor. **Expand timeline** gives it a wide desktop
workspace; narrow screens keep the same controls in the inspector.

## Authoring

Close preview to edit. A sequence has its own name, duration, frame grid and
bounded Undo/Redo history, independent of scene history. Choose a track type and
target, then **Add track**. Saved node identities bind motion, shots, characters
and effects; sound tracks reference encoded audio URIs. The example provides all
five kinds without requiring a model download.

- **Object motion:** local position, Euler rotation and positive scale keys.
- **Camera shot:** a camera, an active time range, pose keys and lens keys. Lens
  is vertical radians for its existing perspective projection or height for its
  existing orthographic projection. Shots cut at boundaries; they cannot overlap.
- **Animation:** a SkeletalMeshComponent, a unique existing clip name, source offset,
  speed and optional source looping. Clip poses include authored root motion;
  preview does not extract gameplay movement or emit animation marker events.
- **Sound:** a project-relative/HTTP/HTTPS audio URI, time range, source offset,
  volume, speed and optional source looping. The browser owns media decoding.
- **Effect:** an existing EffectComponent and its active range. Its immutable
  EffectAsset owns emission, duration, looping, seed and composition settings.
  Per-clip FX rate/offset overrides are not supported.

Select a row or key diamond to edit exact values. Drag a clip to move it, its
right edge to resize it, or a diamond to retime a key. Dragged times snap to the
sequence frame grid; resizing scales the track's keys with its range. Each
completed drag is one native history command. Escape, pointer cancellation or
blur discards an unfinished gesture. Native validation rejects duplicate key
times, conflicting writers, overlapping shots and invalid settings while keeping
the last valid document/history. Disabled tracks retain metadata but need not
resolve their scene bindings for preview.

**Add / capture target at playhead** records the selected source node's current
local transform and camera lens. Edit the source through ordinary scene tools,
then capture; it does not copy an arbitrary preview pose back into the scene.
Key fields support Linear, Smooth (smoothstep) and Hold/cut interpolation. Position,
scale and lens interpolate numerically; Euler angles take the shortest arc on each
axis. This is not quaternion spline interpolation or an authored multi-turn curve.
Parent and child motion compose through the normal hierarchy. Gaps restore the
source's local transform; animation gaps use the character's bind pose.

**Save .wecin** writes a separate `WECINE 1` text asset. It contains track/key data,
saved node IDs, clip names and audio URIs, not a scene, meshes, animations, effects,
audio bytes, callbacks or live playback. Save the scene/resources separately and
keep project audio paths available. Import parses and validates a complete candidate
before one document edit; corrupt/stale asynchronous loads preserve the valid asset.
Saved references can be repaired through the target/clip fields in another scene.

## Playback and scrubbing

**Open preview** creates an isolated built-in scene copy using SceneArchive::clone.
The source, source animation state, scene selection/history and original view poses
remain unchanged. Immutable scene resources are shared; mutable nodes, animation
and FX are independent. The browser pauses the source and rejects conflicting
editor Play/FX/region previews or camera input. Closing rebinds source views before
destroying preview cameras, releases preview audio and restores the prior pause policy.
Saved runtime camera handles are re-resolved on return; removed cameras are omitted.
A source scene replacement closes the stale workbench preview safely.

Drag the ruler/playhead, use the scrub slider or enter exact seconds. Scrubbing
pauses and is silent. **Step frame** is also silent, advancing one frame on the
authored grid. Focus the SVG timeline and use Left/Right or Space. Play starts from
the cursor; reaching duration holds the final visual frame and stops sound. Rewind
returns to zero. Play at the end restarts. Escape closes preview when no edit drag
is active. Source transforms and physics remain independent of presentation choices.

Animation poses sample existing AnimationComponent clips at absolute source time
with their tick clocks disabled in the clone. No controller, physics, AI or gameplay
fixed step is run. Other cloned animations remain frozen. Motion targets containing
rigid bodies, motors or vehicles, managed implementation children, and cameras with
an enabled follow writer are rejected. Use visual children or separate cinematic
actors instead of assigning competing simulation writers.

FX sample a canonical **60Hz** grid. Scrubbing clears/reset-seeds controlled effects
and replays only the active clips' windows, never the whole film. Each FX clip is
limited to **10 seconds**, so a reconstruction needs at most **600 scene variable
steps**. Forward Play advances that same grid incrementally; active-clip changes
or loop wraps reconstruct it. Motion and animation are sampled along the replay
path so moving World-space emitters produce repeatable particles. Inactive effects
are cleared. Existing effect/collision/composition budgets still apply; this is a
bounded replay cost, not a constant-time or frame-time guarantee. Physics bodies
are synchronized/frozen, without solver steps. Bound effect graphs are limited to
128 unique assets. Contact-driven surface trails need application contact input
and are rejected by this preview.

Audio belongs to a preview-owned optional AudioSystem and uses existing stream
voices, gain/rate/loop and activation/suspension contracts. Only enabled sound
tracks create an audio owner; silent sequences can play without an audio device.
Play requests activation from the user's gesture. While activation is blocked,
the audio-enabled preview clock waits. Host suspension pauses media and freezes
the timeline; returning needs another Play gesture. Seek, Pause, Rewind, Close and
completion stop owned voices. Active clips start at their timeline-relative source
offset; failed/rejected clips are not blindly restarted each frame. Non-looped
streams correct cursor drift over .2s; looped initial offsets wrap after metadata
reveals duration. Browser errors are visible in the sequencer status. Keep a
non-looped clip's range inside its source duration. CORS, codecs and buffering
remain browser policy; no full PCM import or sample-accurate offline audio mix is
provided. See [environmental audio](environmental-audio.md).

## Portable APIs and ownership

```cpp
#include "engine/cinematic_sequence.hpp"
#include "engine/editor_cinematic.hpp" // Optional engine_editor.

auto asset = CinematicSequence::create(data); // Complete validation, immutable.
CinematicPlayer clock(asset);               // engine_core, no scene/device owner.
clock.play();
clock.advance(eligibleDelta);                // 0..0.25s, explicit host clock.
auto pose = asset->sample(trackIndex, clock.time());
// Runtime applications bind saved targets/resources and choose their own handoff.

CinematicSequenceEditor document(asset);     // Independent 128-edit history.
CinematicPreview preview(source, views, asset, std::move(optionalAudioBackend));
preview.seek(3);                            // Silent absolute visual sample.
renderer.setScene(preview.scene(), preview.views());
// Play/advance from the chosen presentation clock; request audio activation in a gesture.
// Release renderer scene/camera borrows before destroying preview.
```

`engine_core` owns immutable value assets, validation, sampling, file parsing and
the `CinematicPlayer` clock. `engine_editor` owns independent document history and
the optional isolated preview. The preview owns its scene and optional audio
backend/system; it borrows the source only during construction, then uses ordinary
scene-scoped handles. Renderer references stay borrowed. The browser workbench is
application code in `src/cinematic_browser.inc`, `src/cinematic_example.hpp` and
`web/cinematic.js`; it introduces no DOM/renderer types into portable interfaces.
No second source-scene update, global subscription or automatic gameplay cutscene
component is added. Runtime games can consume the asset/player through their own
binding and gameplay completion policy; no authored gameplay events are executed.

Limits: duration **.01..3600s**, frame grid **1..120fps** (not a render-rate cap),
**64 tracks**, **256 keys/track**, **4096 total keys**, **8 simultaneous sounds**,
names/IDs up to256 bytes, audio references4096 bytes and files **1MiB**. Preview
cloning uses at most10000 nodes and a256MiB archive-allocation budget. Source
components must be supported by built-in cloning or explicitly supplied codecs.
All metadata/settings are transient or separate `.wecin` files; no scene archive
version, built-in sequencer resource record, playback checkpoint or worker codec
change is introduced. Nested sequences, shot crossfades, Bezier curves, audio
waveform editing, offline/video export and automatic recording/apply-back remain
extensions.

## Validation

Use the pinned local Emscripten **6.0.11** and README builds. Native target
`cinematic_sequence_tests`, CTest `cinematic_sequence`; explicit Wasm target
`cinematic_sequence_wasm_tests` runs with Node. Build `webengine web_assets` to
publish matched host/worker/main files. Run `scripts/cinematic-sequence-smoke.cjs`
serially on WebGPU/WebGL2 with DEVICE_SCALE_FACTOR1 and2. It exercises rendered
poses/cuts/FX determinism, real audio, source byte preservation, gestures/history,
files/invalid retention, device recovery, editor preview exclusion, compact controls
and shutdown. Regress animation control, FX composition, audio and relevant editor
flows. Desktop/DPR samples do not establish physical mobile, native rendering/audio,
Safari or sustained performance. See [recorded evidence](cinematic-sequencer-validation.md).
