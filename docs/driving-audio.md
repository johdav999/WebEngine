# Driving audio example

Stage 4 composes the optional AudioSystem with vehicle telemetry and physical
surfaces. `src/driving_audio_demo.cpp` is application code, linked only into the
browser playground. The portable audio service implements variable-rate voices;
vehicle physics has no sound assignments, audio dependency or RPM sound policy.

Open `?scene=vehicle` (or `&surfaceTest=1`) and expand **Driving audio and rate
preview**. Enable driving sound during simulation or editor Play. Drive using the
existing keyboard/gamepad/preview controls. Sound remains opt-in after reload;
press Enable again after host suspension. These quiet synthesized tones/noise are
illustrations of the data path, not finished vehicle recordings.

## Mapping and ownership

One application-owned AudioSystem has Engine, Tyres and Impacts buses, with a
16-voice ceiling. Four retained spatial loops share a private proxy emitter scene:
80/160 Hz idle/load tones crossfade linearly by absolute commanded throttle
(`.65*(1-load)` / `.65*load`). Both use clamped `rpm/referenceRPM` playback rate.
A deterministic noise loop uses maximum absolute contact-wheel longitudinal or
lateral slip times slip gain, clamped to [0,1]. A second noise loop plays on gravel
contact, with gain `abs(speed)/15` clamped to [0,1] and rate `abs(speed)/10` clamped
to [.5,2]. Airborne wheels contribute no slip/surface gain. These values represent
telemetry, not an acoustic tyre model. The listener sits near the authoritative
car position, independently of render interpolation and camera choice.

The application prepares one scene-owned collision forwarding callback, preserving
an existing callback and checking its concrete wrapper type to avoid repeated
wrapping. It captures no borrowed scene/node/device pointers. Begin contacts for
the currently selected vehicle actor, with valid closing speed >=1 m/s, create a
positioned one-shot at the contact. Gravel selects a short decaying noise sample
at .7 rate; other surfaces select a decaying tone at 1.2 rate. Gain is closing
speed/12 clamped to 1, with a .15-second application cooldown. This is not damage,
force or impulse. Replacing the host callback explicitly replaces this forwarding
integration; the next preparation reattaches it while preserving that callback.

Audio updates after simulation using presentation delta. Component/actor handles
identify the current vehicle, and the private scene owns proxy emitters. No source
scene pointer is retained. Pause, missing/disabled vehicle or edit mode releases
voices. Scene replacement, editor unbind/Stop and shutdown stop and disarm driving
sound before releasing scenes. The next Play requires enabling it again. Backend
suspension freezes device time and requires a fresh activation gesture; repeated
unchanged suspend calls preserve a pending activation. Shutdown releases both the
preview and driving audio devices. Budgets/start failures do not publish partial
voices; looping starts retry while enabled if the backend declined them.

## Editor, tuning and persistence

The Details panel provides reference RPM [500,8000] (default 2000), slip gain [0,2]
(default .3), driving volume [0,1] (default .4) and isolated preview rate [.125,8]
(default 1). Preview tone uses the existing Audio system's private sound/scene;
importing a shared sound there replaces the future preview asset. Gesture buttons
activate and stop the preview. Scene source data is untouched. Sound buttons work
during Play; tuning controls are disabled until Stop. Undo/Redo tuning is a
separate bounded 64-entry history, independent of scene history.

Tuning persists under localStorage `webengine.drivingAudio.v1` and can be exported
as a `.wedriveaudio` JSON document. Version 1 has exactly `version`, `baseRPM`,
`slipGain`, `volume`, `previewRate`. Load is bounded to 4 KiB, validates all fields
before publishing, cancels stale asynchronous results, and preserves previous
tuning on invalid data. Storage failures report that tuning is active but unsaved.
There is no scene ID association: one host tuning document applies to the selected
vehicle. Transfer scene and tuning files together when sharing this example.

Stage 4 introduced no archive fields; the current writer is **27** after Stage 5. No new persistent component or SoundAsset
codec is introduced. Existing scenes retain their usual vehicle data; rebuilding
the example reconstructs synthesized assets, buses, emitter proxies and mappings
from application code plus tuning. Voices, cursor/rate automation, device state,
held inputs and playback-enabled state are transient and are never restored.
Custom game emitters/listeners in scene archives still require explicit codecs
and resource resolution; unknown component rejection remains intact.

## Validation

Run native CTest `audio` (extended mock backend contract) and the full suite.
`scripts/driving-audio-smoke.cjs` forces both renderer hosts and asserts real Web
Audio frequency samples, retained source identity, queued activation, offsets,
rate-dependent completion, mute, suspend/resume, real car slip/surface/collision
mapping, tuning history/local/file reload, isolated editor preview/Play, source
hash, scene replacement and shutdown. Run again with `DPR=2` and inspect panel
screenshots. `scripts/audio-system-smoke.cjs` covers the pre-existing audio flows.
The opt-in `vehicleTest=1` fixture positions a real car for repeatable contact/barrier
checks; it does not synthesize collision/audio results. These checks do not prove
physical speakers, subjective sound quality, controller hardware or physical
Android/iOS output. See [recorded evidence](driving-audio-validation.md).
