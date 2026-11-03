# RPG R2: characters and transactions

R2 extends the optional `WebEngine::RPG` library with immutable character/resource/
progression definitions, session-owned character state, modifiers, XP advancement
and a shared command transaction boundary. The module API is now 1.1.0; content
revision remains application-owned. No wire snapshot schema, inventory, combat,
RNG, quest awards or host integration is added. See [foundation](rpg.md),
[architecture](rpg-library-architecture.md) and [evidence](rpg-r2-validation.md).

## Authored definitions

`CatalogDefinition` retains its R1 metadata definitions and adds `characters` and
`progressions`. `CharacterDefinition` has a stable ID, up to 64 attributes, up to
64 resources, and an optional progression ID. Attribute/resource IDs are scoped
to a character definition; their sets must be disjoint. Character, progression
and metadata IDs share one global catalogue namespace. Lists are sorted by ID
before publication. Missing references, duplicate IDs, invalid values or scales
reject the catalogue. Catalogue records, including nested value definitions,
thresholds and allowed-unlock entries, total at most 4096.

An attribute declares ID, unit ID, positive integer scale, minimum, maximum and
initial base value. Initial/base values must lie in the declared range. A resource
declares ID, unit/scale, bounds on its maximum, initial maximum and current value.
Maximum bounds are nonnegative; current must be between zero and initial maximum.
No attribute/resource name or meaning is built in.

All values use checked int64 arithmetic. `Amount{value, unit, scale}` must match
the target's declared unit and scale exactly; the library never converts units,
rescales, rounds or wraps. A scale of 100 means values are expressed in hundredths
of that definition's unit. Derived values use ordered additive integer modifiers,
then one declared clamp; an intermediate overflow rejects even if later additions
would cancel it. XP uses the progression's own unit/scale.

`ProgressionDefinition` supplies a maximum XP, up to 256 strictly increasing,
positive thresholds within that cap, up to 256 unique allowed unlock IDs and an
optional versioned advancement handler. New characters have zero XP, level zero
and no unlocks. Each crossed threshold increases level once. The last threshold
defines the maximum level; XP may continue up to its explicit cap. No combat or
time event grants XP automatically, and negative XP awards are rejected.

## Commands and state

Include `engine/rpg/runtime.hpp`; definitions are in `characters.hpp` and read-only
state/results in `state.hpp`. Use `Runtime::command(producer, sequence, operations)`
to capture the current runtime's private identity, generation and base revision.
Commands hold an owned operation list and do not enqueue or mutate state. Sequence
numbers are positive and strictly greater than the last committed number for that
producer; gaps are allowed. Producers have stable namespaced ContentIds and a
maximum of 64 per session. A failed command consumes no sequence.

| Operation | Effect in the staged candidate |
| --- | --- |
| `CreateCharacter` | Instantiate authored initial values, reserve a durable ID and report it in `Results.created` |
| `SetAttributeBase` | Replace the base within its bounds, then recompute modifiers |
| `AdjustResource` | Add a signed, unit-checked delta; reject underflow/over-cap rather than truncate the requested change |
| `SetModifiers` | Replace one named source's complete modifier set atomically and recompute derived values |
| `RemoveModifiers` | Remove an existing source and recompute; an unknown source rejects |
| `AddExperience` | Stage XP, every crossed level and validated unlock proposals together |

Commands execute 1-256 ordered operations as a single transaction. Intermediate
candidate state is visible to later operations; no live state is published until
all succeed. Newly created IDs are returned in the candidate results; a later
command can address them after successful commit. There is no symbolic reference
to a just-created character within the same command in R2.

```cpp
auto command = runtime.command(engine::rpg::ContentId("game.input"), 1,
    {engine::rpg::CreateCharacter{engine::rpg::ContentId("game.scholar")}});
auto prepared = runtime.prepare(command);
auto characterId = prepared.results().created.at(0);
if (!runtime.commit(std::move(prepared))) {
    // Candidate was invalidated; do not use the uncommitted character ID.
}
```

`Runtime::state()` exposes const character maps, producer watermarks, the identity
cursor, generation and revision. Runtimes remain noncopyable/nonmovable and own
independent state even when they share a catalogue or durable SessionId. The host
still must choose unique durable session namespaces; the private runtime token
additionally rejects commands/candidates crossing runtime instances with equal
SessionIds. Entity IDs from a different session or absent character reject.

## Preparation, publication and lifetime

`prepare` is synchronous and owner-thread-only. It validates stamps, input limits,
sequence and budgets before copying the committed State into a private candidate.
It then validates/applies all operations and advancement callbacks. Rejection,
allocation failure, callback exception or budget exhaustion leaves the live state,
identity cursor, revision and producer watermarks unchanged. The command remains
available for retry when its stamps are still current. R2 has no internal queue
or RNG; future systems must use this same transaction boundary.

`Transaction` is move-only. `state()` exposes its immutable candidate before
consumption; `results()` exposes created IDs and ordered advancement results.
Discarding a preview changes nothing. `canCommit` checks owner, generation,
revision, candidate presence and consumption state. `commit` rechecks and either
returns false unchanged or swaps shared state pointers, consumes the transaction
and publishes the precomputed revision. Commit performs no allocations, callbacks
or old-state destruction. The consumed transaction retains the previous state
until normal disposal outside publication. Its candidate `state()` accessor then
rejects; result records remain readable but cannot execute rewards by themselves.

Two candidates prepared against the same revision compete: after one commits,
the other is stale. Duplicate consumption and moved-from candidates reject.
Borrowed Runtime state views expire after a successful commit or explicit lifecycle
mutation. Copy any data needed across publication. All callbacks receive borrowed
immutable candidate data only for their call; they must not retain those references.

During preparation, reentrant `prepare`, identity allocation and lifecycle mutation
throw `ErrorCode::Reentrant`; `canCommit`/`commit` return false. Read-only inspection
is allowed. Exception paths release this guard. Calls from multiple threads are
unsupported: the host must marshal operations to one owner thread.

`invalidateCommands()` increments generation and revision without changing gameplay,
IDs or watermarks. Use it at an explicit host lifecycle boundary to invalidate
outstanding input/candidates. It is not restore or reset. The retained R1
`allocateEntityId()` reserves a standalone ID and increments revision, invalidating
candidates; it creates no character. Both stage their changes before publication.
Generation/revision overflow rejects. Entity allocation still issues UINT64_MAX
once and then becomes exhausted, never reusing an ID. Save/load remains R8.

## Modifier sources and resource maxima

A modifier source is a stable application ContentId, such as an equipment instance
or temporary buff key. It contains up to one delta per `(target kind, target ID)`.
Kinds are `Attribute` and `ResourceMaximum`; units/scales must match the target.
At most 64 sources and 256 total modifiers exist on one character. R2 knows no
inventory or equipment object: R3 will supply source ownership and transactions.

Sources are evaluated in lexical ID order, with each source sorted by target
kind/ID. Replacing a source does not stack its old deltas. Base values are preserved
separately from derived values, so removing a source recomputes rather than applies
an inverse operation. Empty sets, duplicate targets, unknown targets and invalid
kinds reject. Use RemoveModifiers to remove a source.

When an effective resource maximum decreases, current is clamped to the new
maximum. Increasing/restoring that maximum never refills the resource. This clamp
occurs after each ordered modifier operation in a candidate. Thus a remove-then-add
sequence can lower current even if its final maximum is unchanged; use atomic
source replacement when changing one source. Derived attribute/max clamps do not
permit an overflowing intermediate sum. Explicit resource deltas never clamp.

## Advancement and unlocks

Register an `AdvancementHandler{version, evaluate}` with
`Bindings::addAdvancementHandler`. Handler IDs are unique across metadata and
advancement handlers, sharing the 256-handler limit. Catalogue creation checks
the exact handler version. Registrations/catalogues retain callback providers via
the existing module snapshot contract.

For each newly crossed threshold, the pure callback receives entity ID, immutable
candidate character (including the new XP and previously accepted unlocks), and
from/to levels. It returns `UnlockProposal{accepted, unlocks}`. Rejecting the proposal,
throwing, returning duplicate IDs within that proposal or undeclared IDs rejects
the entire command, including earlier levels, resource edits and source watermarks.
No handler means no unlock proposal, while XP/levels still advance.

Accepted IDs are inserted in the character's unlock set; already-owned IDs remain
owned without another grant result. Results list only newly inserted IDs. Unlock
IDs are game-defined records, not callbacks, skill effects or external rewards.
Any application effect must be staged by a future enclosing host transaction;
an evaluator may not publish outside state or perform IO. Each callback must be
deterministic and bounded; the engine cannot preempt untrusted compiled code.

Default and hard command budgets are 256 operations and 256 total crossed levels.
A caller may lower either. Zero advancement budget still allows an award crossing
no level. Work/size limits reject atomically. Characters cap at 1024 per session;
per-character attribute/resource and source/unlock caps bound state copying and
recomputation. No time-based scheduling or automatic grant exists.

## Consumers, fixtures and R3 handoff

`examples/rpg/character_consumer.cpp` uses two neutral rulesets: a scholar with
unit-scale points and a three-threshold curve, and an operator with hundredth-unit
charge/precision and a different two-threshold curve. Both use the same library,
with sample policy in `examples/rpg/characters.hpp`.

The native/Wasm tests compare complete scripted state projections with
`tests/rpg_r2_fixture.hpp`. This canonical text is a test fixture, not an endorsed
save codec. R3 should compose inventory transfers, equipment-source replacement
and resulting character modifiers inside this existing private candidate and
publication boundary. No second transaction system or module registry is needed.
