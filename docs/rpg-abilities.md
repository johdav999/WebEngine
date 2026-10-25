# RPG abilities, statuses and randomness (R4)

R4 extends `WebEngine::RPG` to API 1.3.0. It uses the R2 command transaction and
R3 inventory ownership. It adds no dependencies, scheduler, encounter state machine,
damage model, script interpreter or renderer. Sample policies are in
[examples/rpg/abilities.hpp](../examples/rpg/abilities.hpp); they demonstrate
random damage, item-consuming healing and non-damage focus changes using named
resources. None of these names or balance choices are engine defaults.

## Ability catalogue and providers

`CatalogDefinition::abilities` contains `AbilityDefinition` records: content ID,
exact handler version, exact parameter schema version, a bounded map of named
`Amount` parameters, resource costs, target requirements and optional item cost.
The existing typed `Bindings` registry adds `AbilityHandler` with required pure
`validate(const AbilityDefinition&)` and
`evaluate(const AbilityContext&, Random&) -> EffectProposal` callbacks.
Handler IDs share the existing metadata/advancement registry's uniqueness and
256-entry cap. Immutable module snapshots retain provider captures throughout
catalogue/runtime use. The existing provider ownership and reentry contracts apply.

All structural catalogue checks finish before any provider validator runs.
Validation rejects unknown handlers, mismatched handler/schema versions, duplicate
content IDs, invalid target/count/stack/phase ranges, duplicate cost resources,
invalid item references and negative costs. The handler validator must reject
unknown parameters, wrong units and unsupported ranges for its schema. This is
typed compiled policy, not a generic expression language. Parameter schema,
module API, content revision and saved RNG version are separate contracts.

`UseAbility` names caster, definition, targets and optional consumed ItemId.
The caster and every target must exist. Targets are sorted by EntityId and must
be unique; min/max counts and self/other eligibility come from the definition.
There is no range, faction, line-of-sight, unlock or class rule in the kernel;
an evaluator may inspect candidate state to reject game-ineligible actions.
Spatial/turn legality arrives in R5/R6.

## Atomic cost and effect order

Within the existing command candidate:

1. Validate the ability, caster, target count, uniqueness and relation rules.
2. Subtract nonnegative resource costs in resource-ID order. Units must match;
   insufficient resources reject. Zero costs are permitted and consume work budget.
3. If an item cost is declared, require exactly one matching item argument in the
   caster's own loadout, then remove its declared positive quantity using R3's
   operation. Equipped items cannot be consumed. Unexpected/missing item arguments
   reject; cost quantity must fit a definition's stack maximum.
4. Invoke the retained evaluator with a const candidate view **after costs** and
   the command's candidate Random view. Do not retain either borrowed view or
   mutate external state, IO, providers, scenes or other runtimes.
5. An accepted proposal applies its effects in vector order. Effect alternatives
   are ResourceEffect, ApplyStatus and DispelStatus. Each entity must be the caster
   or a selected target. Reusing a status source for the same entity within one
   proposal rejects, even for refresh/stack policies.
6. Publish once through the existing no-throw/no-allocation state swap. Any failed
   effect, cost, item removal, callback, overflow or budget discards all changes,
   including candidate random draws and command watermarks.

Resource effects use checked signed int64 addition and exact Amount units/scales.
Out-of-range current resources reject; healing does not silently clamp. Derived
modifier values retain the R2 clamp semantics. Results expose ordered R4 resource
changes, status changes and successful ability uses, for presentation only.
Reading/replaying results cannot execute effects. Ordinary R2 resource operations
retain their original result behavior.

## Status identity and modifiers

`StatusDefinition` declares a named expiry phase, positive duration in matching
phase occurrences, stacking policy, maximum stacks, application-phase inclusion,
additive modifiers and optional per-stack resource tick effects. Each character's
status map is keyed by application ContentId source. Sources are separate from
ordinary modifiers and equipped ItemIds. `SetModifiers` and `RemoveModifiers`
cannot alter status contributions. A source cannot change its definition in place;
explicitly dispel first. Different source IDs are independent instances even when
they share a definition.

| Policy | Reapplication to the same definition/source |
| --- | --- |
| Reject | Reject the entire command. |
| Refresh | Keep one stack; reset duration and application-phase inclusion. |
| Stack | Add one stack up to maximumStacks; at the cap reject. Keep remaining duration/inclusion unless refreshOnStack resets both. |

Non-stacking definitions require maximumStacks=1 and refreshOnStack=false.
Each stack contributes the definition's modifiers and tick effects. Modifier
target existence and units are checked against the actual character at application.
Recompute adds ordinary sources by ContentId, equipment by ItemId, then statuses
by source ContentId, each stack in index order and each modifier by kind/target.
Every addition is checked before final clamps. The combined 256-modifier cap counts
every per-stack contribution and applies equally to ordinary/equipment changes.
Expiry/dispel recomputes from base values; shrinking a maximum clamps current,
while later increasing it does not refill. Missing dispel sources reject.

## Explicit phase hooks and R5 handoff

`AdvancePhase` is an explicit owner-thread command with one of OwnerStart,
OwnerEnd, RoundStart or RoundEnd and a strictly increasing nonzero global epoch.
Owner phases require an existing character; round phases require no owner.
Duplicate/stale epochs reject even under a new command producer/sequence.
Epoch exhaustion does not wrap. This is a host-supplied phase identity, not a
wall clock or inferred encounter round.

Only matching-phase statuses on the selected owner (or all characters for round
phases) participate, ordered by EntityId then source ContentId. Processing is:

- If includeApplicationPhase=false, the first matching hook after application or
  duration reset clears skipNext without ticking or decrementing. Otherwise that
  first matching hook counts. An unrelated phase/owner does not consume this skip.
- On a counted hook, apply tick effects per stack in resource-ID order, then
  decrement remaining occurrences once (not once per stack).
- At zero, expire and recompute modifiers before processing the next source.
  Skipped/ticked/expired status records are ordered with processing.

The first upcoming matching hook is the precisely defined application-phase
boundary. R4 does not infer which encounter turn an application occurred in. R5
must place applications and these hooks consistently: e.g. an OwnerEnd status
applied during that owner's turn counts that turn's end when inclusion is true,
or skips it when false. Definitions with OwnerStart measure upcoming start hooks.
No implicit duration conversion occurs outside encounters; the game explicitly
dispels or advances phases by policy. Pause, rendering, animation and elapsed
seconds never tick a status. R5 must authorize phase order/owners and generate
epochs; R4 alone does not prevent a host from issuing a semantically wrong phase.

Every phase is atomic across affected characters. A tick underflow or budget
failure preserves all statuses, resources and the previous phase epoch. Tick
effects cannot create effects recursively; trigger/reaction systems remain R6.

## Deterministic random contract

Version 1 uses PCG-XSH-RR 64/32 with a fixed stream selector 54, increment 109 and
multiplier 6364136223846793005. The unsigned state recurrence is modulo 2^64.
For the previous state, xor with its right shift by 18, shift right 27 and narrow
to uint32; rotate right by the previous state's high five bits. This follows the
[published PCG algorithm](https://www.pcg-random.org/download.html); tests use its
[official six-output seed vector](https://www.pcg-random.org/using-pcg-c-basic.html).

Seed expansion starts state at zero, advances once, adds the supplied uint64 seed
modulo 2^64, and advances again. Runtime's third constructor argument selects the
seed (default zero); there is no automatic time/entropy source and no live reseed.
Seed 42 gives initial state 1753877967969059832. Separate runtimes with the same
seed deliberately produce the same sequence.

Random::uniform(bound) samples [0,bound), rejecting bound zero. It rejects raw
values below ((0-bound) modulo 2^32) modulo bound before taking value modulo bound,
avoiding modulo bias. Every raw draw, including discarded values and bound-one
draws, consumes command budget. Budget exhaustion is latched: catching the error
inside an evaluator does not permit the ability to publish. All draws occur in
the candidate; discarded previews, failed effects and competing stale candidates
leave live RNG unchanged. Providers must use this view for game randomness.

`encodeRandom` produces exactly 16 bytes: ASCII RNG1, uint32 little-endian version
1, then uint64 little-endian state. The fixed stream is implicit in version 1.
`decodeRandom` rejects bad length/magic/unknown versions; all 64-bit states are
valid. It returns a detached value, not a Runtime restore operation. No pointers,
padding or native endianness enter this encoding. R8 must embed it in its checked
snapshot envelope and validate that entire envelope before restoring. This small
RNG format provides no checksum or complete-game save support on its own.

## Bounds and evidence

Ability parameters: 64; targets: 64; status instances: 128 per character / 16384
globally; stacks: 32 per status; duration: 1..65535 matching hooks. Ability/status
records, parameters, costs, modifiers and tick effects count toward the existing
4096 catalogue-record cap. There are no unbounded parameter graphs.

Budget now includes effects (default/hard cap 256) and raw randomDraws (4096),
alongside operations/advancements. Each resource cost, item cost, proposed effect,
matching status phase visit (including skip/expiry), and per-stack resource tick
consumes one effect unit. An empty matching set consumes no effect unit. Exhaustion
rejects the whole command. Total result records across all result vectors cap at
512. Providers are trusted compiled code: these bounds cannot preempt a malicious
callback or prevent it allocating an oversized vector before returning it.

See [R4 validation](rpg-r4-validation.md). R5 receives explicit phase hooks, status
state, bounded effects and candidate RNG; it must implement encounter ownership,
initiative, turn epochs, refresh policy and outcome evaluation without a second
transaction or render-time progression.
