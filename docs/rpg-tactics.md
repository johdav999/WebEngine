# RPG tactical actions, spatial quotes and reactions (R6)

API 1.5.0 adds tactical resolution to the same `WebEngine::RPG` transaction.
It depends only on Modules and C++20. Sample policy is in
[examples/rpg/tactics.hpp](../examples/rpg/tactics.hpp), with
[validation evidence](rpg-r6-validation.md). Navigation, physics, scene ownership,
camera, rendering and game balance remain outside the kernel.

## Commands and authority

Initialize durable integer positions with `SetTacticalPosition` outside encounters.
Positions use game-selected quantization; there is no implicit meter/cell conversion
or floating-point calculation. Inside encounters only a committed tactical move
changes these positions. The state map is bounded by the existing character cap.

Use a standalone `TacticalAction` command for an ability, item-consuming ability,
or `MoveAction`. It names the active actor, exact encounter/turn epochs, action
payload and immutable spatial quote. Ability caster must match that actor. Raw
`UseAbility` now rejects inside **any** encounter; it remains available for
exploration. Existing trusted administrative character/inventory/status commands
are not player actions and must never be exposed as unrestricted input.

Extend the retained `EncounterHandler` with `authorize(const TacticalContext&)`.
It is optional for lifecycle-only encounters; tactical actions reject without it.
The returned `ActionPlan` accepts/rejects and supplies nonnegative AP, additional
movement and resource costs, plus an optional named cooldown. Move cost is checked
addition of quoted path cost and policy movement cost. Ability resource/item costs
still apply through R4 and are additional to policy costs. Exact units, scales,
checked arithmetic and resource bounds remain R2/R4 contracts.

An ability whose ID is an active cooldown key rejects. A plan's optional cooldown
key must also be absent; positive duration/phase/count validation uses R5.
Policy resource costs are sorted, unique and nonnegative. Costs cannot exceed
remaining allowances; rejection preserves every balance, item, modifier and RNG.
No refill or automatic EndTurn occurs when AP reaches zero.

## Immutable query facts and publication

`Runtime::spatialQuote(actor, snapshot, optionalPath)` copies host query results
into a privately constructed immutable `SpatialQuote`. It stamps private runtime
identity, generation/revision, actor and encounter/turn epochs. The host cannot
construct or restamp a quote directly. Retaining a quote does not keep the runtime
alive or make it valid after a publication.

`SpatialSnapshot` supplies a nonzero `WorldStamp {identity, revision}`, complete
roster positions/residency and the legal target IDs produced by host range/LOS
queries. Entity/target lists are bounded and sorted; duplicates reject. Preparation
requires every roster member resident and its quoted position equal to committed
tactical position. Ability targets must occur in the legal list. The kernel does
not infer range, line of sight or navigation from the coordinates.

`PathQuote` contains start/destination, nonnegative quantized cost, 2–256 route
points and reachable/resident/free query results. All three results must be true;
endpoints must match, start must equal the actor's committed position, and zero
displacement rejects. The destination cannot equal another participant's committed
position. The host remains responsible for route connectivity, intermediate
obstacles, volume/shape occupancy and nonparticipant blockers. Ability quotes must
not contain movement paths. Missing/stale/malformed facts reject without effects.

Pass the currently observed world stamp to both preparation and publication:

```cpp
auto quote = runtime.spatialQuote(actor, snapshot, path);
auto command = runtime.command(producer, sequence, {
    TacticalAction{actor, encounterEpoch, turnEpoch, MoveAction{}, quote}
});
auto candidate = runtime.prepare(command, Budget{}, currentWorldStamp);
// Acquire/confirm route reservation and freeze conflicting writers first.
// Read the current stamp again; do not reuse a stale cached observation.
if (runtime.canCommit(candidate, currentWorldStamp))
    runtime.commit(std::move(candidate), currentWorldStamp);
```

`prepare`'s third argument and `canCommit`/`commit`'s second argument are optional
for existing nonspatial transactions. A tactical candidate requires its exact
world stamp at commit; omission or mismatch returns false without consuming the
candidate. Runtime revision checks still apply. Publication compares supplied
values and swaps state with no callbacks, allocation or external queries.

The world stamp is a trusted owner-thread host observation, not a cryptographic
proof or an atomic lock. Its identity must distinguish worlds and its revision must
change whenever relevant query facts change; never recycle either while quotes
may exist. No cross-thread mutation may interleave the final observation and
commit. A cached old stamp passed dishonestly cannot be detected by this library.

## Resolution order and bounded reactions

1. Validate command/quote/world stamps, active actor/eligibility, roster positions,
   target/path legality and the ability-ID cooldown gate.
2. Prepare BeforeAction reactions in deterministic order. Recheck actor eligibility
   and spatial/cooldown facts after them. If a before reaction incapacitates the
   acting character, the entire action (including that reaction) rejects.
3. Call authorization against the resulting candidate. Check costs and plan
   cooldown, subtract AP/movement and additional resource costs. Resolve the R4
   ability/item operation or commit the destination in candidate state. Set the
   optional plan cooldown.
4. Prepare AfterAction reactions. Reconcile encounter outcome and active-actor
   eligibility once after the complete chain, including any automatic R5 phases.
5. Increment checked action epoch and publish a pending presentation plus action
   results with actor/epochs, ability ID, costs and copied move route. Whole-command
   failure preserves state, RNG, watermarks and the live presentation gate.

Reaction discovery/evaluation are optional paired callbacks on `EncounterHandler`;
both require tactical authorization. `reactions(context, phase, optionalParent)`
returns source-ID/content-ID tokens. `react(ReactionContext, Random&)` returns an
R4 `EffectProposal` using the candidate RNG. Callbacks borrow immutable views and
must be pure, bounded and retain no references. Module snapshots own captures.

Root discovery runs separately for BeforeAction then AfterAction. Within a phase,
the pending frontier sorts by descending **round initiative**, ascending source
EntityId, then lexical token ID. After a reaction, discover children in the same
phase and merge them into that frontier. Causal parents execute before children;
newly discovered children cannot precede their own parent. Each `(source, token)`
is queued at most once across the complete root action, including both phases.
Already seen tokens are ignored, breaking self/cross cycles; duplicate tokens
within one proposal reject. Ineligible sources are skipped and remain seen.

Reaction effects may target only roster members, using existing checked
resource/status/dispel operations. Repeated status-source writes within one
proposal reject, matching R4. They cannot move, summon, invoke lifecycle commands
or open a new root action. Reactions are staged data/effects, not live interrupts.

Default/hard caps: 256 unique reaction tokens, depth 8 (root reactions have depth
1), path points 256. `Budget::reactions` and `reactionDepth` may be smaller. New
children beyond depth/count reject, including the already prepared parent/root.
Discovery, proposal visits, eligibility and evaluator calls consume the shared
4096 encounter-work budget; all costs/effects consume the existing effect budget
and all evaluator RNG draws consume the existing random budget. Result vectors
share the 512-record total. Compiled callbacks cannot be forcibly preempted.

## Presentation, cancellation and R9 host requirements

`State::presentation` is the input gate and remains present even if resolution
finished the encounter or advanced to another actor. It carries globally increasing
action epoch, originating encounter/turn, actor, world and optional destination.
The encounter's own stage continues to describe its already resolved state.

While pending, every operation except a standalone `AcknowledgePresentation`
rejects, including EndTurn/exit/admin mutations. `allocateEntityId` also rejects.
The acknowledgement must match all three pending epochs. Completed, Skipped and
Recovered are explicit host-selected completion reasons with identical simulation
behavior: clear the gate and emit one presentation record. They never apply costs,
effects, RNG, reactions, outcome callbacks or turn phases again. Duplicate, stale,
invalid-reason or batched acknowledgements reject. Tactical actions are also
standalone commands, so one command cannot bypass the gate with an action/ack pair.

Discarding an uncommitted candidate cancels preparation without changes. After
commit, cancel only visual playback: rebuild/snap visuals to committed positions,
resources and encounter state, then acknowledge Skipped or Recovered. There is no
physics rollback or cost refund. `invalidateCommands` cancels old queued input and
prepared transactions while preserving the pending gate; the host creates a fresh
acknowledgement command from current state after recovery. No save API is added.

R9's browser/Windows hosts must:

- Use existing navigation/physics to create immutable query results on the owner
  thread, and keep scene handles out of RPG state.
- Reserve route and destination and freeze conflicting transform/physics/AI
  writers before the final world-stamp observation and commit. Reject and release
  reservations if preflight/publication fails. Reservation ownership itself must
  not invalidate the quoted facts; if it changes them, query/prepare again.
- Keep those reservations/writers frozen through playback. Publish authoritative
  destinations once, then interpolate only presentation. Acknowledgement alone
  must never move a physical actor or apply damage again.
- On focus loss, scene replacement or renderer interruption, discard stale input,
  rebuild from committed state and release the gate/reservations only after
  projection recovery. Do not let another writer take over midway through a path.

These are documented integration requirements, not implemented host adapters or
GPU acceptance. R7 receives the same single prepare/preflight/commit boundary for
coordinated quest rewards; R8 must serialize durable positions/counters and reject
saves while presentation is pending. R9 delivers [shared rendered sample hosts](rpg-hosts.md); see [actual acceptance evidence](rpg-r9-validation.md).
