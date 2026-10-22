# Typed AI events

Optional `engine_ai` now lets a flat or hierarchical `StateMachine` react to typed
one-time occurrences. Graphs declare `AiEventDefinition` entries; an
`AiTransition::event` selects one, while an empty event key retains a polled guard.
The transition's existing condition and minimum source-state time still apply.

```cpp
auto data = graph->data();
data.events = {
    {"HeardNoise", {{"position", engine::AiParameterType::Vector,
                     engine::Vec3{}, -100, 100}}},
    {"AnimationFinished", {}}
};
data.transitions.push_back({10, patrolId, investigateId, "always", 0, "HeardNoise"});
auto definition = engine::StateMachineDefinition::create(std::move(data));
// Configure/attach through the existing controller API, then on the scene owner:
controller.postEvent({"HeardNoise", {{"position", engine::Vec3{4,0,2}}}});
```

An event payload is an exact map of declared parameters, using the existing
Boolean, Number, Vector and saved-Actor types. Numeric/vector values must be finite
and within inclusive schema bounds. There are at most 32 event definitions with
8 parameters each; keys use the existing unique 1–256 byte no-whitespace/control
contract. Definitions and transition triggers validate before graph publication.
Unknown events, missing/extra fields and invalid payload types/ranges throw before
queue mutation. `AiControllerComponent` also validates nonempty actor payload IDs
against its scene before queuing. Later target destruction is handled by normal
saved-ID resolution, with no retained target pointers.

## Queue ownership and delivery

Each machine owns an independent queue. `StateMachineSettings` adds
`eventQueueCapacity` (default 32, range 1–64) and `eventsPerStep` (default 8,
range 1–64), after the existing transition/condition settings. Actor controllers
use the default runtime budgets. `postEvent(event, optionalScope)` returns false
for a full queue, stopped/failed runtime or expired/foreign scope. Overflow rejects
the newest input and preserves queued inputs. Schema errors throw. These are
single-owner APIs, not worker-safe messaging or asynchronous subscriptions.

- Each fixed `step` captures the last queued sequence before enter/update callbacks.
  Callback-posted events wait until the next step, even when transition budget
  remains. Native game/animation adapters marshal events onto the scene owner.
- Ready events are examined FIFO. For the front event, evaluate eligible polled
  and matching event edges together: active ancestors first, then descendants;
  stored vector order within a source. Parent interrupts therefore retain priority
  over child event/polled edges. Earlier FIFO events precede later occurrences.
- A selected event edge consumes that occurrence exactly once before exiting and
  entering states. It cannot trigger a second transition. A selected polled edge
  leaves the event queued for reconsideration in the new path, subject to budgets
  and scope cancellation.
- If no edge accepts the occurrence, discard it as unmatched. This includes a
  false guard, an inactive source or an unsatisfied minimum time. Events are not
  persistent flags that wait indefinitely for a guard to become true.
- Transition and condition budgets remain shared across event and polled work.
  Consumed/unmatched occurrences spend the event quota. Unexamined events remain
  queued for later steps; polled evaluation can still proceed when the event quota
  is exhausted. Stop, restart and failure clear pending inputs. Pause by not
  stepping; it does not drain the queue. Scoped cancellation is separately bounded
  by the queue/path capacities.

## Payload callbacks and stale completions

`currentEvent()` exposes the triggering payload during its matching guard and
accepted edge's exit/enter/transition callbacks. It is null for updates, polled
guards/transitions and outside dispatch. `StateMachine::currentEventSequence()` correlates that
occurrence with diagnostics. Copy data needed after the callback; do not retain
the borrowed payload pointer. A behavior can deliberately publish persistent data:

```cpp
// An application-registered investigate enter callback:
if (const auto* event = controller.currentEvent();
    event && event->key == "HeardNoise") {
    controller.setBlackboard({{"lastSeenPosition", event->parameters.at("position")}});
    // Application-owned navigation may now use that position.
}
```

The blackboard field must be declared with compatible bounds. Events do not
implicitly alter health, choose navigation goals or start animations. Those rules
belong to registered application callbacks.

Capture `eventScope(activeStateId)` when starting asynchronous work:

```cpp
auto completion = controller.eventScope(attackStateId);
// Later, back on the scene owner; retain a scene-scoped component handle in
// the adapter and resolve it before calling this:
controller.postEvent({"AnimationFinished", {}}, completion);
```

The token identifies the machine instance and that particular state activation.
Leaving that state cancels its queued scoped events; a late post rejects. Reentry
of the same state ID, restart, configuration replacement and another agent/Play
instance cannot validate the old token. A retained ancestor scope survives sibling
transitions. Unscoped occurrences remain valid across state changes until examined
or explicitly canceled by lifecycle control. Stop/disable/detach and configuration
handoff clear pending events. Producers still own and cancel their subscriptions;
scope validation prevents stale delivery but does not retain the scene/producer.

Instances are noncopyable/nonmovable so activation identities cannot accidentally
be duplicated. Existing callback reentry restrictions remain; controller
configuration from a running callback explicitly rejects before replacing runtime
ownership. Payload reads and blackboard writes from callbacks remain supported.

## Editor and debugger

Open **AI graph editor → Event-driven template**. The supplied graph demonstrates
`HeardNoise`: Patrol → Investigate, `Damaged`: an Alive parent interrupt into
Combat/Attack, and `AnimationFinished`: Attack → Recover or investigation/return
completion. It uses built-in idle/move/always/flag bindings and typed payloads.
For actor assignment, bind `flag.value` to the `allowDamage` blackboard field.
Payload-driven navigation/damage policies require application callbacks.

**Event definitions** adds/removes event schemas and Boolean/Number/Vector/Actor
parameters with defaults and ranges. **Apply events** is one validated graph
history edit; Undo/Redo/files retain it. Referenced event declarations cannot be
removed or renamed until their edge triggers are remapped. Invalid edits retain
the last valid graph; **Reload definitions** discards staged form edits.

Select a transition and choose **Event trigger**, or **Polled guard (no event)**.
Changing its condition/delay preserves the event trigger. The condition remains
an additional guard. Authoring is disabled during preview and scene Play.

Standalone preview and the live Play debugger both have **Event queue** panels.
Choose an event, edit typed parameters and post it for a later fixed tick. Preview
uses manual condition checkboxes: enable `always` and `flag` for the template,
then **Step** after posting. Live Play uses the actor's real registered bindings.
The **Lifetime** selector captures an active state's token; **Capture current
activation** refreshes it deliberately. Keeping the old token after a state exits
lets the debugger demonstrate stale-completion rejection.

`pendingEvents()` and `eventHistory()` expose queue depth and up to 32 diagnostic
records: queued, consumed, unmatched, canceled, overflow, stale-scope and stopped.
Records contain typed payloads, simulation time, sequence and accepted edge ID.
Rejected posts have sequence zero. Records are best-effort under allocation
failure; cancellation itself remains noexcept. Restart clears pending inputs but
retains bounded event history; configuration replacement creates a fresh history.
The transition log also records the triggering event key and sequence. Live
injection changes only the Play instance, preserving source bytes/defaults/history.

## Persistence and scope

Graphs write `WEAI 5` and read versions 1–5. Older graphs have no events and keep
their polled behavior. The graph's 512 KiB limit, `AIC 2` payload/`AIC 1` reader,
2 MiB controller limit and outer optional component codec version 1 remain.
Scene files and Play retain event schemas/triggers, but never queued inputs,
activation tokens, event histories or runtime payload contexts. Loaded instances
start with empty queues. Deploy matching host scripts and Wasm/worker artifacts.

Automatic animation/perception subscriptions, delivery
acknowledgments across machines, event TTL/retry, durable runtime checkpoints and
parallel state regions remain outside this increment. See
[hierarchical states](ai-hierarchy.md), [blackboards](ai-blackboards.md),
[controllers](ai-controllers.md) and [event validation](ai-events-validation.md).

[State task sequences](ai-tasks.md) now add registered per-activation work, cancellation, timeouts and live task diagnostics through actor controllers.
