# RPG L3: targeting and area effects

API 1.9.0 adds `engine/rpg/targeting.hpp` to `WebEngine::RPG`. The implementation
is portable C++20 and has no physics, renderer, clock, random or IO dependency.

## Query and preview

`selectTargets(state, spatialSnapshot, actor, query)` returns sorted unique entity
IDs without changing state, RNG, epochs or resource balances. The actor may be any
encounter participant, allowing inspection before their turn. Querying does not
authorize an action; normal active-actor and quote checks still apply on execution.

`TargetQuery::shape` selects:

- `SingleTarget`: one existing encounter participant.
- `SphereTargets`: a ground/world-space center and inclusive radius.
- `ConeTargets`: origin, integer direction and length; a 90-degree full angle
  (45-degree half angle) with an inclusive spherical length boundary.
- `LineTargets`: origin, integer direction, length and radius; an oriented cylinder
  with flat, inclusive end caps. Radius is half-width.

All shapes operate in three dimensions. Direction need not be normalized.
`range` is inclusive Euclidean distance from actor to selected entity or area
origin/center. Area extents are independent of cast range. An out-of-range aim
point rejects; a valid shape with no eligible targets returns an empty list.
Ability minimum/maximum target counts are validated during normal resolution.

`TargetTeam::Any`, `Allies` and `Opponents` use L2 encounter assignments. Unassigned
entities match only Any. `includeSelf` defaults false; enabling it still respects
the team and LOS filters. `requireLineOfSight` defaults true and filters by the
host's `SpatialSnapshot::legalTargets`. Set it false only for deliberate rules
such as an effect that passes through walls. The kernel does not raycast.

Every query requires complete, resident roster positions matching committed state,
unique entity facts and unique legal-target IDs within that roster. A nonzero
world stamp is mandatory. Input ordering does not affect output ordering.

## Exact geometry and limits

Coordinates must be in [-500000, 500000] on each axis; extents/ranges in
[0, 1000000]; direction components in [-1000, 1000] with a nonzero vector.
These are game-defined quantized units, not meters. Queries reject out-of-bound
values before arithmetic. Existing tactical positions outside this range remain
legal for legacy actions; L3 queries explicitly require bounded coordinates.

Squared distances, dot products and comparisons use exact 64-bit integer arithmetic
on Windows and Wasm. The largest squared projection is 9e18; cone doubling is at
most 18e18, below uint64's limit. There is no floating-point normalization or
platform-dependent boundary epsilon. Work is bounded by the 64-participant roster.
Arbitrary cone angles, chaining, polygon volumes and terrain surface projection
are not provided by this milestone.

## Atomic area execution

Attach the same query to `SpatialSnapshot::targeting`, issue the existing
`Runtime::spatialQuote`, and submit a `UseAbility` whose targets are the preview:

```cpp
snapshot.targeting = TargetQuery{
    SphereTargets{aimPoint, radius}, castRange, TargetTeam::Opponents};
auto selected = selectTargets(runtime.state(), snapshot, actor, *snapshot.targeting);
auto quote = runtime.spatialQuote(actor, snapshot);
TacticalAction action{actor, encounterEpoch, turnEpoch,
    UseAbility{actor, abilityId, selected, {}}, quote};
```

Preparation recomputes selection both before and after BeforeAction reactions.
The submitted list must match the complete selection, including cardinality;
ordering is normalized, duplicates reject. Movement quotes cannot carry targeting.
Runtime/generation/turn/world stamps retain their existing checks. A changed world
stamp rejects publication without consuming the candidate. Preview data is not an
authentication boundary: hosts own world revisions and trusted spatial facts.

The ability callback proposes effects for the selected targets. Existing R4/R6
resolution applies resource/item costs, AP, cooldown, effects and RNG atomically,
then creates one presentation gate. Costs apply once per action, not per target.
An invalid target, failed effect or exhausted effect budget publishes nothing.
Damage formulas, cover bonuses, falloff, friendly-fire policy and reaction rules
remain authored ruleset behavior. Ground-origin LOS and terrain legality must be
checked by host queries/authorization; entity visibility alone does not prove them.

Omitting `targeting` retains the existing R6 legal-target contract. A ruleset that
requires L3 geometry must require the intended query in its authorization callback;
do not expose arbitrary administrator operations or let player input choose weaker
filters. Query presence alone does not identify an ability's intended shape.

## Integration and persistence

The shared Windows/browser host uses an opponent-only radius-one area query for
its strike against the sparring partner. Its simple spatial facts retain the
sample's range policy; production games must supply actual obstacle/LOS queries.
The dedicated tests apply an area ability to multiple targets and verify preview,
boundaries, filters, rollback and stale-world rejection.

Queries and quotes are transient. API 1.9.0 changes no durable definitions or
snapshot fields: L2's v1/v2 snapshot compatibility remains unchanged. After load,
rebuild spatial facts and previews. See [L3 validation](rpg-l3-validation.md).
