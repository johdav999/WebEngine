# Staged engine support for driving games

Status: Stages 1–5 implemented and validated, 2026-10-18; see [vehicle contracts](vehicles.md)
and [recorded validation](vehicles-validation.md). Stages 6–9 remain planned. Stage 5: [splines/routes](splines-routes.md) and [validation](splines-routes-validation.md). Stage 4 contracts/evidence: [driving audio](driving-audio.md) and [validation](driving-audio-validation.md). Stage 2 contracts/evidence are in
[physical surfaces](physical-surfaces.md) and [validation](physical-surfaces-validation.md).
Proposed API names in later stages are illustrative until their contract review. This plan follows `architecture.md`; it does not change its implemented
feature inventory.

## Objective and scope

Deliver optional, reusable engine mechanisms for wheeled vehicles and the systems
around them. Games own vehicles' tuning, controls, sound assignments, race/mission
rules, traffic rules, damage consequences, progression and content.

Existing foundations include scene-owned Jolt physics, fixed updates and render
interpolation, queries/sensors, composable cameras, action bindings and touch
controls, spatial audio, terrain paging, asset loading, effects/decals, editor
history/isolated Play, and versioned scene/application archives. These require
integration and extensions, rather than replacement.

Local Jolt 5.3.0 sources contain wheeled vehicle constraints/controllers, suspension,
drivetrain configuration, wheel collision testers and wheel transforms. Stage 1
must establish their exact supported integration with the current scene lifecycle.
Stage 1 now exposes the optional vehicle subsystem described below.

## Rules applying to every stage

- Read current architecture, contracts and source; preserve unrelated checkout work.
- Keep public interfaces portable. Jolt, browser and renderer implementation types
  stay private. New services remain optional and use existing resources/handles.
- Simulation uses authoritative fixed-step state, independently of views, LOD,
  culling and interpolation. Presentation never writes back into simulation.
- Validate replacements before publication. Specify disable, destruction, reset,
  reconfiguration, scene transfer and cancellation behaviour, including borrowed
  references and callback lifetimes.
- Extend typed editor properties and command/history paths. Complex collection
  editing may use a dedicated panel backed by portable commands. Invalid edits
  preserve valid state. Authoring remains gated during isolated Play.
- Deliver built-in codecs/factories where applicable, subtree duplication and
  Play cloning with each new component/resource. Resolve saved references using
  stable identities; never persist runtime handles or solver pointers.
- Distinguish authored configuration, supported resumable runtime state and
  transient solver/presentation data. Saves are not deterministic solver snapshots.
- Allocate archive versions against the current checkout at implementation time.
  Preserve old-file defaults and reject malformed/unsupported data atomically.
  Update all affected host/worker transports together; do not assume they all need
  the same schema change.
- Update README/feature contracts and architecture when delivered responsibilities
  change. Update installed engine-skill and the CatGame copy, preserving their
  specific guidance, before declaring a feature complete.

## Common acceptance gate

Each stage must pass focused native tests and its browser fixture on **forced
WebGPU and forced WebGL 2**. Build using the repository-local SDK and pinned
`.emscripten-version` (currently 6.0.11), following current README commands.

Use the same staged host/Wasm and scripted commands for both backends. Record
actual backend, build revision/configuration, browser, viewport/DPR, commands,
telemetry assertions, inspected screenshots and console/GPU errors. Share the
portable simulation contract; visual parity respects reported backend capabilities.
Allow documented numerical tolerances instead of requiring native/Wasm bit identity.

Browser checks cover the actual editor workflow: create/configure, invalid input,
undo/redo, duplicate, Play/Stop isolation, save/open and application checkpoint
restore. Check DPR1/DPR2 for new interactive panels/handles. Include pause, blur,
host suspension, reset, removal and scene replacement where the feature participates.
Use renderer recovery/resize checks where GPU resources are added or affected.

Run relevant existing regression suites, including archive/editor/lifecycle suites
for new persistent types. Native/Wasm codec checks must exercise 32-bit transport
and malformed input budgets. Retain failure evidence. Existing recorded validation
is baseline context, not a new test run. Desktop/emulated checks do not establish
physical mobile acceptance or sustained mobile performance.

## Stage 1 — Vehicle foundation

**Outcome:** one configurable car drives on a small engine handling course on both
renderers, with complete basic authoring and persistence.

Implementation:

- Define an optional `VehicleComponent`, shared immutable vehicle configuration,
  per-instance state and explicit commands: throttle, steering, service brake and
  handbrake. Define reverse/gear semantics without embedding physical key choices.
- Start with one rigid chassis and four wheels; configurable wheel placement,
  dimensions, suspension, driven/braked wheels, steering limits and a basic
  configurable engine/transmission/differential. Document supported wheel counts
  and leave motorcycles, tracks and articulated vehicles outside this stage.
- Adapt Jolt facilities privately. Scene physics owns registration and constraints;
  the component uses existing generation-checked identities. Specify when commands
  are consumed and telemetry becomes available, and how sleeping vehicles wake.
- Publish speed, RPM, gear and per-wheel contact, normal, suspension travel,
  steering/spin and slip information supported by the solver. Publish a coherent
  post-step snapshot, rather than querying solver state from renderer code.
- Add torque, world-position force/impulse and point-velocity body APIs with
  documented units and spaces. Add centre-of-mass/inertia control with explicit
  coordinate/offset semantics. Avoid duplicate force application when Jolt owns
  vehicle forces; validate analytic off-centre-force behaviour separately.
- Reuse current top-level rigid-body topology and one convex hull/box chassis.
  Establish mass/COM behaviour here; compound geometry follows in Stage 2.
- Provide interpolated wheel presentation aligned with chassis interpolation,
  using configurable visual-node bindings. Visual nodes do not become solver bodies.
  Teleport/reset clears interpolation history consistently.
- Make chassis/body rebuild, disable/re-enable, removal, failed configuration and
  scene teardown safe. Specify whether each operation resets or retains velocity,
  commands, drivetrain state and wheel state; never leave orphan step listeners.

Editor and persistence:

- Add Vehicle creation/removal, validated chassis requirements, wheel-list editing,
  tuning groups, visual bindings and bounded contact/suspension debug drawing.
- Provide a simple vehicle preset as an example, plus read-only live telemetry in
  Play. Stop restores the untouched authoring scene and its history/selection.
- Persist shared configuration and stable visual references. Define separate
  authoring-load defaults and checkpoint restoration for gear/RPM/wheel spin and
  chassis velocities. Do not restore held control input, contact caches or listeners.
- Rebuild vehicles after bodies and saved references resolve. Duplication remaps
  internal bindings and preserves intended shared-resource identity. Restore into
  staged ownership; failed solver construction cannot replace the running scene.

Validation:

- Native: acceleration/braking/reverse, steering direction, suspension settling,
  wheel contact, airborne motion, COM/offset-force tests, invalid settings and all
  body/component/world lifecycle edges. Verify interpolation does not mutate state.
- Course: flat lane, hill, bank, kerb, ramp, wall and reset area. Assert bounded
  braking distance and suspension travel, heading change and stable rest using
  documented tolerances; avoid subjective claims that handling is approved.
- Both backends: repeat identical scripted runs with simulation telemetry, wheel
  alignment/interpolation, follow-camera pixels, visibility/view changes, frame
  gaps, pause/suspension, save/reload and editor workflows. Rendering changes must
  not change vehicle telemetry under the same fixed-step command sequence.

Exit gate: a complete single-car engine example, not a race game, and all common
acceptance checks pass. Do not wait for later stages to make this usable.

## Stage 2 — Physical surfaces, compound shapes and impact data

**Outcome:** reusable physical material behaviour, better chassis geometry and
useful contact data for effects or gameplay consumers.

- Add immutable compound collision shapes with validated child transforms,
  bounded nesting/child counts, shape sharing, queries and correct COM/inertia.
- Add a physical-surface resource/identifier and mapping for bodies, mesh triangles
  and terrain. Visual material assignment must not implicitly change grip.
- Expose surface identity through wheel contacts and appropriate query/contact
  results. Define material combination and tyre/surface friction policy explicitly.
- Add bounded, documented relative-velocity/impact measurements where available;
  distinguish velocity, impulse and force. Consumers choose crash sounds or damage.
- Editor: compound child editing/debugging, physical-surface assets/assignment,
  terrain/mesh mapping and vehicle grip inspection, with history and shared-asset
  edit semantics. Changing a shape follows explicit body/vehicle rebuild policy.
- Persistence: resource deduplication, compound topology, surface tables/mappings,
  old-file neutral defaults and safe unresolved-reference rejection.
- Validation: offset/rotated compounds, COM and query correctness, asphalt/gravel/ice
  lanes with different measured stopping/turning behaviour, terrain seams, invalid
  indices, bounded contact delivery and all common acceptance checks on both backends.

Exit gate: changing visual material leaves physics unchanged; changing physical
surface measurably changes configured traction and survives save/open/Play.

## Stage 3 — Gamepad input and vehicle camera composition

Implemented: [contracts](driving-controls.md), [validation](driving-controls-validation.md).

**Outcome:** analogue control and smooth driving presentation through generic APIs.

- Add a browser gamepad adapter to existing action/context bindings, supporting
  axes/triggers, dead zones, inversion, controller selection and disconnect cleanup.
  Preserve simultaneous-device source ownership and lifecycle input gates.
- Supply portable response/filter utilities only where justified by the example.
  The application chooses steering sensitivity and speed-dependent handling policy.
- Verify current camera targeting against interpolated physics presentation. Add
  explicit presentation-pose targeting and velocity look-ahead if needed; retain
  final camera collision constraints and one camera writer.
- Editor: isolated vehicle-preview controls, camera look-ahead/pose-source settings
  if implemented, and inspectable gamepad status. Runtime permissions/devices are
  host state; the scene editor must not own a second input pipeline.
- Persistence: serialize newly authored camera parameters. Keep device identity,
  held buttons, filter transients and live bindings outside scene archives;
  versioned application settings may store user mappings explicitly.
- Validation: synthetic gamepad browser tests, analogue extremes/dead zones,
  disconnect while accelerating, context switches and multiple sources. Exercise
  chase camera at variable render rates, reverse, barriers and resets on both
  backends. Record any physical-controller checks separately.

Exit gate: keyboard/touch/gamepad feed the same vehicle commands without stuck
input; camera smoothing/interpolation does not modify simulation.

## Stage 4 — Audio playback rate and driving audio example

Implemented: see [audio rate contracts](audio.md#playback-rate) and
[driving example, editor and persistence](driving-audio.md). The bullets below
record the scope; audio scene codecs are not added, and application tuning
reconstructs transient playback.

**Outcome:** reusable variable-rate sound playback plus a configurable example.

- Extend portable voice options/control and backend contracts with validated
  playback rate, including loop/completion/offset semantics and failure behaviour.
- Implement the browser backend without re-creating a voice every frame. Define
  rate-change timing and behaviour through suspension and activation.
- Demonstrate application-owned RPM/load crossfades, tyre-slip loops and surface/
  impact sound selection using existing buses and emitters.
- Editor: an isolated sound/rate preview that respects gesture activation; a
  vehicle example may show live RPM-to-rate values without hard-coding that policy
  into vehicle physics.
- Persistence: keep voices/device state transient. If this stage adds persistent
  audio components/resources, first implement their missing built-in archive,
  asset-resolution and Play-clone support; do not silently drop audio on reload.
  Otherwise explicitly reconstruct example playback from application configuration.
- Validation: native backend-contract tests and real browser node/audio sample
  assertions for rate, loops, completion, mute, suspension and cleanup. Exercise
  activation and example sound on both renderer hosts; listening/device acceptance
  remains separate from automated audio-node checks.

Exit gate: variable-rate voices work independently of the vehicle subsystem and
the example remains silent/clean after scene replacement or Stop.

## Stage 5 — Splines and procedural route geometry

Implemented: [source, sweep, editor and persistence contracts](splines-routes.md).
The bullets below record the delivered scope; long paths use bounded synchronous
rebuilds and explicit rejection rather than automatic streaming or junction meshes.

**Outcome:** generic curves and swept geometry suitable for roads, rails and paths.

- Add validated immutable open/closed splines: position/tangent, arc-length
  sampling and bounded nearest-point queries. Specify continuity and degeneracies.
- Add configurable cross-section sweep/extrusion, stable frames/banking, UV distance,
  bounds and optional static collision. Build candidates before publishing; choose
  explicit bounded rebuild/admission policies for long paths.
- Keep lane rules, racing lines and road-network junction semantics in applications.
  General intersection solving and automatic road junction meshes are deferred.
- Editor: point/tangent editing, bank/profile controls, snapping, grouped undo,
  duplication and geometry/collision preview. Terrain conformity is an explicit
  sampling operation, not a live hidden dependency.
- Persistence: spline control data, sweep/profile resources and references; rebuild
  derived meshes under load budgets. Terrain dependencies and any baked samples
  have explicit ownership/versioning.
- Validation: curve endpoints/loops, arc-length error, nearest-point limits,
  vertical curves/frame flips, winding, banking, UV seams, collision agreement,
  malformed files and edit/save/Play workflows on both renderers.

Exit gate: a short authored curved/banked road drives correctly, with collision
independent of camera LOD and resource sharing preserved after reload.

## Stage 6 — Surface-following trails

**Outcome:** generic bounded contact strips for tyre marks, tracks and similar traces.

- Add surface-oriented strip/trail data with supplied contact normals, world-space
  samples, width, texture distance, breaks, fading and bounded point/triangle counts.
  Reuse existing effect/material resources; do not repurpose camera-facing ribbons
  without an explicit alignment contract.
- Applications supply contact/slip samples and emission policy. Define receiver
  movement, terrain edits, eviction and unsupported surface behaviour explicitly.
- Editor: preset/parameter controls and an isolated sloped-surface preview, with
  budgets and diagnostics. Retain current decal workflows for discrete marks.
- Persistence: authored definitions; define optional bounded live-strip continuation
  separately, including age/clock semantics. Render caches remain transient.
- Validation: hills/banks, airborne gaps, resets/teleports, sampling at different
  frame rates, capacity eviction, texture continuity, depth/fog/transparency,
  archive continuation and both-renderer editor/recovery checks.

Exit gate: stable ground marks follow supplied surface normals and stay within
documented CPU/GPU budgets without affecting collision.

## Stage 7 — Spotlights

**Outcome:** reusable cone lights suitable for headlights, torches and scene lighting.

- Extend light contracts with validated direction, inner/outer cones and distance
  attenuation. Define supported light budgets and shader limits on both backends.
- Implement both renderers with capability reporting. Unshadowed cones are the
  initial deliverable; spotlight shadows are a separately scoped extension.
- Editor: light type, cone/range controls, oriented bounds/gizmos, history and
  isolated Play. Headlight mounting uses ordinary hierarchy transforms.
- Persistence: new light fields/type, defaults for older files and duplication.
- Validation: cone boundaries, transformed parents, attenuation, invalid settings,
  multiple views, masks, budgets, save/open and both-backend pixels/recovery.

Exit gate: headlights visibly illuminate a test scene on each backend within its
reported lighting limits; no claim of spotlight shadow support.

## Stage 8 — General region streaming

**Outcome:** optional scene-content and collision residency beyond terrain pages.

- Define region manifests, application-owned request priorities/interests, staged
  publication, bounded admissions, cancellation and eviction. Reuse asset loading,
  terrain ownership and existing scene lifecycle instead of adding another graph.
- Separate visual readiness from required collision readiness. Expose readiness
  and failure so games can gate movement or loading explicitly. Prefetch direction
  and safety policy belong to the application.
- Pin regions for active simulated bodies; define crossing, teleports, persistent
  identity and references to unloaded content. Do not evict support under a vehicle.
- Editor: region bounds/manifest authoring and live residency/budget diagnostics;
  preview streaming must not delete source authoring content.
- Persistence: authored manifests/references plus explicitly selected application
  world state. Runtime jobs, requests, caches and stale handles are not checkpoints.
  Restore required regions before publishing a playable state.
- Validation: delayed/failed loads, cancellation, rapid focus changes, border
  crossings, teleport, memory/budget bounds, active-body pinning and save restoration
  with missing content. Run high-speed traversal on both renderers with telemetry.

Exit gate: the fixture cannot silently drive into missing required collision;
late results cannot resurrect evicted/replaced scene content.

## Stage 9 — Optional vehicle path following

**Outcome:** reusable command-producing path control, when an actual AI consumer needs it.

- Build on spline/path sampling and vehicle commands/telemetry. Respect curvature,
  steering limits, braking distance, stopping and path loss; add bounded obstacle
  probes and cancellation. Do not reuse capsule-character navigation as a car driver.
- Games own route choice, traffic lights, lanes/right-of-way, overtaking, police
  pursuit and racing tactics. Global traffic simulation is outside this stage.
- Editor: path/vehicle bindings, look-ahead/speed constraints and debug targets.
- Persistence: authored bindings/parameters; explicitly choose resumable progress.
  Recreate runtime controllers and revalidate support/residency on restore.
- Validation: curves, hairpins, braking/stops, obstacles, unreachable routes,
  disable/removal, restored bindings and identical command semantics across both
  renderer fixtures. Bound queries/work per vehicle and across the example fleet.

Exit gate: a vehicle follows a configured path safely within declared assumptions;
this is not acceptance of city traffic or competitive racing AI.

## Delivery order and completion criteria

Implement Stage 1 first, then Stage 2. Stages 3 and 4 make the foundation useful
for a polished prototype. Stage 5 precedes spline-based authoring and Stage 9.
Stages 6 and 7 can follow independently once their consumers exist. Stage 8 is
conditional on world scale; it is not a prerequisite for a closed-course game.

Split a stage into smaller changes when necessary, but retain its exit gate. Each
handoff lists delivered APIs/defaults/limitations, editor and persistence coverage,
actual native/Wasm/WebGPU/WebGL2 checks, evidence paths and outstanding failures.
Do not call a stage complete while its required renderer, authoring or save path
is deferred. Stage 1 implementation and runtime evidence are recorded separately in vehicles-validation.md; later stages have no implementation claim.
