# Actor AI controllers, bindings and live debugging

`AiControllerComponent` in optional `engine_ai` attaches an immutable state-machine
graph and typed binding configuration to an ordinary actor. Include
`engine/ai_controller.hpp`. Each component owns its runtime, timers and transition
history; definitions and the immutable binding registry can be shared.

## Editor workflow

1. Select an actor or one of its components in the scene hierarchy.
2. Open **Details → AI state machines → Open AI graph editor**. Import a `.weai`
   file, author a graph, or choose **Actor movement template** for an Idle/Move cycle.
3. Close the graph workspace. In **Actor AI controller**, choose **Use open AI
   graph**. Every state and condition gets a registered binding selector. Missing
   bindings are listed, and assignment is disabled until they are selected.
4. Edit schema-generated boolean, number, vector or saved-actor-reference fields.
   Movement needs a motor; **Add character motor** uses existing scene history.
   Character motors retain engine defaults, including gravity/collision. Configure
   the motor and supporting collision for the intended scene; the AI does not
   create a navigation mesh or ground automatically.
5. **Attach / update controller** validates the complete configuration before
   publishing it. Scene Undo/Redo restores assignment/configuration independently
   of graph-document Undo/Redo. **Load assigned graph** brings a component's saved
   graph into the graphical authoring document as an undoable edit.
6. Save the scene normally. Assignments embed their graph and binding values in
   the scene's optional AI component payload. The standalone `.weai` source file
   remains separately exportable; changing it does not silently change assigned
   snapshots.

During **Play**, authoring is read-only. Select the actor before Play, then open
**Live debugger during Play**. Its read-only graph highlights the active state and
latest transition and shows elapsed state time and up to 32 recent transitions.
**Restart actor AI** restarts only the selected runtime component. **Stop Play**
rebinds retained source views before disposing of the runtime. Source bytes,
transforms, graph settings, selection and undo history are retained.

The browser inspector uses the built-in registry below. Application integrations
can register their own bindings in C++ and pass that registry to components and
their persistence codecs; executable callbacks are never loaded from graph files.

## Typed registry and supplied bindings

`AiBindingRegistry::create(behaviors, conditions)` validates and publishes an
immutable registry. Behavior entries provide optional enter/update/exit callbacks,
typed parameter schemas and a `needsMotor` contract. Condition entries provide an
evaluation callback and schema. Callbacks receive the owning controller and its
validated `AiBinding`; no borrowed scene/motor pointer is retained by the registry.
Any application captures must outlive the shared registry and its instances.

Schemas support Boolean, Number, Vector and Actor. Numeric/vector values must be
finite and within declared bounds; parameter names/counts/types must match exactly.
Actor values are optional stable saved IDs, not raw pointers or serialized runtime
handles. Nonempty references must resolve to actors when configured/decoded/saved.
At runtime a missing/disabled target resolves to null, so `near` returns false.

| Built-in key | Kind | Parameters / behavior |
| --- | --- | --- |
| `idle` | Behavior | Stops an existing motor on entry. |
| `move` | Behavior | `velocity`: world-space Vector, default `(1,0,0)`, each component -100..100. Requires a motor; collision-aware velocity must have Y=0. |
| `always` | Condition | Returns true; combine with graph minimum-state time for timed transitions. |
| `flag` | Condition | Boolean `value`, default false. |
| `not-flag` | Condition | Negates Boolean `value`, default false. |
| `near` | Condition | Actor `target`, default none; Number `distance`, default 2 m, range 0..10000. Tests authoritative world positions. |

Graphs retain their own state behavior/condition labels. Controller configuration
maps each state ID and every distinct condition label to a registry key and values;
the inspector can therefore bind a graph with different game-facing names. Unknown
keys, missing/extra fields, invalid ranges or stale references reject without
replacing valid configuration or adding a history command.

Registry limits: up to 128 behaviors and 128 conditions, with up to 16 parameters
per entry. Keys are 1–256 bytes without whitespace/control characters. Existing
graph limits remain 256 states and 1024 transitions.

## Portable actor setup

```cpp
auto registry = engine::AiBindingRegistry::builtins();
auto graph = engine::StateMachineArchive::load(graphFileText);
// Defaults require graph binding names to exist in this registry. For arbitrary
// names construct settings.states/settings.conditions with explicit mappings.
auto settings = engine::AiControllerSettings::defaults(graph, *registry);
auto& actor = scene.spawn("Agent");
actor.addComponent<engine::CharacterMotorComponent>();
actor.addComponent<engine::AiControllerComponent>("AI", settings, registry);
```

Controllers are direct actor components; only one may own an actor. They run with
`fixedTickOrder() == -100`. `Component::fixedTickOrder()` defaults to zero, and
`Scene` visits sibling components in stable ascending order for fixed updates
only. Actor callbacks still precede their children; equal orders preserve existing
insertion order. Scene hierarchy order is not mutated. This guarantees commands
reach even an earlier-inserted free motor in the same fixed step. Render ticks
and physics/presentation ownership remain unchanged. Keep other command producers
from competing with an active AI controller.

`configure(settings)` validates a complete candidate, then cancels old behavior
and publishes a fresh Idle runtime. Disable/detach/owner handoff stops commands
and the machine. Re-enable does not silently resume; call `restart()` explicitly.
Stopping fixed-tick calls alone is not cancellation: call `stop()` before disabling
fixed ticking or handing the motor to another producer. Gravity/physics continue
according to motor settings. Component/target lookups respect pending removal;
no raw runtime target pointers are stored.

Runtime validation/callback failures record an error and stop that component's
commands. Exit failures are contained by the noexcept cleanup boundary and recorded.
Debug diagnostics expose status, current state, elapsed time, error and bounded
transition records. Restart clears diagnostics/history and validates references.

## Scene persistence and Play

AI stays outside `engine_core`. `aiControllerCodecs(registry)` returns an opt-in
`ComponentCodecs` registry with `engine.ai.controller`, version 1. Pass it to scene
save/load and codec-aware cloning:

```cpp
auto codecs = engine::aiControllerCodecs(registry);
auto bytes = engine::SceneArchive::save(scene, views, assets, {}, {}, codecs);
auto loaded = engine::SceneArchive::load(bytes, {}, codecs);
engine::EditorPlaySession preview;
preview.start(scene, views, assets, codecs);
```

`SceneArchive::clone` now accepts optional codecs after its limits argument.
`EditorPlaySession::start` accepts optional codecs after assets. Existing callers
retain built-in-only behavior; no core-to-AI dependency or archive-version change
is introduced. Browser scene files, autosave/recovery and editor Play supply the
AI codec explicitly. The custom factory validates the configuration, then resolves
saved actor references after the full hierarchy exists and before lifecycle hooks.

The self-contained payload is bounded to 2 MiB and embeds `.weai` definition text,
state mappings, distinct condition mappings and typed values. Unknown codec/schema,
missing callbacks, malformed data or invalid references reject before a scene is
published. Runtime timers/current state, commands, transition logs, callback code
and registry closures are not serialized. Loaded/Play controllers start fresh;
source and preview never share mutable AI state. Graph definitions inside custom
payloads are reconstructed by value; other reference-cloned immutable scene assets
retain their existing sharing contracts.

This is configuration persistence, not an AI runtime checkpoint or visual action
scripting system. Navigation bindings, game perception and additional behaviors remain application
integrations. Typed dynamic inputs are described in [AI blackboards](ai-blackboards.md). Generic actor duplication
still uses the built-in subtree codec path and rejects custom AI components rather
than dropping them. No automatic binding discovery, remote
debugger or physical-mobile acceptance is implied.

See [validation](ai-controllers-validation.md).

Graph schemas, per-agent live values, parameter sources and payload version 2
are described in [typed blackboards](ai-blackboards.md); version 1 remains readable.

[Hierarchical graphs](ai-hierarchy.md) run parent and leaf bindings with a complete
active-path debugger and one log record per accepted transition.

[Typed events](ai-events.md) add postEvent/currentEvent/eventScope adapters, bounded
queue diagnostics and explicit rejection of configuration reentry from callbacks.

## State task sequences

The registry optionally accepts a third vector of typed task factories. Controllers execute ordered `AiState::tasks` with per-activation cancellation, timeouts, scoped result events and bounded diagnostics. Graph files now write WEAI5 and read 1..5 within the unchanged AIC2 container. See [task integration and editor workflow](ai-tasks.md).
