# L4: damage, defeat and revival

Implemented in portable `WebEngine::RPG`, API 1.10.0. Include
`engine/rpg/vitality.hpp` (also exposed by runtime.hpp). Windows and browser use
the same transaction implementation and sample controller.

## Ownership and usage

Outside an encounter, issue `ConfigureVitality{entity, healthResource, onZero}`.
The resource must already belong to the character; there is no second HP pool.
Configuration is permanent for that character. `onZero` is Downed (default) or
Dead. Existing characters without a profile retain legacy resource behavior.

Ability and reaction handlers may return `DamageEffect`, `HealEffect`,
`ReviveEffect`, or `DefeatEffect` in their effect proposals. Administrative
commands wrap these in `ApplyVitalEffect`; this is not player authorization.
Games own hit rolls, damage types, armor/resistance formulas, revival eligibility,
costs, targeting and checkpoint policy. Compute final magnitudes in the handler
using the transaction RNG. Use existing ability/tactical costs and authorization
for player revival actions. Do not expose administrative operations to players.

Damage/healing/revival magnitudes must be nonnegative and match the resource's
unit and scale. Damage saturates at zero; healing saturates at maximum. Zero HP
transitions Alive to the configured depletion state. Damage against an already
incapacitated character applies zero. Healing requires Alive: it never revives.
Revival requires Downed, a positive amount and positive resource maximum; Dead
additionally requires `allowDead=true`. Revival clamps health to maximum without
clearing inventory/statuses or other resources. Explicit defeat sets HP to zero
and selects Downed or Dead; it cannot turn Dead back into Downed.

`Results::vitality` reports event, requested/applied magnitude and before/after
life states. `Results::resources` reports the actual signed HP delta. Defeat and
configuration have no requested magnitude. Raw resource depletion reports a
Depleted event. These records are transient presentation data, not save state.
Costs, RNG, effects and life transitions publish together on successful commit;
invalid effects, exceeded budgets and stale publication leave live state intact.

## Turns, periodic effects and outcomes

`canAct(state, entity)` excludes Downed/Dead characters. Encounter turns,
reactions and exploration ability use enforce this in addition to game policy.
Revival restores eligibility for subsequent turn selection. `teamDefeated` is
true only for a nonempty assigned encounter team whose members cannot act.
Rulesets choose Victory/Defeat using this helper; the kernel does not choose
player teams, checkpoints or restart completed encounters after revival.

Raw resource writes retain strict bounds but reconcile zero HP into defeat.
Raising an incapacitated character's health through raw writes rejects the
transaction. Periodic effects on a tracked health resource use clamped damage/
healing while Alive and skip HP changes while incapacitated; duration still
advances. Periodic healing never resurrects. Other resources keep legacy rules.

## Persistence and compatibility

Runtime snapshot v3 adds vitality profiles and validates resource/life-state
consistency, depletion policy, references and active-actor eligibility. Saves
without profiles still emit v1, or v2 when L2 party/team data exists. All three
versions load; old saves do not invent health profiles. Catalogue and combined
save envelopes retain their existing versions. Older libraries reject v3.
Targeting queries remain transient; games choose filters suitable for revival.

The shared rendered sample opts both characters into vitality and converts
health effects to typed effects. The dedicated `rpg_vitality` suite covers
overkill, bounds, defeat, explicit resurrection, atomic failure, revival costs,
turn skipping, outcome policy, periodic behavior and save compatibility.
See [validation](rpg-l4-validation.md) for actual platform evidence.
