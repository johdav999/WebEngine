# Typed AI blackboards

An immutable `StateMachineData::blackboard` schema declares up to 64 fields.
`AiParameter` and `AiValue` live in `engine/ai_value.hpp`; Boolean, Number, Vector
and Actor reuse the controller parameter types. Names are unique 1–256 byte keys
without whitespace/control characters. Numeric and vector defaults/updates must be
finite and within the declared inclusive bounds. Actor values are optional saved
actor IDs, resolved against the owning scene rather than retained pointers.

Each `AiControllerComponent` copies the graph defaults into its own live map.
Shared graphs never share mutable values. Update inputs synchronously on the scene
owner, normally in the owning actor's fixed tick before child AI and motor ticks:

```cpp
auto data = graph->data();
data.blackboard = {{"targetVisible", engine::AiParameterType::Boolean, false},
                   {"velocity", engine::AiParameterType::Vector,
                    engine::Vec3{1,0,0}, -100, 100}};
auto definition = engine::StateMachineDefinition::create(std::move(data));
auto settings = engine::AiControllerSettings::defaults(definition, *registry);
settings.states.at(moveState).sources["velocity"] = "velocity";
settings.conditions.at("flag").sources["value"] = "targetVisible";
// Attach using the existing actor controller API, then feed gameplay inputs:
controller.setBlackboard({{"targetVisible", visible},
                         {"velocity", engine::Vec3{speed,0,0}}});
```

`setBlackboard(patch)` validates the complete patch before publishing it. Unknown
fields, wrong types, nonfinite/out-of-range values and nonempty actor IDs absent
from an attached controller's scene reject without changing any values, current
state, timers, commands or transition history. Live references whose actors are
later removed/disabled resolve to null. Detached writes validate ID syntax; scene
resolution becomes available after attachment. These APIs are single-owner APIs,
not worker-safe subscriptions or an asynchronous event queue.

`AiBinding::sources` maps a parameter name to a field name. `parameters` still
contains a complete valid constant configuration. A source overrides its constant
at each enter/update/exit/condition callback, which receives a resolved value copy.
Source types must match exactly; numeric/vector field bounds must fit entirely
inside the consuming parameter bounds. Unknown source fields/parameters reject
the complete configuration. Collision-aware `move` requires Y=0 both for defaults
and live writes. Narrow fields or adapter callbacks enforce additional game rules.
`flag` and `not-flag` provide positive/negative Boolean guards.

`blackboard()` and `blackboardValue(key)` expose read-only live values; an unknown
lookup throws. Returned maps/value references are borrowed until the next patch,
configuration replacement or component destruction. `restart()` retains live gameplay inputs while resetting runtime
timers/history; `configure()` replaces the schema/configuration and resets values
to defaults. Stop/disable cancels the controller normally and retains inputs.

## Editor and debugger

Open **Details → AI state machines → Open AI graph editor**. In **Blackboard
fields**, add/remove fields and edit their type, default and bounds, then choose
**Apply fields**. This is one validated graph-history edit; Undo/Redo and `.weai`
files retain the schema. Schema editing is gated during graph preview and Play.
The standalone graph preview still uses its manual guard checkboxes; it does not
execute gameplay callbacks or actor bindings.

Choose **Use open AI graph** in **Actor AI controller**. Each parameter offers
**Constant** or a compatible **Blackboard: field** source. Apply the assignment
through the existing scene history. Removing/renaming a field in the graph does
not silently mutate previously assigned snapshots. Reassign and remap explicitly.

During Play, **Live debugger during Play** shows current values, editable typed
inputs and guard labels in the bounded transition log. Live edits affect only the
selected reconstructed Play instance and never modify source defaults/history.
The values shown alongside controls refresh even while an input is being edited;
rejected writes retain the old runtime values. Transition labels identify the
accepted graph guard, not arbitrary callback explanations or an exhaustive guard
evaluation trace.

## Persistence and example

Graph files now write `WEAI 5`; readers accept versions 1, 2, 3, 4 and 5. Version 1 has an
empty schema. Version 3 adds [hierarchical states](ai-hierarchy.md); version 4 adds [typed events](ai-events.md). Controller payloads write `AIC 2` with parameter-source mappings and
accept version 1 constants. The outer optional component codec stays version 1;
the payload version identifies its grammar. Existing 512 KiB graph / 2 MiB
controller limits are checked on save and load. Matching browser code/Wasm must
be deployed together. Scenes and Play retain schema/defaults/bindings, while live
values, timers, commands and transition history remain transient.

The application-owned patrol demo in `src/ai_demo.cpp` now uses an actor controller
with live perception flags, `target` and `lastSeenPosition`. Actor fixed ticks
publish observations; registered application callbacks own the Navigator and
patrol/follow/investigate/return policy. Target loss leads to investigation and
return while navigation failure remains a bounded blocked state. Custom registries
and their captures remain application-owned; generic bindings do not acquire
automatic perception, navigation or callback discovery.

See [controller contracts](ai-controllers.md) and
[blackboard validation](ai-blackboards-validation.md).

[State task sequences](ai-tasks.md) now add registered per-activation work, cancellation, timeouts and live task diagnostics through actor controllers.
