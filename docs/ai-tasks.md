# AI task sequences

Optional `engine_ai` extends actor controllers with reusable per-state sequences.
`AiState::tasks` contains up to 16 ordered `AiTaskSpec` entries. A spec has a
registered `key`, exact typed `parameters`, optional compatible blackboard
`sources`, a finite `timeout` in `(0,86400]` seconds (default 30), and an optional
declared observation `event`. `succeededEvent` / `failedEvent` name declared
zero-parameter result events; empty names retain the terminal outcome without
posting a result. Graph publication validates structural data; controller
configuration additionally validates the registry, schemas, source ranges,
saved-Actor references and competing motor writers before replacing valid state.

```cpp
auto registry = engine::AiBindingRegistry::builtins();
auto data = definition->data();
data.events.push_back({"TaskSucceeded", {}});
data.events.push_back({"TaskFailed", {}});
auto wait = registry->taskDefaults("wait");
wait.parameters["seconds"] = .5;
data.states[0].tasks = {wait};
data.states[0].succeededEvent = "TaskSucceeded";
data.states[0].failedEvent = "TaskFailed";
// Add transitions triggered by those events, then configure the controller.
```

## Execution and cancellation

Each controller owns its task instances; graphs and registries remain immutable.
The controller creates and starts a task during its state's first update, captures
constant/resolved blackboard inputs once, then ticks it with simulation time.
An active state ticks at most one task per fixed step. Successful intermediate
tasks start their successor on the following step; there is no zero-duration loop.
At most 16 active hierarchy levels therefore tick 16 tasks in a step. Pausing
simulation pauses tasks and deadlines.

`AiTask::tick` returns `Running`, `Succeeded` or `Failed`. A still-running task at
its deadline fails; work beyond the deadline is not ticked. A task can succeed
exactly at its deadline. Exceptions from factories/start/tick become task failure
with a diagnostic reason, permitting the authored failure transition. A null
factory result also fails. Invalid returned status values fail. A terminal task's
`cancel(controller) noexcept` releases commands/subscriptions before destruction,
including successful and failed tasks. Adapters must make this cleanup idempotent
and keep external captures alive for the registry's lifetime. Never retain the
borrowed start-spec reference, controller pointer or event reference externally;
use scene-scoped component handles and resolve them on the owner thread.

Leaving a state cancels its running work and activation-scoped queued results.
Retained ancestor tasks survive sibling transitions. Parent interrupts, stop,
disable, detach, failure, restart, configuration replacement and navigation-source
replacement use the same cleanup. Re-enable requires explicit restart. Calling
controller `stop()` from a callback defers teardown until the current machine step
unwinds, avoiding destruction of the executing task. Callback configure/restart
and source replacement cannot replace an executing runtime.

Final success or failure posts the configured event with the state's activation
token. Existing event cutoffs defer result transitions until a subsequent step.
If the queue is full, retain one terminal result and retry on later state updates;
do not recreate or retick the task. No result is posted twice after acceptance.
Leaving the state drops undelivered results. Diagnostics expose delivery pending.
This bounded local retry is specific to task results, not general event retry.

Queued occurrences are observed once when examined, before transition guards.
`StateMachineHost::event(event, optionalScope)` supplies a synchronous observation
hook; controllers forward it to active tasks whose state scope matches, or all
active tasks for an unscoped occurrence. Wait tasks only observe after starting;
earlier examined events are not replayed. Observation neither consumes an
occurrence nor bypasses FIFO, transition priority or queue budgets. An interrupt
from the same occurrence can cancel a task before it reports success next tick.

## Built-ins and application adapters

| Task | Parameters and behavior |
| --- | --- |
| `wait` | Number `seconds`, default 1, range 0..86400. Succeeds after simulation duration. |
| `wait-event` | No typed parameters. Set spec `event` to a declared event key. Succeeds on its next tick after observing that occurrence. |
| `move-to` | Vector `target`, default (1,0,0), components -10000..10000; Number `speed` default 2, .01..100; Number `distance` default .15, .01..10. Requires a motor and an explicit runtime navigation source. |
| `face-target` | Vector `target`, default (0,0,1), components -10000..10000. Requires a motor/scene-root actor, faces local +Z toward world target in the horizontal plane, and succeeds in one tick. |

`move-to` owns an ordinary `Navigator`, preserving capsule clearance, support,
source revision invalidation and bounded recovery. It never creates navigation or
ground automatically. The host calls `controller.setNavigationSource(source)`,
then `restart()`. Changing the source stops the controller. Sharing a source and
invalidating/publishing its revisions lets active navigators wait/replan normally.
Navigator and task deadlines both apply. Missing integration, unavailable motor,
unreachable route, exhausted budgets and timeout report failure. Motor-dependent
behavior and task writers cannot compete along an active hierarchy; applications
must likewise prevent other controllers from writing the actor's movement/pose.
Runtime sources are excluded from persistence and must be reattached after load
or to the isolated Play instance by its owning application.

`AiBindingRegistry::create(behaviors, conditions, tasks)` optionally accepts up to
128 `AiTaskBinding` entries, each with a schema (up to 16 fields), `needsMotor`
flag and factory returning a fresh `unique_ptr<AiTask>`. To extend the built-ins,
copy their three vectors and append your task binding before creating a registry.
Use the same registry for controllers and `aiControllerCodecs(registry)`.

A sequence **MoveTo → FaceTarget → PlayAnimation → WaitForEvent** uses the first
two built-ins and an application `play-animation` adapter. That adapter starts an
existing animation component/clip, registers completion with a scene-scoped
controller handle plus activation token, and succeeds once playback is started.
The start callback receives the owning state activation scope explicitly. Its cancel hook releases its start request/subscription ownership deliberately:
transfer a continuing subscription to an application owner if the next task must
wait for it. `wait-event.event = "AnimationFinished"` then waits for the marshaled
completion. Animation event names, clips, combat and damage rules remain game
policy. `tests/ai_task_tests.cpp` executes this sequence with a controlled animation
adapter, navigation invalidation, completion delivery and a damage interrupt;
it is lifecycle evidence, not a rendered animation acceptance claim.

## Editor and persistence

Open **AI graph editor → Task sequence template**, select **Work**, and edit
**State task sequence**. Add/reorder/remove tasks, select typed constants or
compatible blackboard sources, set timeouts and result-event selectors, then
**Apply tasks** for one validated graph-history edit. **Reload tasks** discards
staged edits. Invalid changes retain the valid graph. Undo/Redo, graph files,
assigned controller snapshots and scene files retain sequence configuration.
The browser editor uses the built-in registry; custom registries belong to native
application integration.

The template runs Wait → WaitForEvent(AnimationFinished) → Wait, then transitions
to Done; Damaged interrupts to Interrupted, and timeout uses TaskFailed. Assign
the graph to an actor, enter Play and open its live debugger. **Live tasks** shows
the active index/key, elapsed time, status, failure reason and pending delivery;
**Task history** shows at most 32 start/terminal/cancellation records. Inject
AnimationFinished after wait-event starts, or Damaged to interrupt. Source graph,
values and scene history remain unchanged by Play.

Standalone graph preview still evaluates the raw state-machine host with manual
guards; its label explicitly says tasks execute in Actor Play. A custom raw
`StateMachineHost` remains responsible for interpreting task metadata. Only
`AiControllerComponent` provides the delivered executor.

Graphs write `WEAI 5`, reading versions 1..5 (older graphs have empty sequences).
The existing `AIC 2` container, 512 KiB graph / 2 MiB controller limits and optional
outer component codec version 1 remain. No core scene archive version changes.
Task objects, progress, sources, subscriptions, pending results and history are
transient; load/Play clones start fresh. No parallel task combinators, runtime
checkpoint serialization, worker scheduler or automatic animation subscriptions
are implied. See [validation](ai-tasks-validation.md), [events](ai-events.md),
[hierarchy](ai-hierarchy.md) and [controllers](ai-controllers.md).
