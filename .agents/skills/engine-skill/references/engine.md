# Using WebEngine in a game

This is the game-integration reference for the `engine-skill` Codex skill. It describes how to compose a game from WebEngine, not a requirement to enable every subsystem.

Baseline inspected: 2026-10-25, WebEngine commit `3b7c0ad7ce56b955a31d0e5ef99f990b72a9a96a`. There were concurrent uncommitted RPG and simulator changes. Those changes are not a released capability promise. Consumer repositories may pin an older revision.

## Start with the game's actual dependency

1. Read the game's `AGENTS.md`, architecture/design documents and build configuration. Identify its shipped platforms and the feature requested.
2. Resolve **engine root** from the game's submodule, `WEBENGINE_SOURCE_DIR`, or FetchContent source directory. Inspect the pinned revision and any intentional override. Do not silently substitute the sibling WebEngine checkout or advance a pin.
3. Read `architecture.md` in that engine root and the relevant contracts below. Paths such as `docs/assets.md` and `include/engine/application.hpp` in this guide are relative to **engine root**, not to this skill's installation folder.
4. Verify public headers, exported CMake targets, implementation and tests at that revision. A feature present in this guide but absent from the consumer pin requires an explicitly scoped engine upgrade or a design using available APIs.
5. Separate current implementation, roadmap, local uncommitted work and recorded historical validation. Never present a design document or a passing test from another revision as proof of the current game.

The repository source and the consumer's own instructions take precedence over this snapshot when they differ. Resolve discrepancies before depending on an API.

## Ownership: engine mechanisms, game rules

| Owner | Responsibilities |
| --- | --- |
| Platform host | Window/canvas, event polling, rendering, platform IO, audio backend, durable storage and lifecycle integration |
| `Application` | Loaded scenes, input snapshots, simulation/fixed/presentation clocks, asset preparation and scene transitions |
| `Scene`, actors and components | Scene-local entities, transforms, component lifecycle, physics membership and rendering representations |
| Game session | Persistent game state, economy, inventory policy, campaign/shift progression, commands, content selection and save migrations |
| Optional engine libraries | Generic mechanisms such as module composition, quest evaluation, navigation and networking, enabled when needed |

Keep state that must survive level changes in an application-owned game session. Scenes project that state into actors and UI. Do not make a transient actor the sole owner of the player's wallet, campaign progress or cafe shift state.

Use existing scene-scoped actor/component handles for cross-frame references. Resolve them after transitions and asynchronous work; do not retain raw scene or camera pointers across unload. Game save IDs, quest IDs and network entity IDs are different identities from runtime handles.

Published meshes, textures, skeletons, clips, prefabs and definitions are shared immutable assets. Transform, material overrides, morph weights, playback and game-session state are per instance. Stage and validate replacements before publishing them; failure must preserve valid live state.

## Compose the host and build

Use public CMake targets. Do not copy private renderer source lists, SDL/Dawn/D3D12 headers or platform implementation files into the game.

| Use | Integration seam |
| --- | --- |
| Portable scene/game code | `engine_core`; enable optional libraries only for required features |
| Browser renderer | `WebEngine::Browser` owns renderer sources, embedded shaders, WebGPU port and WebGL2 requirements |
| Browser services | Link the needed `engine_assets_browser`, `engine_audio_browser`, `engine_gui_browser` adapters at the host boundary |
| Windows host | `WebEngine::Desktop`, public `engine/desktop.hpp`; enable `WEBENGINE_BUILD_WINDOWS` |
| Windows services/distribution | Consult `WebEngine::Platform`, `WebEngine::WindowsRuntime` and the desktop/distribution contracts at the pin |
| Source-linked optional plugins | `WebEngine::Modules`; consumers may configure `modules/` without the renderer |
| Quest kernel | `WebEngine::Quests`, enabled via `WEBENGINE_BUILD_QUESTS`; consult the independent `examples/quests` consumer |
| RPG foundation | `WebEngine::RPG` only when delivered by the chosen revision; see the status limits below |

DesktopHost owns its Application, scene binding and frame loop. Use `host.application()`; do not create a second loop or replace the host's scene binding. Register camera views with `host.setViews(sceneId, views)` before starting or transitioning into that scene. Use `host.run(callbacks)` or `host.step(...)`, not an additional `Application::advance` inside callbacks.

A browser host owns its Application/render binding and invokes `advance` once per frame with monotonic seconds. Convert RAF milliseconds to seconds. Adapt browser input, audio and storage at that boundary. Follow `docs/consumer-hosts.md`, `docs/windows-desktop.md` and the sample closest to the game's host rather than copying the engine playground wholesale.

## Frame, input and transition rules

Application frame order is pending controls and asset publication/preparation/transition, input snapshot, variable simulation, fixed simulation, `afterSimulation`, then presentation. Read `docs/application-lifecycle.md` before altering this order.

- Gameplay timers use simulation time. Pause keeps input/presentation available but freezes simulation. Suspension freezes all clocks; do not catch up hidden-tab time using wall-clock deltas.
- Use fixed component ticks for movement/physics that requires fixed steps. Application already advances managed scenes and physics; do not step them again.
- Consume input actions, not platform key codes in game rules. Held input is a rate multiplied by delta; pointer displacement is already a delta and is not multiplied again. Respect GUI ownership, input cancellation and context changes.
- Apply presentation-only interpolation, camera smoothing and visual corrections without changing authoritative simulation state.
- Prepare incoming scenes/assets, register their views, and request transitions through Application. Load/unload and immediate commits require an idle boundary. A requested transition may wait for preparation while the old scene remains active.
- Release UI callbacks, platform subscriptions and rendering borrows before their captured session/scene is destroyed. Keep the host alive through Application shutdown.

For example, brewing time belongs to simulation, a menu hover may use presentation time, and a turn-based encounter advances only under the game's explicit turn/action policy. A render frame must not grant a reward or advance a turn simply because it was drawn.

## Choose an existing system first

Read only the rows relevant to the feature being built. These are routing hints, not an exhaustive list of guarantees for every backend or dependency revision.

| Game requirement | Mechanism and integration guidance | Engine-root references |
| --- | --- | --- |
| Level/room loading | Application scene ownership, preparation and deferred transitions; game owns destinations and travel policy | `docs/application-lifecycle.md`, `docs/assets.md` |
| Repeated props, furniture, NPC templates | Shared meshes/materials and Prefab definitions; instance overrides remain separate | `docs/prefabs.md`, `docs/prefab-editor.md` |
| GLB/FBX, textures and async loading | AssetManager requests, preparation, cancellation and explicit lifetimes; publish scene changes on the owner thread | `docs/assets.md`, `docs/mobile-assets.md`, `docs/windows-background-assets.md` |
| Picking, interaction and placement | InteractionComponent, selection and ManipulationController; game decides eligibility, price, placement rules and commit/cancel effects | `docs/selection.md` |
| Player locomotion and collision | Character motor, colliders, rigid bodies and Jolt queries; separate movement commands from device input | `docs/movement.md`, `docs/physics.md`, `docs/physics-joints.md` |
| Orbit/follow/third-person cameras | Composable CameraController behaviours, collision avoidance and transitions; game supplies targets and camera policy | `docs/camera-controllers.md` |
| Characters and animation | SkeletalMeshComponent, AnimationComponent, controllers, layers, IK, root motion and morph targets | `docs/animation-control.md`, `docs/animation-authoring.md`, `docs/inverse-kinematics.md`, `docs/animation-retargeting.md` |
| NPCs and customers | State machines, typed blackboards/events/tasks, navigation, character commands and optional crowd coordination; game owns goals, schedules and service priorities | `docs/ai.md`, `docs/navmesh.md`, `docs/ai-controllers.md`, `docs/ai-tasks.md`, `docs/crowd-navigation.md` |
| Menus/HUD/settings | Retained GUI, measured layouts, themes and custom widgets; browser HTML UI is a host choice, not portable gameplay | `docs/gui.md`, `docs/gui-layout.md`, `docs/gui-themes.md`, `docs/ui-input-integration.md` |
| Keyboard/touch/gamepad/accessibility | Input actions/contexts and supported adapters; ensure menus consume input before gameplay | `docs/driving-controls.md`, `docs/accessibility-input.md` |
| Scene look/materials | Shared material resources and instance overrides; consult backend capabilities before using PBR, post-processing or shadows | `docs/materials-validation.md`, `docs/material-graph.md`, `docs/rendering-fallback.md`, `docs/lighting-post-processing-validation.md` |
| Steam, impacts, spells and trails | Particle/effect definitions, effect components, decals and trails; game triggers them from accepted simulation events | `docs/fx.md`, `docs/fx-composition.md`, `docs/fx-contacts-decals.md`, `docs/trails.md` |
| Music, sound and environments | AudioSystem and platform backends; game owns sound policy and service lifetime | `docs/audio.md`, `docs/environmental-audio.md`, `docs/windows-audio-environment.md` |
| Terrain, water and outdoor worlds | Terrain/streaming resources, water and sky; game owns world layout, loaders, budgets and movement gating | `docs/terrain.md`, `docs/hybrid-terrain.md`, `docs/region-streaming.md`, `docs/water.md`, `docs/sky.md` |
| Large planetary worlds | Optional Planet library; bounded source-driven patches and radial physics, not a ready-made seamless RPG world | `docs/planet-terrain.md`, `docs/planet-physics.md` |
| Vehicles and routes | VehicleComponent, physical surfaces, splines and swept routes; game owns handling presets, races and progression | `docs/vehicles.md`, `docs/physical-surfaces.md`, `docs/splines-routes.md` |
| Quest chains/objectives | Optional quest catalogues, bindings, transactions and snapshots; game owns rewards and persistence policy | `docs/quests.md`, `docs/quests-host.md`, `docs/module-sdk.md` |
| Saves/checkpoints | Scene/Application archives, custom component codecs and platform byte storage; game owns its durable save envelope | `docs/persistence.md`, `docs/scene-archives.md` |
| Cutscenes/replays | Sequencer and recorded poses/events; explicitly choose simulation and playback clock policies | `docs/cinematic-sequencer.md`, `docs/replay-recording.md` |
| Multiplayer | Optional transport/session/scene adapters and bounded replication; server owns authority, game owns schemas and rules | `docs/multiplayer.md`, `docs/multiplayer-runtime.md`, `docs/multiplayer-components.md`, `docs/multiplayer-physics.md` |
| Profiling and quality | Capability-aware quality, adaptive settings, LOD and timeline measurements; measure the actual target device | `docs/rendering-quality.md`, `docs/adaptive-quality.md`, `docs/profiling-timeline-validation.md` |

## Integration details that prevent common mistakes

### Assets and rendering

Asset IO/decode may be asynchronous, but worker completions must not mutate the scene or GPU directly. Use AssetManager's owner-thread publication and generation/lifetime checks. Retain the previous visible asset while a replacement is loading or invalid. Dispose request interests/callbacks when the consumer disappears.

Preparation readiness does not automatically imply GPU prewarming. Geometry/material caches are renderer-owned. Preserve asset licenses and the game's reviewed asset pipeline; content generation services are authoring tools, not runtime dependencies.

WebGL2 is a capability-limited fallback, not feature-identical WebGPU. Windows Dawn and direct D3D12 also have distinct supported scopes. Read `docs/windows-d3d12.md` and `docs/windows-graphics-fallback.md` for the consumer's backend; do not infer support from another renderer's success. Do not rely on existing static-mesh LOD to simplify every skinned or morphed character automatically.

### Animation and clothing

Consume extracted root motion once and let game movement/collision policy decide how to apply it. Do not move an actor twice by applying both root motion and an independent locomotion displacement for the same step.

Clothing milestone one adds optional `WebEngine::Clothing` (`engine/clothing.hpp`): interchangeable shared-rig garments, one driver palette, rigid single-bone accessories, authored body-region hiding, per-instance color and bounded `WECL 1` saved selections. Read engine-root `docs/clothing.md` and `docs/clothing-validation.md`. Require the exact shared skeleton/binding table and mesh bind space; matching bone names alone is insufficient. Apply outfits at idle boundaries and synchronize in `present`, including paused frames. Generated nodes are archive-transient and owned beneath the driver. Inventory policy, automatic fitting, physical cloth and automatic body-morph propagation are outside this milestone. Consumer pins predating this change do not have these APIs.

### NPCs and GUI

Navigation supplies paths and movement mechanisms, not a customer's ordering policy or an RPG faction system. Dynamic obstacles/source revisions may invalidate paths; handle unreachable goals, cancellation and bounded retries. Keep decision clocks tied to simulation, and do not embed cafe recipes or quest-specific dialogue into engine state-machine code.

`GuiSystem::clear()` is an idle-only rebuild operation. From widget callbacks, mark UI dirty and rebuild after dispatch; do not destroy the active widget tree during its callback. Respect `guiConsumed` and cancellation in desktop pointer/wheel handlers. Use public widget and input APIs rather than editing private host state.

### Persistence and optional gameplay libraries

Save a versioned game envelope with content revision, game-owned state and the necessary engine/quest snapshots. Reconstruct custom codecs/bindings before restoring. Validate the entire candidate before replacing live state; preserve the original save before migration and use appropriate atomic platform storage. Scene serialization alone does not save every custom game component or external game service.

Module providers, immutable catalogues and sessions have explicit lifetimes. Keep captures owned; existing sessions may retain old catalogues after composition changes. Modules are source-linked C++ composition, not a dynamic DLL/Wasm plugin loader.

Quest evaluation produces bounded candidate changes/effects. Apply wallet, inventory or other game effects together with the accepted quest transaction; save their resulting states together. Follow the exact transaction API at the pin, including receipt/idempotency rules. Do not reward both an event listener and a quest completion handler for the same accepted action.

The documented RPG R1 foundation supplies typed identities, catalogue/provider validation and independent runtime ownership. The R1 documentation does not establish inventory, combat, progression, full character simulation, snapshots or RPGQuests as delivered. Uncommitted later-stage headers are not proof that a consumer can use them. Likewise, a simulator proposal or in-progress target is not a shipped cafe-economy framework. Check current contracts, source, build options and tests before adopting either library; keep game policy in its game session.

Networking is optional. Do not introduce it into a single-player task. Existing adapters do not imply general deterministic physics rollback, production identity, matchmaking or internet deployment.

## Build and verify at the right scope

Use the consumer's build commands first. A sibling WebEngine demo build does not validate the consumer executable, dependency pin, asset staging or lifecycle.

For WebEngine itself, the checked-in browser workflow uses the repository-local SDK version from `.emscripten-version`:

```powershell
./scripts/setup.ps1
./scripts/build.ps1 -Configuration Release
./scripts/serve.ps1
```

These scripts require usable Git, Python, CMake and Ninja on PATH. Do not mistake a Windows Store alias for a working Python runtime. Use a temporary shell PATH or the known local runtime when necessary. Check disk space before new SDK or native dependency downloads. Do not reinstall a working SDK just to update documentation.

Native desktop setup/build uses `scripts/setup-windows.ps1` and `scripts/build-windows.ps1` with the supported Visual Studio C++ toolchain. Consult those scripts at the pin for generator/backend options and the Python requirement. Browser and native builds need separate build trees.

For portable engine changes, use focused CTest suites. Renderer-free `examples/modules`, `examples/quests` and supported `examples/rpg` consumers exercise their own libraries, not the whole engine. A passing Node/Wasm kernel test does not validate GPU rendering or the native host.

For input, rendering, host or scene changes, also exercise the affected game flow: scene activation/transition, pause/resume, asset failure, repeated entry/exit and shutdown. Run relevant browser smoke checks on WebGPU and WebGL2 and native Windows checks for engine changes. Test the game's selected Windows backend explicitly. Record unsupported/unavailable checks without implying another target proves them.

Documentation-only work needs skill validation, link/source-reference checks and preservation of local project guidance; it does not require rebuilding the engine. Report the commands actually run, their results and remaining limitations. Existing validation documents are historical evidence, not a fresh run. Desktop emulation does not establish physical-mobile acceptance.

## Maintaining this skill

The canonical guide is WebEngine's root `engine.md`. The discoverable package is `.agents/skills/engine-skill/SKILL.md` with a bundled `references/engine.md` copy. When this guide changes, synchronize that reference into the requested consumer skill packages and preserve their `references/project-context.md` guidance. Keep the snapshot revision/status accurate; review new functionality against source before advertising it. Follow applicable repository instructions for any separately maintained skill copies when extending engine functionality.

## Simulator production integration (local stages 2–3)

The local optional `WebEngine::Simulator` target now provides inventory holds,
process definitions, stations, workers and FIFO jobs. This uncommitted source is
not available through CafeGame's current dependency pin. Read `docs/simulator.md`
and `docs/simulator-production.md` and inspect the headers before adopting it.

Use `defineProcess`, createStation/createWorker and enqueue with game-owned data.
Call assign explicitly; it atomically holds input/output capacity and worker/slot.
Map the returned Job handles/token to existing navigation; arrive starts production,
movementFailed terminates failed travel, and assign(true) is an immediate-arrival
adapter. Cancellation after start never refunds inputs. Terminal calls cannot
produce or consume twice. Remove cancels affected jobs before releasing resources.

Advance returns consumed/remaining milliseconds plus blocked/completed; retain and
retry pending time after draining events, including blocked results with zero
remaining time. Assign is not automatically invoked by advance. Call ordering and
timestamped host inputs define determinism; do not invent another wall clock or
step the engine scene/physics twice. Pause stops simulation time. The outbox and
record limits are visible backpressure, not permission to drop transitions.

The standalone examples/simulator CMake consumer configures no renderer/physics.
Use the pinned SDK, test Node and browser Wasm and run the same contracts with a
native C++20 toolchain. Current local browser checks do not establish native or
hardware-renderer support; consult docs/simulator-production-validation.md. Service queues and payments are described in stage 4 below; persistence and game adoption remain later stages.

## RPG R3 inventory extension (2026-10-25)

R2 is published character/resource/progression and atomic command support. R3
extends WebEngine::RPG to API 1.2.0 with immutable ItemDefinition and
InventoryDefinition records, typed durable ItemId, and CreateInventory, AddItem,
RemoveItem, SplitItem, MergeItems, TransferItem, EquipItem and UnequipItem.
Use the engine-root docs/rpg-inventory.md for exact quantity, capacity, identity
and modifier ordering rules and docs/rpg-r3-validation.md for platform evidence.
R3 uses the same Runtime::command/prepare/commit; do not add a second transaction.
Equipment contributions are derived in CharacterState::equipmentModifiers and
cannot be edited through ordinary modifier commands. Capacity counts unequipped
stacks; equipment replacement stages displacement and modifiers atomically.
Weight/economy/loot/crafting, abilities, RNG, combat, saves and rendered RPG hosts
are outside R3. Native/Wasm kernel tests do not establish rendered acceptance.
This extension requires the R3 engine revision; RPGGame's pinned 56080eca engine
does not contain it. Do not advance a consumer pin as part of a skill refresh.

## Simulator service integration (local stage 4)

Local Simulator now adds customers, FIFO order acceptance, item/recipe lines,
linked jobs, patience and delivery receipts. See docs/simulator-service.md and
its validation record; this is not a CafeGame dependency upgrade or a release.
ArriveCustomer starts patience; acceptOrder creates bounded jobs atomically for the
queue head. Completed outputs remain claimed by their order until delivery or
abandonment. Deliver must precede expiry and creates one retained receipt plus one
Payment event. Keep menu pricing, satisfaction, refund policy and wallet in the game.

Use pendingReceipts and game-owned receipt-ID deduplication before acknowledgeReceipt.
No save/crash consistency is promised until the game and simulator have a shared
save envelope. ForgetCustomer retires terminal jobs/order/acknowledged receipt
atomically; never forget an individual order-owned job. Cancellation/failure cascades
release other job holds and completed output claims without destroying stock.
Abandonment explicitly cancels running work or finishes it for ordinary stock.

Advance merges completion and expiry deadlines; completion at the expiry instant
is not delivery. A successful blocked result reports committed consumed/remaining
time; an outbox LimitReached error publishes no candidate/time progress, so drain
and retry the original delta. Configure the outbox for the largest atomic cascade.
Do not interleave later host commands while pending simulation time remains.

## RPG R4 abilities and status extension (2026-10-25)

API 1.3.0 extends the existing command transaction with UseAbility, ApplyStatus,
DispelStatus and AdvancePhase. Immutable AbilityDefinition and StatusDefinition
records use typed AbilityHandler validation/evaluation in the existing Bindings.
All costs, item use, effects, statuses and RNG changes prepare privately and publish
once. Providers borrow candidate views only during the call and retain no references.
Runtime accepts an explicit uint64 seed (default zero). PCG32 draws and rejection
sampling consume the shared candidate budget. RNG1 encoding is only the detached
random state; it does not restore or save a complete game.
Statuses advance only under explicit OwnerStart/OwnerEnd/RoundStart/RoundEnd
commands with increasing epochs. The first matching hook may count or be skipped
according to the definition; no render-time/automatic turn progression exists.
Read engine-root docs/rpg-abilities.md for exact ordering, inclusion, budgets,
schema and ownership rules and docs/rpg-r4-validation.md for platform evidence.
Encounter authority/initiative/turn budgets remain R5; full saves remain R8 and
rendered hosts R9. Check the actual consumer pin: RPGGame still pins 56080eca.

## Simulator persistence (stage 5)

Runtime::save/restore implement bounded canonical version-1 snapshots. Restore validates privately, creates a fresh epoch, and emits no historical events. Rebind stable IDs via epoch(), discard old host commands and rebuild travel requests. Persist wallet/applied receipt bookkeeping alongside core bytes; receiptSequence() exposes the high-water mark. Only version 1 and exact definition catalogs are accepted. Read docs/simulator-persistence.md for limits and evidence. CafeGame bundles the unreleased portable module; its renderer pin is separate.

## Simulator release integration (stage 6)

Read engine-root docs/simulator-guide.md for the full owner-thread loop, backpressure, receipt deduplication and restore rebinding. examples/simulator/parity.cpp exercises two-customer contention, interruption and saved state; scripts/simulator-parity.py compares exact snapshots/events across native/Wasm CI. This is unreleased v0.1 source; check docs/simulator-release.md for evidence and open native/hardware gates. No renderer pin changes or published package are implied.

## RPG R5 encounter extension (2026-10-25)

API 1.4.0 adds BeginEncounter, EndTurn, FinishEncounter, ExitEncounter and
SetCooldown to the same prepare/commit boundary. Register a typed EncounterHandler
in Bindings for eligibility, initiative, allowances and automatic/finish/exit decisions.
Catalogues retain provider ownership. Roster cap is 64; initiative builds once each
round after RoundStart, descending score with ascending EntityId ties. Turn and
encounter epochs never reuse values. AP/movement refresh after OwnerStart once;
R6 will add tactical spending, spatial quotes, reactions and presentation gates.
Committed state exposes roster/order/cursor/active/epochs/allowances/outcome without
scene references. Encounter-generated phases scope to its roster; direct AdvancePhase
rejects until exit. Cooldowns tick before each character's statuses. No eligible
actor finishes as Stalemate unless policy already chose another outcome. Bounded
encounter work and status effects reject cyclic/failed phase work atomically,
including the initiating ability's costs/RNG. No clock or rendering advances turns.
Read engine-root docs/rpg-encounters.md for exact ordering and low-level-operation
trust boundaries; docs/rpg-r5-validation.md records native Windows/Linux and Wasm
commands/results. No rendered-host acceptance is claimed (R9); saves remain R8.
Check the actual consumer pin before use: RPGGame still pins 56080eca.

## RPG R6 tactical extension (2026-10-25)

API 1.5.0 adds standalone TacticalAction (ability/item or move), immutable
Runtime::spatialQuote query copies, checked turn/resource costs, bounded reaction
frontiers and pending presentation. SetTacticalPosition initializes quantized
positions outside encounters; combat moves publish destinations once. Raw
UseAbility now rejects inside encounters and remains exploration-only. Register
EncounterHandler::authorize and optional paired reactions/react callbacks.
Quotes bind runtime owner/generation/revision, actor, encounter/turn and world
identity/revision. Supply the freshly observed WorldStamp to prepare (third arg)
and canCommit/commit (second arg); omission/mismatch rejects a spatial candidate.
Commit compares stamps without callbacks. Host must reserve route/destination,
freeze conflicting writers and prevent interleaving between final observation and
publication. This is trusted host input, not a navigation or locking implementation.
Reactions use before/after phases, round initiative and stable source/token order;
each token fires once per root action, depth <=8 and tokens <=256 with shared
effect/RNG/work budgets. Any failure rolls back the complete action.
Pending presentation blocks mutation until a standalone matching
AcknowledgePresentation. Completed/Skipped/Recovered only unlock; they never
repeat effects or move actors. invalidateCommands preserves pending state; rebuild
visuals from committed state before a fresh recovery acknowledgement. R9 supplies
actual scene/browser/desktop adapters. Read engine-root docs/rpg-tactics.md and
docs/rpg-r6-validation.md for exact semantics, migration and platform evidence.
R7 quest coordination and R8 full snapshots remain future work. Check consumer
pins: RPGGame still uses 56080eca and has not adopted this API.

## RPG R7 quest bridge extension (2026-10-25)

API 1.6.0 adds optional engine_rpg_quests / WebEngine::RPGQuests above RPG and
Quests. Enable WEBENGINE_BUILD_RPG_QUESTS with both dependencies; base RPG stays
independent of Quests. quest_bridge::Coordinator owns private runtimes and retains
the distinct typed catalogues/registries. Policy callbacks project facts and map
quest effects to AddExperience/AddItem/AdjustResource/ApplyStatus proposals. Explicit
event projection posts at most one stamped event atomically using existing quest
source sequences. Provider callbacks are pure and borrow views only during calls.
The same RPG candidate includes RecordAward entries keyed by stable game-selected
source plus the original quest-id/effect-key receipt. Ledger cap is 16384; duplicate
receipts reject. Replay records ReplaySuppressed without invoking reward mapping;
later completion cannot turn suppression into a live grant. No ledger eviction.
Coordinator prepare stages quest progress/queue consumption and all rewards;
commit preflights both kernels then publishes without callbacks, allocation or
interleaving. Read docs/rpg-quests.md for the exact predicate-independence proof.
Failed rewards preserve both states and pending input. No independent restore is
exposed. applyRpg handles nonspatial operations only; use prepareRpg/commitRpg with
a freshly observed WorldStamp for tactical actions. Pending presentation blocks
joint quest processing until recovery/acknowledgement. Raw mutable kernels are not
exposed. See docs/rpg-r7-validation.md for Windows/Linux/Wasm evidence and the
base-RPG/bridge-off dependency check. R8 must save/validate both kernels and award
ledger together; rendered hosts remain R9. RPGGame still pins 56080eca.

## RPG R8 snapshots and safe restore (2026-10-25)

API 1.7.0 adds engine/rpg/snapshot.hpp to WebEngine::RPG. encode/decodeCatalog
uses normalized definitions and an exact typed handler version/schema manifest;
decode retains supplied immutable bindings and uses existing definition validators.
Wire-v1 envelopes have magic/version/tag/length/FNV-1a checksum, explicit LE64
integers, bounded strings/collections and a 16 MiB total cap. Snapshot carries
session, durable state and exact canonical catalogue descriptor; content changes
at the same revision still reject. Providers version executable policy changes.
Runtime::save and restore(bytes or Snapshot) require exploration or awaiting-command
save data; presentation-pending saves reject and finished encounters must exit.
Restore validates all ownership, derived values, progression, status/turn references,
RNG version, counters, watermarks and award receipts before swapping detached state.
No gameplay evaluators or effects run. Restore rebases generation/revision and
invalidates old commands/transactions/spatial quotes; exhausted ID cursors stay zero.
Raw pointers, handles, callbacks, queued commands and presentation are excluded.
Coordinator::save/restore joins both kernels and validates exact quest receipt/award
agreement, including ReplaySuppressed records. Two new runtimes are fully restored
before one owned aggregate swap; failures retain both live states and queued input.
Successful restore clears quest input and replaces owner identities. The normal
R7 preflight/publish contract is unchanged. examples/rpg/save_game.hpp demonstrates
a sample checkpoint plus both kernels behind one enclosing bundle swap.
Games own migrations, storage, backups, namespace selection and host command clearing;
rebuild visuals from committed positions after successful load. No scene archive
version or consumer pin changes. Full golden bytes and malformed/reload tests run
on Windows, Linux and Wasm; see docs/rpg-r8-validation.md. Rendered storage/input
adapters and GPU acceptance remain R9. RPGGame still pins 56080eca.

## RPG R9 shared rendered sample (2026-10-25)

Root WEBENGINE_BUILD_RPG_DEMO=ON requires RPG, Quests and RPGQuests enabled.
It builds rpg_host_tests plus rpg_demo on Emscripten or rpg_demo_windows with
the Windows Desktop target. Standalone examples/rpg remains renderer-free and
rejects the demo flag. API 1.7.0 and all RPG defaults remain unchanged.
examples/rpg/host_controller.hpp owns the neutral two-actor rules/session,
combined SavedGame, bounded stamped input queue and single-writer reservation.
host_scene.hpp maps durable entities to scene-scoped handles and uses existing
OrbitCameraBehaviour/CameraController. Application owns ticks; camera/pause and
animation acknowledgement never advance turns. WaypointGraph supplies bounded
routes with authored clearance and occupancy checks. Reserve before final world
validation; no physics/motor/AI may write these presentation actors. Rebuild from
committed positions before acknowledging interrupted presentation. Travel/load
discard stale input and rebind views before activation. Unbind GUI/events before
destroying the controller; do not retain handles across replacement scenes.
Browser uses localStorage webengine.rpg.v1; Windows uses atomic DesktopFiles saves.
Failed writes retain the old save; detached combined load retains award receipts.
The sample offers primitive 3D, third-person inspection, equipment, turn actions,
parcel reward and save/load. It has no AI or game lore and does not change RPGGame's
56080eca pin. Build details: docs/rpg-hosts.md. Evidence: docs/rpg-r9-validation.md.
Headless controller tests, software browser pixels/input, native D3D12 captures
and physical-device acceptance are separate evidence categories. R10 remains.


Clothing milestone two adds optional physical garments through ClothDefinition, ClothSimulation and ClothingInstance::advanceCloth/resetCloth. Read engine-root docs/cloth-dynamics.md and docs/cloth-dynamics-validation.md. Advance once after simulation with simulation delta, animated world-space targets and capsule proxies; paused presentation never steps the solver. Require rigid unit-scale driver transforms. Fixed 120 Hz stepping is bounded; reset after teleport/seek/load. Existing dynamic vertex streams render the result without double skinning. Discrete particle/capsule collision does not provide self-collision, tearing, automatic fitting or rigid-body feedback. Outfit saves retain IDs/colors, not transient particles.

## RPG R10 release handoff (2026-10-25)

R10 adds the complete standalone rpg_rulesets consumer: scholar/hand/blade/point
rules versus operator/socket/probe/scaled charge, deterministic effects, reversed
initiative and distinct AP/movement policy. Both use only WebEngine::RPG. Tests
cover tactical action, equipment, save/restore, RNG policy and atomic rejection.
Kernel API 1.7.0, snapshot/catalogue/coordinator wire 1 and PCG32 version 1 are
unchanged. docs/rpg-release-1.md audits supported APIs, nested limits, ownership,
copyable CMake/registration, migration/version policy and release exclusions.
Local browser WebGPU/WebGL2 and Windows D3D12 evidence must be distinguished from
hosted rendered CI failures. Release is conditional, not an unconditional GPU
platform acceptance claim; inspect docs/rpg-r10-validation.md before adopting.
GameSession should own quest_bridge::Coordinator and immutable catalogues plus
staged campaign state, without exposing mutable child kernels. The game keeps
Lattice/content/balance, third-person presentation, host input, outer save policy
and provider authoring. See docs/rpg-game-adoption.md. No consumer pin is advanced;
RPGGame stays at 56080eca until a separately authorized and validated adoption.
Older R1-R9 sections describe historical milestones; R10 is the current handoff.

Simulator movement: engine/simulator/travel.hpp supplies TravelBinding/TravelTicket. begin/reconcile/complete guard runtime epochs, job assignment and host-request generations. Hosts own actor/interaction bindings and navigation; clear cancels only the binding. See docs/simulator-movement.md.


Clothing milestone three adds optional particle self-contact (selfCollisionDistance, zero disables) and borrowed ClothEnvironment world-space planes/boxes as the trailing advanceCloth/ClothSimulation::advance argument. Read engine-root docs/cloth-collision.md and docs/cloth-collision-validation.md. Preserve simulation delta, rigid transforms and atomic publication. Linked/rest-near particles are excluded; pins win. Collision is discrete and per garment, without triangle/edge or continuous guarantees, inter-garment contact or rigid-body feedback. Shared browser/Windows examples supply floor and block proxies; validate both platforms independently.

## RPG library stabilization L1

The direct D3D12 backend checks adapter enumeration failures before dereferencing
COM interfaces. WEBENGINE_D3D12_WARP=1 explicitly requests Microsoft's software
adapter for diagnostics/CI and reports adapter.software=true; it is never an
implicit fallback and must remain unchanged during the host lifetime. Unset it
for normal play and hardware acceptance. The native smoke verifies the selection.
Browser software acceptance keeps a bounded test deadline and pixel/input/save
checks, with original GPU error/loss diagnostics. Software and physical hardware
evidence are separate; consult docs/rpg-l1-validation.md for actual gate results.
API 1.7.0 / wire 1 and consumer pins are unchanged. RPGGame adoption remains a
separate task; do not confuse engine sample acceptance with game acceptance.


Clothing milestone four adds optional surfaceTriangles topology, barycentric vertex/triangle self-contact, borrowed ClothEnvironment::triangles and sweptCollision. Read engine-root docs/cloth-surfaces.md and docs/cloth-surfaces-validation.md. Supply real garment faces and world-space collider snapshots; keep simulation-time ownership, rigid transforms and atomic stream publication. Bounded linear sweeps cover self surfaces and static-snapshot external triangles/capsules/boxes. Edge/edge, inter-garment and moving-proxy CCD are not guaranteed. Surface meshes stay small; reset/equip and outfit persistence retain M2 contracts. Shared browser/Windows examples use actual block triangles; validate native and browser independently.

Simulator occupancy: include engine/simulator/occupancy.hpp for OccupancyPool<Owner>. Compact queues advance FIFO positions; stable pools retain seat/equipment ownership and promote oldest waiters. Arrival checks owner, point and token. Hosts release on departure and rebase validated owners before restoring typed claims. See docs/simulator-occupancy.md; core save schema is unchanged.


Clothing milestone five adds optional ClothDefinition::edgeSelfCollision, deriving at most 2048 unique edges from actual surfaceTriangles. Read engine-root docs/cloth-edges.md and docs/cloth-edges-validation.md. Closest finite-edge parameters distribute response across four endpoints with pinned weights preserved. Existing sweptCollision enables bounded linear edge sweeps; local neighbor pairs are excluded. Preserve simulation delta, rigid transforms, candidate publication and transient persistence. edgeContacts counts projections and freezes during pause. Worst-case edge cost is quadratic; this is not a universal mesh CCD or inter-garment guarantee. Shared browser/Windows demos enable edge contact; validate both platforms independently.

Simulator workflows: include engine/simulator/workflow.hpp for TaskBoard. Dependencies refer only to existing earlier tasks; role/shift matching uses priority then FIFO IDs, without automatic preemption. Guard starts/completions with assignment tokens; available(false) interrupts and preserves remaining work. Restore validates typed snapshots and refreshes active tokens. Read docs/simulator-workflows.md for ownership, bounds and CafeGame schema-3 integration.

L1 renderer follow-up: Direct D3D12 preserves the existing instance-buffer
binding and per-view/fence lifetime; the sample shares meshes and asserts actual
multi-instance draws. Pipeline descriptions initialize valid disabled-state
enums. Non-layered unlit materials use a dedicated pixel shader preserving alpha,
reflection clipping, emission and fog; browser pipelines already specialize
materials. WEBENGINE_D3D12_GPU_VALIDATION=1 opts into GPU-based binding validation
alongside WEBENGINE_D3D12_VALIDATION=1. Leave diagnostics unset for normal play.
Linux CI runs full Chromium WebGPU under Xvfb (RPG_HEADED=1), with SwiftShader ANGLE
and VulkanFromANGLE; WebGL2 is also checked headlessly. Headless WebGPU's black
canvas is not accepted as a rendering pass. Pixel/input/save/load assertions and
the normal GPU watchdog remain enabled. GPU errors and unexpected loss fail the
browser acceptance check. API 1.7.0 / wire 1 and RPGGame's pin remain unchanged.
L1 is validated at c7db4b446afe4b7ae92b3122c7b3b5c89f7365e3: all RPG library,
SDK and rendered-host gates pass, with separate Intel hardware acceptance.
The general engine vehicle/spotlight failures remain outside this RPG scope.
Consult docs/rpg-l1-validation.md for reproduction commands and coverage limits.

Simulator supply stage 10: Runtime::transfer atomically moves bundles without consuming input holds or incoming capacity. Include engine/simulator/supply.hpp for SupplyBoard, explicit simulation-time delivery polling, output-capacity-backed supplier orders, cancellation/retirement, typed restore and threshold/target reordering. Runtime must outlive the board; do not externally finish its reservations. Restore core first, rebase typed IDs, then restore into a fresh board. See docs/simulator-supply.md. CafeGame envelope schema 4 persists storeroom, automation and pending deliveries; core wire format remains 1. Supplier costs, vehicles and hauling are game policy/future work.

Simulator economy stage 11: include engine/simulator/economy.hpp for optional Ledger. Signed integer postings require contiguous per-stream source sequences; compacted history retains totals and duplicate watermarks. Prepare all allocations before a related inventory/service mutation, preserve exclusive ledger ownership, then use allocation-free commit. Quotes reject stale revisions, foreign/consumed candidates and lifetime reuse. Hosts own prices, currencies, wages and joint persistence. See docs/simulator-economy.md. CafeGame schema 5 adds paid supplies/refunds, on-shift wages, arrears and finances; core wire schema remains 1.

Simulator quality stage 12: include engine/simulator/quality.hpp for Freshness (explicit-time integer shelf-life samples) and Condition (monotonic use IDs, saturating wear/dirt and independent clean/repair restoration). Runtime::remove atomically disposes unreserved bundles. The host owns expiry effects, equipment thresholds, task/ledger coordination and joint persistence. See docs/simulator-quality.md. CafeGame schema 6 adds coffee expiry/waste and staffed machine cleaning/paid repairs; core wire schema remains 1.


For simulator placement and layout, read engine-root docs/simulator-layout.md. GridLayout validates bounded footprints/access/reachability; hosts own navigation and prices. OccupancyPool::setEnabled disables unclaimed stable points. CafeGame uses envelope 7 with closed/drained editing and joint layout/ledger/claim validation.


For simulator demand and satisfaction, read engine-root docs/simulator-demand.md. demand.hpp supplies explicit seeded DemandRandom, ArrivalTimer and validated pure scores. Hosts own profiles, arrival boundaries, candidate RNG publication and exactly-once review/receipt coordination. CafeGame envelope 8 validates guest clocks/outcomes and paid-service totals together.

## RPG library L2: parties and teams (API 1.8.0)

WebEngine::RPG now owns State::parties, SetParty/RemoveParty commands and
BeginEncounter::teams. PartyState has capacity, activeLimit, ordered PartyMember
entries (entity, active, ControlMode) and optional active leader. Membership is
unique across parties; limits are 64 parties and 64 members/party. Replacement
commands implement recruitment/dismissal/control/leader changes atomically.
Exit any encounter before changing parties. partyOf and activePartyMembers expose
membership. Nonempty encounter teams must cover the exact roster (at most 16
teams); CombatMember stores team ContentId and Player/AI/Scripted metadata.
teamRelation and controlledBy are queries, not authorization or AI. Games own
encounter selection, friendly fire, outcome, recruitment, formation and AI policy.
Empty teams preserve legacy unassigned behavior. Shared browser/Windows sample
and host save/load tests exercise both APIs; see docs/rpg-parties.md for contracts.
Runtime save wire v2 appends parties/teams; v1 remains readable and is still emitted
when no L2 data exists. Catalogue and combined outer envelopes stay v1. Older
libraries reject v2. Existing operation/error enum indices are preserved.
Use rpg_parties plus existing snapshot/host tests; platform evidence is recorded
in docs/rpg-l2-validation.md. Consumer pin upgrades are separate integration work.

## RPG library L3: targeting and area effects (API 1.9.0)

engine/rpg/targeting.hpp exports selectTargets(State, SpatialSnapshot, actor,
TargetQuery). Queries return sorted IDs without mutation/RNG. Shapes: SingleTarget,
SphereTargets(center,radius), ConeTargets(origin,direction,length) with 90-degree
full angle and spherical length cap, LineTargets(origin,direction,length,radius)
with cylindrical sides and flat end caps. All geometry is 3D exact integer arithmetic.
Coordinates are limited to +/-500000, range/extents to 0..1000000 and nonzero
direction components to +/-1000. Do not pass arbitrary float/meter scene positions;
the game chooses quantization. Out-of-bound inputs reject before arithmetic.
TargetQuery range measures actor to entity/area origin. TargetTeam Any/Allies/
Opponents uses encounter teams; unassigned matches only Any. includeSelf defaults
false; requireLineOfSight defaults true using host legalTargets facts. Opting out
of LOS is explicit game policy. Complete resident roster positions must match state.
Attach the query to SpatialSnapshot::targeting before issuing the existing immutable
SpatialQuote. Tactical preparation checks the complete target list against selection
before/after BeforeAction reactions; missing/extra/duplicate targets reject atomically.
AP/resource/item costs apply once per action and effects/RNG publish together through
existing transactions. Movement cannot carry targeting. World/revision/turn checks and
presentation recovery remain unchanged. Pure preview does not itself authorize input.
Rulesets must require intended shapes/filters in authorization when needed: omitting
targeting retains the legacy contract. Hosts own physics LOS, aim-point legality,
cover/falloff and damage rules; the kernel does not raycast. Queries are transient and
not saved; L2 v1/v2 save compatibility is unchanged. Both rendered hosts use the shared
opponent-area sample; dedicated rpg_targeting tests cover multi-target atomic effects.
See docs/rpg-l3-validation.md for actual Windows/browser/portable evidence and limits.


Simulator authoring stage 15: include engine/simulator/diagnostics.hpp for detached
validateDefinitions(resources, processes, limits) with field paths and read-only
inspectJob(Runtime, JobId) state/blocked-error explanations. These reuse core rules,
never advance/assign/drain events and own no host references, IO or scenes. Game
hosts own typed definitions/scenarios, action availability and balancing policy.
CafeGame has four atomic-reset presets, browser inspection/JSON export and the
native/Wasm cafe_balance reference-v1-zero-travel-100ms runner. Core schema 1 and
Cafe envelope 8 remain unchanged; hot reload, content migrations and scale are
not supplied. See docs/simulator-authoring.md and preserve native/browser parity.

Clothing milestones six through eight add optional ClothDefinition::inverseMass,
aerodynamicDrag, and ClothEnvironment wind/capsuleTranslations/triangleTranslations.
Read docs/cloth-materials-motion.md and docs/cloth-materials-motion-validation.md.
Preserve pin authority, mass-independent gravity, atomic publication and simulation
time. Hosts own stable collider ordering, pending translations and reset/equip
history; only translating capsules/triangles are supported. The shared browser and
Windows demo uses weighted fabric, aerodynamic airflow and a moving block. Check
platform evidence independently; VS2022 uses direct D3D12 because the pinned Dawn
binary needs a newer compatible runtime. See the bundled cloth-materials-motion
reference for parameter limits and validation commands.

## RPG L4: damage, defeat and revival (API 1.10.0)

ConfigureVitality opts a character into health tracking over an existing resource,
once and outside encounters. onZero chooses Downed or Dead. Ability/reaction
proposals support DamageEffect, HealEffect, ReviveEffect and DefeatEffect;
ApplyVitalEffect is an administrative operation, not player authorization.
Magnitudes are final nonnegative unit/scale-checked amounts. Games own formulas,
resistances, hit rolls, revival eligibility/costs, outcomes and checkpoints.
Damage clamps at zero; healing clamps at maximum and requires Alive. Revival
requires positive health and Downed; Dead additionally requires allowDead=true.
Explicit defeat cannot turn Dead into Downed. Costs/RNG/effects publish atomically.
Results.vitality records requested/applied values and life-state transitions.
canAct gates turns, reactions and abilities in addition to game eligibility.
teamDefeated checks nonempty assigned teams; rulesets choose Victory/Defeat.
Revival does not restart a finished encounter. Raw health depletion reconciles
life state; raw health increases cannot resurrect. Health periodic effects clamp
while Alive and skip incapacitated HP while still advancing status duration.
Runtime snapshot v3 persists profiles; untracked saves still emit compatible
v1/v2. Old saves do not invent profiles. Catalogue/combined envelopes unchanged.
Both rendered sample hosts use typed health effects and vitality profiles.
See docs/rpg-vitality.md and docs/rpg-l4-validation.md for contracts/evidence.
Implementation currently remains local pending publication approval; do not
assume consumer submodules contain L4. No fresh GPU acceptance is established.


Simulator compatibility stage 16: read engine-root docs/simulator-compatibility.md.
Include engine/simulator/compatibility.hpp for ContentHash/ContentStamp/checkContent
and checkpointHash; fingerprints are not authentication. Runtime::metrics is an
O(1) read-only retained-record/limit query. Clock-only advance and idle assignment
avoid state copies; due transitions retain transactional budgets and ordering.
Hosts own known-content migration policy, checksums, metrics and stress reports.
CafeGame envelope 9 fingerprints catalog/profile values and declared ruleRevision;
bump that revision for saved-policy changes, never rewrite its frozen legacy stamp.
Known schemas 1..8 migrate only under the historical default content. Use the same
native/Wasm stress host for checkpoint replay; exclude wall timings from parity.
Core schema remains 1; this supersedes stage-15 future-migration guidance. GPU/mobile
acceptance and arbitrary content migrations are not implied by portable tests.

## RPG L5: bounded combat AI (API 1.11.0)

Include engine/rpg/combat_ai.hpp. Host code supplies up to 64 uniquely named
CombatOption values containing ordinary stamped TacticalActions. Runtime and
RPGQuests Coordinator expose planCombat(producer, sequence, actor, options,
scorer, budget, world, stopToken). CombatAiBudget defaults to 32 evaluations,
caps at 64 and carries existing per-action budgets. Evaluation uses full normal
transaction/reaction validation without publishing live state, RNG or watermarks.
The pure CombatScorer sees borrowed before/after states, results and the option;
return signed utility or nullopt to decline. Highest score wins, stable ContentId
ties. Options are sorted before bounded-prefix evaluation; truncated is explicit.
Evaluations report rejected ErrorCodes and accepted optional scores. No accepted
option previews normal EndTurn; invalid fallback returns NoAction and its error.
prepareCombat(plan, freshWorld) revalidates the private command. Publish with
normal commit/commitRpg and a fresh world observation after route reservation.
State/revision/turn/world changes invalidate decisions; even fallback requires
world-stamped publication. Stop tokens cancel between evaluations and after
callbacks; cancellation after preparation makes commit fail. Owner-thread only;
callbacks must be pure, bounded and cannot retain references. Cancellation cannot
interrupt a running callback. No worker, scheduler or hidden mutation route.
Preview exposes concrete simulated rolls; games choose what information scoring
may use. Game code owns enumeration, navigation, melee/ranged tactics and enemy
content. Both rendered samples expose explicit AI step for opponent strike or
end-turn fallback. API 1.11.0 retains L4 v1/v2/v3 saves; plans are transient.
See docs/rpg-l5-validation.md for current checks and unverified rendering limits.
Implementation remains local pending publication approval; game pins unchanged.

## RPG L6: loot and atomic rewards (API 1.12.0)

Runtime Operation accepts RollLoot {table, entries, rolls=1} and GrantReward
{id, grants, loot}. RewardGrant supports AddExperience, AddItem, AdjustResource
and ApplyStatus. LootEntry {id, weight, grants} uses positive uint32 weights;
empty grants explicitly mean no drop. Ordered weighted sampling is with replacement,
using the existing candidate PCG32 and shared randomDraws budget. Maximum 64 entries,
32 rolls, 256 authored grants per table; bundle maximum 16 tables. Wrappers and all
expanded grants count toward the existing 256-operation budget; no nested tables.
Results::loot reports table/entry/roll and counts toward the 512-result cap.
Full inventory rejects the whole command including XP, RNG, IDs and receipt. Use
fresh commands with the same stable AwardId after freeing space. AddItem still
creates one stack without implicit merge, overflow splitting or item loss.
Duplicate Granted or ReplaySuppressed receipts reject before drawing. Existing
snapshot codecs persist RNG and ledger with no L6 wire change. Pre-grant saves
restore pre-grant entitlements; failed claims do not reserve rolls. Games own
content versions, eligibility, receipt allocation and pending-claim save policy.
Quest RewardOperation accepts RollLoot inside coordinated quest publication.
Coordinator prepareRpg/applyRpg accept GrantReward only outside its reserved quest
award source; direct RecordAward remains forbidden. Quest and encounter namespaces
coexist in combined saves. Windows/browser shared samples expose Collect loot
and parcel quest loot. See docs/rpg-rewards.md and docs/rpg-l6-validation.md.
Implementation remains local pending publication approval; consumer pins unchanged.

## RPG L7: equipment requirements and progression choices (API 1.13.0)

Include engine/rpg/builds.hpp. CatalogDefinition equipmentRules attach typed
BuildRequirements (minimum level, minimum base attributes, required unlocks) and
up to 31 additional requiredSlots to item definitions. EquipItem retains one
anchor; occupiedEquipmentSlots returns its union with required slots. All must
exist in the inventory. Overlapping equipment is displaced completely, capacity
checked and modifiers recomputed once. Temporary/equipment bonuses cannot meet
base-attribute prerequisites. Failure publishes no displacement or resource change.
CatalogDefinition choiceRules attach ProgressionChoices to a progression: 1..256
points per level and up to 256 UnlockChoices with positive cost, prerequisites and
optional exclusiveGroup. IDs must be in allowedUnlocks; cyclic/unknown prerequisites
reject. SetProgressionChoices replaces the complete selected set outside encounters.
All prerequisites, exclusivity and the level-derived point budget are checked.
Automatic advancement cannot grant selectable IDs. Selected IDs are visible through
CharacterState::unlocks and tracked separately in State::choices; automatic unlocks
survive respec. remainingChoicePoints and Results::choices expose remaining budget.
Empty selections respec; explicitly unequip incompatible gear BEFORE changing the
set in the same command. Every intermediate operation preserves build validity.
Full bags reject the whole command. Game code owns respec fees, unlock effects,
backgrounds, specializations and migration. Choices do not grant stat modifiers by
 themselves. No mutable/refundable point counter or scheduler is introduced.
Catalogue wire v2 stores optional build rules; runtime v4 stores nonempty selected
sets. Old formats remain readable and are emitted when extensions are absent.
Exact catalogue compatibility and restored prerequisites/occupancy/point ownership
are validated. Coordinator prepareRpg/applyRpg and combined saves support the same
operations. Both hosts expose mastery, greatblade and respec with point feedback.
See docs/rpg-builds.md and docs/rpg-l7-validation.md for limits and actual evidence.
Implementation remains local pending publication approval; consumer pins unchanged.

## RPG L8: economy transactions and diagnostic replay (API 1.14.0)

TransferResource moves a positive exact Amount between distinct characters;
insufficient funds, mismatched units/scale and recipient headroom reject.
Economy operations require exiting combat before use.
TradeItems consumes an explicit unequipped stock quantity, optionally transfers
payment and creates a fresh destination stack. Partial stock retains its ID;
fully consumed stock retires. No auto merge, infinite stock or implicit pricing.
CraftItems consumes 1..32 unique input stacks, then up to 16 payments and creates
1..32 output stacks. Full consumption frees slots before products. Everything
uses ordinary candidate transactions and the shared expanded-operation budget.
Trade/craft use stable game-issued AwardIds: retry with the same ID; new intended
orders get new IDs. The persisted ledger prevents duplicate outputs across load.
RPGQuests reserves its quest receipt namespace for all receipt-bearing wrappers.
Recipes, currency, prices, permissions, stations and output policy stay game-owned.
Existing snapshots store all consequences without new wire fields.
engine/rpg/replay.hpp provides replay over an exploration snapshot and up to 64
ReplaySteps. It reports committed prefix count, first failed step/code/message
and prefix snapshot. No IO, recording, scheduler or command serialization. Combat
replay needs host regeneration of world facts/quotes and is outside this helper.
The shared Windows/browser sample exposes Buy tonic, Sell tonic and Craft shield.
See docs/rpg-economy.md and docs/rpg-l8-validation.md. Game adoption remains G12;
publication and consumer pins are separate. Preserve unrelated skill guidance.


## Optional foliage integration (2026-10-28)

The optional WebEngine::Foliage target adds immutable species catalogues,
deterministic source-sampled placement, bounded visual/independent trunk-collision
streaming and stable removal saves. Read the bundled foliage.md reference and
engine-root docs/foliage.md plus docs/foliage-validation.md. Call update at an idle
pre-simulation boundary with separate focus positions; present takes application
simulation seconds for distance detail and GPU wind through generic bulk instances.
M6 uses shared immutable MeshBend resources and a 32-byte MeshWindState; wind-only
ticks preserve placement/object uploads. WebGPU/Dawn, WebGL2 and direct D3D12 have
wind color/shadow paths; CpuMeshes retains the CPU reference. Bulk picking requires
component plus local instanceKey, mapped to the durable FoliageId. Scene owns
transient projection; explicitly clear if the scene will
outlive its stream. Games own biome/harvest/storage/content policy and collision
readiness gating. Read docs/foliage-dense-validation.md for actual M6 checks and
limits; this does not add GPU culling/indirect draws. Check the consumer pin.
