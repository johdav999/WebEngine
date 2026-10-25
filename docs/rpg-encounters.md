# RPG encounters and turn budgets (R5)

API 1.4.0 adds portable encounter lifecycle operations to `WebEngine::RPG`.
It retains R2's single prepare/commit transaction and R4's status/RNG semantics.
No Application, scene, device input, navigation, renderer, clock or new scheduler
is involved. [The sample rules](../examples/rpg/encounters.hpp) provide one neutral
policy. [Validation](rpg-r5-validation.md) records actual platform results.

## Policy registration and ownership

Register an `EncounterHandler` through `Bindings::addEncounterHandler`. Its ID
shares the existing handler namespace and 256-handler cap. All four callbacks and
a positive version are required. `BeginEncounter` selects an exact
`HandlerReference` from the catalogue's retained module snapshot. No per-encounter
callback is stored in state. The snapshot keeps owning provider captures alive;
replacing a composition does not alter a running encounter's policy.

Callbacks receive borrowed `EncounterContext {const State&, const EncounterState&}`:

- `eligible(context, entity)` selects whether an actor can take a turn.
- `initiative(context, entity)` returns a signed int64 score. Higher acts first;
  ties use ascending durable EntityId. No subtraction or floating point is used.
- `allowance(context, entity)` returns nonnegative int64 AP and movement units.
  Their interpretation and balance belong to the game.
- `decide(context, check)` returns acceptance and a typed outcome. Automatic checks
  may continue or return Victory, Defeat, Escape or Stalemate. Finish must return
  a terminal outcome. Exit must approve and preserve the recorded terminal outcome.

Callbacks are pure, bounded compiled game policy. They may inspect candidate
state but must not retain references, perform IO, mutate external state or another
runtime, or reenter runtime mutation. Exceptions/reentry/budget failures abort the
whole command. Invocation budgets cannot preempt a malicious callback. Initiative
has no private entropy source: deterministic scores use candidate state. Random
effects remain on R4's candidate stream; lifecycle itself draws no randomness.

## Committed state and operations

No `State::encounter` means inactive. An encounter exposes `AwaitingCommand` or
`Finished`; intermediate phase work exists only inside a private candidate.
It holds the selected handler version, encounter epoch, round, turn epoch, fixed
sorted roster, scored initiative order, cursor, optional active actor, allowance
and outcome. There are no scene pointers or transient provider handles in it.

`BeginEncounter` requires no existing encounter and 1–64 unique existing character
IDs. Inventories/reserved IDs/foreign-session IDs reject. Encounter epochs increase
monotonically across exit/reentry. Turn epochs increase globally on each started
turn, including a turn incapacitated by its start phase. All uint64 counter overflow
rejects. Finished encounters remain inspectable until an approved exit.

`EndTurn` requires the current active EntityId and exact encounter/turn epochs.
`FinishEncounter` and `ExitEncounter` require both epochs. Finish uses policy to
approve a terminal outcome; exit requires already finished state. Duplicate EndTurn
under a new producer sequence still rejects. The original command also retains
runtime owner, generation, revision and producer sequence protection.

`EncounterResult` records Began, RoundBegan, TurnBegan, TurnEnded, Skipped, Finished
and Exited with epochs, round, optional actor, outcome and allowances at that event.
Its vector is ordered; separate resource/status/cooldown result vectors retain
their own processing order. There is no global interleaved presentation event log.
Results describe committed changes and do not execute effects when replayed.

## Exact phase and outcome ordering

1. Begin validates the whole roster/policy, increments encounter epoch, records
   Began and evaluates an automatic outcome before any phase. A terminal result
   finishes immediately at round zero, without status ticks.
2. Starting a round increments round, clears the old order/cursor and runs
   RoundStart on roster members only, in ascending EntityId order. Evaluate an
   automatic outcome. If continuing, evaluate each roster member's initiative
   once in ID order against the same completed phase state and empty order,
   including incapacitated members. Publish the sorted order and record RoundBegan.
3. Find the next eligible member. Recheck eligibility against current candidate
   state; never reorder mid-round. Ineligible members get Skipped and no owner
   phases. If the roster has no eligible member, finish as Stalemate. Policy was
   evaluated before seeking and can choose a different terminal outcome.
4. Set active and increment the global/encounter turn epoch. Run OwnerStart for
   that actor, then evaluate an outcome and eligibility. If still eligible, call
   allowance and refresh AP/movement **once, after OwnerStart**, then record
   TurnBegan. If OwnerStart incapacitates the actor, run its matching OwnerEnd,
   record TurnEnded, clear active/allowance, advance cursor, evaluate and seek again.
5. EndTurn runs OwnerEnd even if its actor has become incapacitated. Record
   TurnEnded, clear active/allowance and advance cursor; evaluate outcome before
   seeking. At order exhaustion, run RoundEnd, evaluate, then start the next round.
6. After each non-lifecycle operation in a command, evaluate outcome. If continuing
   but the active actor is now ineligible, close its turn as in step 5. Future
   incapacitated actors are skipped when encountered. Failures in this automatic
   work roll back the initiating operation, including ability costs and RNG.

A terminal outcome immediately clears active/allowances and records Finished;
it does not synthesize extra OwnerEnd/RoundEnd phases. Explicit finish/exit also
add no phase. Statuses and cooldowns survive exit. Games explicitly clear/convert
them as needed. While any encounter exists, external `AdvancePhase` rejects;
the encounter exclusively generates checked global phase epochs. Outside encounters,
R4's explicit owner/all-character round phase operations remain available.

## Cooldowns, durations and work bounds

`SetCooldown` sets/replaces a named cooldown on an existing character. Zero clears
it. Positive remaining occurrences are bounded at 65535, with at most 64 keys per
character (and therefore 65536 across the bounded character population). Keys need
not be ability IDs; association, duration choice and action legality are game policy.

Each phase visits characters in EntityId order. Before that character's statuses,
matching cooldowns decrement in key order; zero removes the key. Each change emits
`CooldownResult`. Then R4 statuses run in source order, applying tick effects,
decrementing duration and recomputing on expiry. R4's includeApplicationPhase=false
skips the first matching status hook; cooldowns always count the next matching
hook. Unstarted/skipped owner turns do not tick owner cooldowns or statuses.

Paused, idle or inspected state never advances a phase, cooldown, round or turn.
No elapsed-time API exists. AP/movement are exposed for R6 but not consumed by R5:
R4 ability/resource operations remain trusted low-level operations, without tactical
turn authorization. Hosts must not expose arbitrary operations as untrusted player
input. R6 supplies tactical costs, spatial quotes, reactions and presentation gating.

`Budget::encounterWork` defaults to and caps at 4096 per whole command. Every policy
callback, phase dispatch and seek iteration consumes one unit. The existing effect
budget also covers each cooldown set/tick and all status effects. A pathological
eligibility/start/end cycle fails within budget without publishing a partial turn.
No eligible roster finishes safely instead of manufacturing empty rounds. Recursive
status effects remain impossible in R4; bounded reactions are R6. The combined
result cap of 512 includes encounter and cooldown records. No queue is introduced.

## R6 inputs and exclusions

Use committed encounter/turn epochs, active actor, eligibility policy, roster/order,
allowance balances, cooldowns, phase hooks, outcome and the same prepare/commit
candidate for tactical authorization. The host still owns commands and presentation.
No simultaneous turns, mid-round summons, delay-turn, tactical positions, movement
cost application, reaction system, pending-animation state or snapshot wire format
is delivered here. Browser/Windows rendered integration remains R9. RPGGame's pin
is unchanged pending explicit adoption.

## R6 integration update

[R6 tactical contracts](rpg-tactics.md) supersede the earlier handoff: combat
UseAbility must now use a standalone TacticalAction with a retained spatial quote
and tactical authorization. Raw UseAbility remains exploration-only. Pending
presentation blocks mutation until a matching standalone acknowledgement, which
does not run phases or outcome callbacks. Trusted administrative operations remain
host-owned; never expose them as arbitrary player actions.
