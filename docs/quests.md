# Portable quest kernel (M2)

`WebEngine::Quests` is an optional C++20 static module. It depends only on
`WebEngine::Modules` and the standard library. It has no renderer, scene, GUI,
storage, browser, Windows, physics or puzzle-rule dependency. The same source
runs under MSVC and Emscripten. See [validation](quests-validation.md).

## Build and compose

Enable `WEBENGINE_BUILD_MODULES=ON` and `WEBENGINE_BUILD_QUESTS=ON`, then link
`WebEngine::Quests`. `quests/` also supports a standalone consumer after adding
`modules/`; [examples/quests](../examples/quests/CMakeLists.txt) shows this without
configuring engine core or renderer dependencies. Both options can be disabled.
`WEBENGINE_BUILD_QUEST_TESTS` and `WEBENGINE_BUILD_QUEST_DEMO` default off. The
demo needs the existing Emscripten host or Windows desktop target.

```cpp
#include "engine/quests/runtime.hpp"
namespace q = engine::quests;
engine::ModuleComposition<q::Bindings> composition;
auto registration = q::moduleRegistration();
auto bindings = composition.replace({&registration, 1});
auto catalog = q::Catalog::create(authoredDefinition, bindings);
q::Runtime session(catalog);
```

The explicit `webengine.quests` registration installs built-ins and declares
`webengine.quests.content` and `webengine.quests.snapshot`, both version 1.0.0.
A code plugin declares a dependency on `webengine.quests` and installs typed
condition/objective handlers into the staging `Bindings`. The delivery module
in [demo.hpp](../examples/quests/demo.hpp) implements a custom weight objective.
M1 validates dependency order and duplicate module/schema IDs; `Bindings` rejects
duplicate handler IDs. Missing handlers, parameter types, mode mismatches and
unsupported handler versions reject the catalogue before it is published.

A catalogue retains its immutable module snapshot, including provider captures.
Each runtime retains its catalogue and owns independent mutable progress.
Destroying/replacing the composition cannot invalidate an existing runtime.
Own captured services; never capture references to installation-local objects.
Registrations are compiled code, not downloadable scripts or dynamic libraries.

## Content and evaluation

`CatalogDefinition` declares typed fact defaults, exact event payload schemas,
and quest definitions. Values are `bool`, `int64_t` or `string`; use explicit
`std::string`/`int64_t` construction. Quest/stage/objective IDs are stable keys.
Keys permit lowercase ASCII letters, digits, dot, underscore and hyphen, at most
128 bytes; namespacing module-supplied handler/type IDs is recommended. Content
revision keys are explicit and separate from quest IDs and reward policy keys.

A quest has prerequisites, one entry stage, sequential stages and completion
effects. Prerequisite and stage graphs must be acyclic, references must exist,
and every stage must be reachable. Every stage has nonempty All/Any objectives.
`Current` objectives recompute their count from current state. `Accumulate`
objectives increment only for accepted events, saturating at their target.
Completion tests counts against the target; fact changes never increment an
event counter. Branches are tested in authored order and the first true branch
wins. No matching branch leaves the stage active. An empty transition list
completes; an empty `next` selects Complete/Fail/Cancel. Other branches enter a
stage with fresh counts, choice and elapsed time. Events do not spill into it.

Built-ins, all handler version 1:

| Handler | Kind | Parameters / result |
| --- | --- | --- |
| `quest.always` | condition | no parameters; true |
| `quest.fact` | condition / Current | `key:string`, `value:any`; equality with a declared fact of the same type |
| `quest.choice` | condition / Current | `value:string`; equality with this stage's selected declared choice |
| `quest.elapsed` | condition / Current | `milliseconds:int64`; stage elapsed simulation time reaches a positive threshold |
| `quest.event` | Accumulate | `event:string`; optional paired `field:string`/`value:any` filter; one matching occurrence |

Current boolean objectives return 0/1. Custom objectives return a bounded-by-target
uint32 count or increment according to their registered mode. Schema validation
and evaluation callbacks must be deterministic, side-effect-free and bounded.
They borrow inputs only during the call and must not access wall clocks, mutate
game/scene/economy state or recursively mutate the runtime. Exceptions propagate
with live state intact. Work budgets count callback invocations, not CPU time;
the engine cannot preempt an ill-behaved compiled callback.

## Input scope, order and budgets

One owner thread calls the runtime; there are no locks, scheduler or subscriptions.
Use `batch()` for current generation/revision stamps. Commands also carry the
quest's activation stamp. Activate requires Available; Retry requires an attempted
quest and preserves its durable earned flag and receipts. Choose selects one
declared choice on an active stage; Fail/Cancel terminate an active attempt.
Prerequisites use durable earned completion, so practice cannot relock successors.

Create events through `runtime.event(...)` and enqueue with `post(...)`. Events
carry a private runtime identity plus generation, activation and stage stamps.
Source sequence numbers must be positive and strictly increasing across all
quests for that source, including already queued input. Duplicates, wrong types,
cross-runtime events, stale scope and queue overflow reject before enqueueing.
Use stable producer IDs, not a new source per event. Do not post during evaluation.
After restore, continue from saved source watermarks. Save only committed state;
the transient queue is deliberately absent from snapshots.

Preparation applies fact patches, advances timers of previously active stages,
executes commands in supplied order, settles active quests in lexical quest-ID
order, refreshes availability, then processes a bounded FIFO event prefix.
Current objectives and branches can settle through several stages in one batch.
Time is not carried across stage transitions. A stage activated in this batch
starts at zero elapsed time. Queue entries whose scope becomes stale during the
batch are consumed with `discarded-scope` diagnostics and advance their source
watermark; they cannot count toward the new attempt/stage.

The host supplies integer simulation milliseconds in `Batch::deltaMs`. It should
derive them from the existing Application simulation clock, preserving fractional
remainder as necessary, and supply zero while paused. The kernel never catches up
using wall time. The [M3 host sample](quests-host.md) now drives this from Application simulation
deltas, including pause, suspension and scene travel.

Default work budget is 64 events and 4096 evaluator calls per preparation. The
event prefix may commit while later queued events remain. On evaluator budget
exhaustion, `Prepared` returns BudgetExceeded with no transaction: **no facts,
commands, timers, progress, effects or queued input are committed**. Retry the same
batch with a larger budget or smaller input; never drop elapsed time silently.
The maximum explicit budget is 256 events and 1,048,576 evaluator calls.

## Candidate publication and effects

```cpp
auto batch = session.batch();
batch.facts["gate"] = true;
auto prepared = session.prepare(batch);
if (!prepared.transaction) { /* retry with an adequate budget */ }
else {
    auto& candidate = *prepared.transaction;
    // Validate effects and stage the game's economy/journal together first.
    // Abort by discarding candidate if the game rejects any effect.
    // On the same owner thread, publish staged game state without throwing
    // only after this succeeds. No externally visible work during preparation.
    if (session.commit(std::move(candidate))) { /* noexcept host publication */ }
}
```

Transactions expose immutable candidate state, typed effect requests and bounded
diagnostics. They retain a runtime identity and exact base state/queue revisions.
Any commit, restore or successful post invalidates older candidates. `canCommit`
is only an owner-thread observation, not a concurrency lock. `commit` rechecks,
returns false on stale/cross-runtime input, and performs no allocations/callbacks.
Borrowed `state()`, `view()` and diagnostic references expire on successful
commit/restore. A transaction's state is valid only before successful consumption
or move. The application must own atomic publication of its other state; the
kernel does not atomically commit a wallet, filesystem or external service.

First completion proposes each effect with a stable receipt `quest-id/effect-key`.
It marks durable earned completion and receipts only in the candidate. Retries,
reload, later ticks and practice cannot emit those effects again. Policy keys are
independent of content revision and activation. Effect payloads/types remain game
policy, validated by the host before publication. `Batch::replay` rebuilds earned
state and receipts while suppressing emitted effects; use it only when rebuilding
the associated game state from an authoritative journal, never as a way to defer
a live reward. Persistence must save the quest snapshot and game ledger together.

## Snapshots, validation and limits

`encodeCatalog`/`decodeCatalog` and `encodeSnapshot`/`decodeSnapshot` use canonical
bounded little-endian formats: 8-byte `WEQCAT\r\n` / `WEQSAV\r\n` magic, uint32
version 1, explicit lengths/tags and trailing uint64 FNV-1a checksum. Maps have
lexical order. No raw structs, addresses, scene handles, timers from the OS,
callbacks, pending events or diagnostics are serialized. These bytes can be
wrapped in the game's outer archive. No platform IO is part of this module.

Catalogue fingerprints cover canonical content and handler references/versions.
Changing handler semantics requires changing the handler version. Snapshots must
match the catalogue ID, content revision and fingerprint. Validation checks exact
facts/quest sets, statuses, possible stage paths, choices/counts, prerequisite
consistency, earned receipts, source watermarks and ranges. Validation is
structural; it does not prove a player legally earned an award. Checksums and
fingerprints detect accidental mismatch, not malicious tampering. Games retain
their own authoritative puzzle journals and save compatibility policy.

`restore` validates and stages first, then rebases generation and revision above
both saved and current values, clears queued input and diagnostics, and emits no
effects or callbacks. Failure leaves live state/queue intact. Old commands,
events and candidates become stale. Counter/time exhaustion rejects; no wrap.
There is no implicit content migration or receipt translation in M2.

| Limit | Value |
| --- | --- |
| Quests / stages per quest / objectives per quest | 1024 / 64 / 128 |
| Catalogue total stages / objectives / branches / fields | 4096 / 8192 / 16384 / 65536 |
| Prerequisites per quest / choices and branches per stage / effects per quest | 64 / 16 / 16 |
| Fact keys / event types / fields per payload or parameter block | 256 / 128 / 32 |
| Condition handlers / objective handlers / parameters per schema | 256 / 256 / 32 |
| Queue / sources / commands per batch / retained diagnostic entries | 256 / 64 / 64 / 64 |
| Objective target / key bytes / string value bytes / encoded bytes | 1,000,000 / 128 / 1024 / 16 MiB |
| Stage timer / batch delta | at most 315,360,000,000 ms |

Limits reject, never truncate authored data or silently drop pending input.
Diagnostics retain the latest 64 entries. `Error` carries an `ErrorCode` and
readable message; callback exceptions propagate unchanged. Invalid wire/type
fields can report Limit/TypeMismatch as well as InvalidContent/InvalidSnapshot.
There is no arbitrary script VM, cyclic state machine, dynamic unload, parallel
quest regions, scene ownership, journal UI, save transport or Paws migration.
