# AI state machines and graphical authoring

Link `engine_ai` and include `engine/state_machine.hpp`. Immutable
`StateMachineDefinition` assets describe states, named behavior bindings and
ordered conditional transitions. Each agent owns a `StateMachine` instance and
supplies its game context through `StateMachineHost`; neither definitions nor
the runtime own scenes, actors, renderers, browser input or callback registries.

## Runtime integration

```cpp
engine::StateMachineData data;
data.initial = 1;
data.states = {{1, "Idle", "idle", {80, 80}},
               {2, "Follow", "follow", {380, 80}}};
data.transitions = {{1, 1, 2, "target-visible", 0},
                    {2, 2, 1, "target-lost", .5}};
auto definition = engine::StateMachineDefinition::create(std::move(data));
engine::StateMachine machine(definition);
```

Implement `StateMachineHost::enter`, `update`, `exit` and `condition` in the
application. Bind `AiState::behavior` to game actions and condition keys to
perception, memory or navigation outcomes. Keys are names, never executable code.
Use generation-checked `ActorId`/`ComponentId` references for game targets; resolve
them through their owning scene. Definitions contain no target handles.

An application actor can inherit both `Actor` and `StateMachineHost`:

```cpp
void fixedTick(double dt) override {
    updatePerception(dt);              // Game code; DecisionClock can bound queries.
    machine.step(*this, dt);           // This actor implements StateMachineHost.
    navigator.step(motor(), dt);       // Before child CharacterMotorComponent ticks.
}
void enter(const engine::AiState& state) override {
    if (state.behavior == "follow")
        navigator.navigateSource(navigationSource, lastSeenPosition);
}
void exit(const engine::AiState&) noexcept override {
    if (auto* motor = getComponent<engine::CharacterMotorComponent>())
        navigator.cancel(*motor);
}
bool condition(std::string_view key) override {
    return key == "target-visible" && targetVisible;
}
```

Call `setTickEnabled(true)` on the actor. Stop the machine with its host on
disable/detach/despawn, target/control handoff or before destroying the game
context. Guard component lookups during deferred destruction: an already
pending-removal motor may no longer resolve. Cancel persistent movement explicitly
where needed. One producer owns motor commands; do not reissue navigation every
frame merely because a state remains active.

### Execution and lifetime contract for polled flat graphs

- Instances start Idle. First `step(host, dt)` enters the initial state, advances
  its timer and calls update once. Eligible transitions are evaluated afterward.
- `minimumSeconds` measures simulation time in the current state. Transitions
  are checked in stored vector order; the first true condition wins. A transition
  runs old exit, resets the timer, then new enter. The destination's update runs
  on the next step, not once for every transition in the same step.
- Defaults are one transition and 1024 condition evaluations per step. Transition
  limits are 1–64; condition limits are 1–65536. Cycles/self-transitions are valid
  but defer at the transition bound. Exhausting the condition allowance fails with
  cleanup and throws. These are work limits, not a frame-time guarantee.
- Accept finite `dt` in `(0, .25]`. Pause by not stepping. Authoring and graph
  presentation never advance a game instance. Agents own their independent timers.
- `stop(host)` exits at most once and remains Stopped on later steps.
  `restart(host, definition, initialId)` validates the replacement before exiting
  the old state, resets the instance and enters the requested state next step.
- Enter/update/condition failures exit the active (possibly partially entered)
  state once, mark Failed and rethrow. Later steps stay inert until explicit
  restart. Exit must be `noexcept`. Callback reentry is rejected; do not call
  step/stop/restart recursively from callbacks, including exit.
- Hosts are synchronous, non-retained references. Keep the same owning game
  context while running and explicitly stop before it disappears. The runtime
  introduces no worker, subscription, scheduler or implicit scene phase.

Definitions validate before publication: 1–256 states, up to 1024 transitions,
unique state/transition IDs, existing entry and endpoints, finite positions within
±10000, finite delays 0–86400 seconds and 1–256 byte labels/bindings without control
characters (bindings also exclude spaces). Unreachable states and cycles are
allowed authoring choices. Bindings are validated by the consuming application;
the generic definition cannot infer which game callbacks exist.

## Graphical engine editor

Open `?editor=1`, then **Details → AI state machines → Open AI graph editor**.
The graph workspace is an integrated modal tool, with its own asset document and
history. It uses the same C++ definition validation and runtime as game code.

- Add/select states, edit names and behavior bindings, and choose the entry state.
- Drag nodes to arrange them. A completed drag is one undo command; Escape,
  pointer cancellation, blur and page hiding cancel unfinished gestures. Arrow
  keys move a selected state; the selection dropdown also supports keyboard use.
- Select a source state, click **Connect states**, then click its destination.
  Select the connection or its dropdown entry to edit the condition and minimum
  state time, change priority or delete it. Deleting a state removes its incident
  connections and selects another entry if needed; the last state cannot be deleted.
- **Undo/Redo** affects the AI document only. Template replacement is undoable.
  **Frame graph** and zoom change presentation without editing the asset/history.
- **Save .weai** exports the definition; **Open .weai** validates a complete file
  before replacing the document and starting clean history. Invalid fields/files
  retain the last valid graph. Closing/reopening the tool retains its in-memory
  document. Save the AI file separately to retain it across page reloads.

### Isolated runtime preview

**Start preview** creates a separate `StateMachine` over the current immutable
definition. Named-condition checkboxes stand in for game observations. **Step
0.1 s** advances once; **Run/Pause** advances the preview's own bounded clock.
The active node, elapsed state time and enter/exit counts are shown. Stop before
authoring; definition-changing controls are disabled during preview, and portable
bridge commands enforce that gate. Closing the tool, leaving the editor or
starting scene Play stops the preview. Blur/hiding pauses automatic preview time.

Preview resolves conditions supplied by the checkboxes and does not execute game
behavior bindings. It validates graph flow through the actual C++ runtime without
moving actors or changing scene clocks, bytes, selection or scene undo history.

### Running an authored graph in the demo

The patrol template is application data in `src/ai_demo.cpp`, outside reusable
libraries. The autonomous demo now uses `StateMachine` for Patrol, Follow,
Investigate, Return and Blocked behavior; manual click-to-move explicitly hands
control away from the state machine and retains navigation recovery.

**Use in AI demo** validates bindings before retaining the graph for the current
application session. Close the graph, exit the scene editor, then open the AI demo.
The supported behavior keys are `patrol`, `follow`, `investigate`, `return` and
`blocked`. Supported condition keys are `target-visible`, `target-lost`,
`target-removed`, `memory-expired`, `arrived` and `navigation-failed`.
Renamed states, graph entry, connections, priorities and minimum times come from
the authored definition. Unknown demo bindings reject without replacing valid
configuration. Other games implement their own host and supported binding set.

## Portable authoring and files

`engine_editor` additionally links `engine_ai` and exposes
`engine/editor_ai.hpp`. `AiGraphEditor` owns immutable graph snapshots, bounded
history (default 128 edits; configurable 1–1024), undo/redo and a saved identity.
`edit(data)` validates and prepares a candidate before replacing state; identical
content is a no-op. `open(bytes)` parses before replacing document/history;
`markSaved()` tracks export state independently of scene files.

`StateMachineArchive::save/load` writes version-5 `.weai` text with header `WEAI 5` (also reads versions 1–4),
stable IDs, graph/state names, bindings, layout, transition order and delays.
Loads reject unsupported versions, invalid counts, orphan identities, malformed
values, truncated/trailing input and files over 512 KiB. This format contains no
live callbacks, runtime timers, agent target handles or scene resources. Applications
can load a file and pass the resulting shared definition to their instances.

[Actor AI controllers](ai-controllers.md) now attach graph snapshots and typed
registered bindings to ordinary actors through scene history. Their optional custom
codec preserves configuration in scene files and isolated Play clones; a live
Play debugger displays selected controller state/time/history. Standalone graph
files remain separate authoring assets. Concurrent states, behavior
trees, automatic game callback discovery, visual action scripting, runtime AI
checkpoints and AI graph autosave remain extensions.

See [validation evidence](ai-state-machines-validation.md).

Typed field declarations are now part of graph assets; see [AI blackboards](ai-blackboards.md).

[Hierarchical states](ai-hierarchy.md) extend these flat-graph rules with parent
interrupts, active paths, per-ancestor timers, group authoring and complete-chain
cancellation. Flat graphs retain the ordering described above.

[Typed events](ai-events.md) add bounded per-agent occurrence queues, guarded
event edges, activation-scoped cancellation and editor/debugger injection.

[State task sequences](ai-tasks.md) now add registered per-activation work, cancellation, timeouts and live task diagnostics through actor controllers.
