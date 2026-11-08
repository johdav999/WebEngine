# Equipment requirements and progression choices (L7)

API 1.13.0 extends WebEngine::RPG with optional catalogue rules and ordinary
transaction commands. Include `engine/rpg/builds.hpp` for definitions and pure
queries, and `engine/rpg/runtime.hpp` for mutation. Both host targets share the
implementation. No scheduler, class system, skill tree UI or game balance is built in.

## Equipment

`CatalogDefinition::equipmentRules` associates an EquipmentRule with one item
definition. BuildRequirements checks minimum level, typed minimum **base**
attributes, and required unlock IDs. All conditions must hold. Equipment and
temporary modifiers cannot bootstrap eligibility. Unit/scale mismatches or
missing character attributes fail the requirement. Requirement values are game
content; the engine does not convert units or assign meanings to attributes.

`requiredSlots` adds up to 31 fixed slots to the anchor selected by EquipItem.
The item definition's existing `slots` remains the list of allowed anchors.
The occupied set is the sorted union of anchor and required slots, all of which
must exist in the inventory definition. `occupiedEquipmentSlots(catalog,item)`
returns this set (empty for bag items). No redundant occupancy map is persisted.

Equipping displaces every overlapping item completely into the same bag, checks
final stack capacity, and recomputes modifiers once. A two-slot item applies its
modifiers once. Full capacity rejects the whole command, retaining all prior
equipment and resources. Removing, transferring or splitting still requires
unequipping first. Unequipping releases all occupied slots.

After every ordinary operation the runtime validates build requirements before
encounter callbacks or subsequent commands can observe the candidate. Lowering
a required base attribute while gear is equipped rejects. There is no silent
auto-unequip, item deletion or overflow destination.

## Selectable progression

`CatalogDefinition::choiceRules` contains one optional ProgressionChoices per
progression definition. Each UnlockChoice uses an ID already in allowedUnlocks,
a positive integer cost, BuildRequirements and an optional exclusiveGroup ID.
At most one selected choice can belong to each named group. Required unlocks may
be automatic advancement unlocks or other choices in the same progression.
Self-dependencies, cycles, unknown unlock references, duplicate IDs and invalid
costs reject catalogue construction. Lists are normalized before publication.

Each level earns `pointsPerLevel` choice points (default 1, range 1..256).
Choice costs range 1..256; at most 256 choices exist per rule. The available
budget is level times pointsPerLevel minus selected costs, calculated with uint64.
There is no separately mutable point balance or refund counter.

`SetProgressionChoices { entity, choices }` replaces the **complete** selected set.
Supply all desired prerequisites together; the input order does not matter.
Unknown/duplicate IDs, overspending, conflicts and unmet prerequisites reject.
The command is allowed only outside encounters and after any presentation gate.
`remainingChoicePoints(catalog,state,entity)` queries the remaining budget;
Results::choices reports the normalized selection and remaining points.

Selected IDs appear in CharacterState::unlocks for existing gameplay handlers.
State::choices separately records which unlocks the player selected. Advancement
handlers cannot grant selectable IDs: this rejects with RejectedUnlock and rolls
back the whole XP award. Automatic unlocks survive selection changes and respec.
Choice unlocks do not automatically apply stat modifiers or grant abilities;
game-authored handlers and rules decide what an unlock enables.

An empty selection performs a full respec. Unequip gear that would lose its
prerequisites **before** changing the set, within one command if desired:

```cpp
auto command = runtime.command(producer, sequence, {
    UnequipItem{bag, greatblade},
    SetProgressionChoices{actor, {}}
});
auto candidate = runtime.prepare(command);
runtime.commit(std::move(candidate));
```

If bag capacity, requirements, costs or any later operation fail, neither operation
publishes. Respec fees and restrictions beyond the encounter gate are game policy;
stage ordinary resource/item costs in the same command. Repeated replacement does
not accumulate points. A selection may be changed without losing XP or level.

## Bounds, compatibility and ownership

Each requirement has at most 64 attributes, 256 unlocks and minimum level 0..256.
New rules and nested entries count toward the existing 4096 catalogue-record cap.
Selected state has at most one nonempty record per character, at most 256 IDs per
record; choice results count toward the existing total result cap. Empty selection
records are erased. Value queries borrow state during the call. The equipmentRule
and progressionChoices lookup pointers remain valid only while their immutable
catalogue is retained; runtime state references expire on publication.

Catalogues with rules use canonical catalogue wire version 2, retaining version 1
bytes when both new rule lists are empty. Runtime wire version 4 appends selected
sets after the existing party/team/vitality extensions. Without selected sets,
the writer retains the existing v1/v2/v3 output as appropriate. Readers accept
those older formats. No game-content migration is inferred: catalogue bytes,
including build rules, must match exactly.

Restore rejects unknown/missing choice owners, forged unlock ownership, overspent
points, missing prerequisites, incompatible multi-slot equipment and slot overlap.
The RPGQuests combined envelope needs no change and supports these commands via
prepareRpg/applyRpg. No separate quest evaluator or receipt path was added.

## Sample and integration

Both sample hosts add Choose mastery, Equip greatblade and Respec buttons, plus
remaining-point feedback. Parcel XP enables mastery. The greatblade requires
mastery, level one and base insight ten, occupying hand and offhand. The sample
respec stages its unequip before clearing choices and can reject if the bag is
full. Changing sample content intentionally makes older sample saves incompatible.
The shared controller tests exercise selection, equipment, respec and combined load.

Game R1 adoption remains the separately planned G11. See
[validation](rpg-l7-validation.md) for actual platform evidence.
