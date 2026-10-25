# RPG inventory and equipment (R3)

R3 extends the R2 transaction in `WebEngine::RPG`, API 1.2.0. It is portable
C++20 with no new dependencies, callback registry, scene bindings or save format.
Game content supplies `ItemDefinition` and `InventoryDefinition` in the immutable
catalogue. See [example definitions](../examples/rpg/inventory.hpp) for a blade /
tonic / hand loadout and a probe / cell / socket loadout with different units.

## Ownership and limits

`CreateInventory` returns an EntityId in `Results::inventories`, using the same
monotonic session allocator as characters. Its optional character must exist and
may own only one loadout. Unattached inventories act as containers and cannot equip.
There is no deletion/rebinding of inventories in R3. Inventory IDs cannot resolve
as characters. Entity and item IDs remain independent from scene handles.

`ItemId` is a separate typed session/value pair. `State::nextItem` is monotonic,
starts at one and uses the existing checked EntityIds allocator internally;
UINT64_MAX is issued once, then zero means exhausted. Retired IDs never return.
One global item map is the sole ownership record: each ItemState has exactly one
inventory and either no slot (in its bag) or one equipment slot. Equipped items
have quantity one. No duplicate inventory/equipment lists can drift out of sync.
There are no mutable per-instance affixes/durability fields in this milestone.

Hard limits: 1024 inventories, 16384 live item instances, 256 bag stacks per
inventory, 32 named equipment slots per inventory, and 256 combined ordinary and
equipment modifiers per character. Catalogue records, nested slot declarations
and item modifiers count toward the existing 4096-record cap. Slot names are
validated ContentIds. Duplicate definitions/slots/modifier targets reject.

`InventoryDefinition::capacity` is an immutable game-selected bag stack limit
between zero and 256. Equipped instances do not consume bag capacity. Item stack
maxima are positive int64; quantities count whole items, without fractional units.
Item and inventory slot lists jointly determine eligibility. These bounded data
policies need no evaluator callback. Weight limits, crafting, vendors, prices,
loot tables and arbitrary eligibility callbacks are not supplied.

## Operations and identity

All operations use Runtime::command / prepare / commit with R2 owner, generation,
revision and producer-sequence checks. A command copies candidate state, applies
operations in order, and publishes once. Any error discards all inventory,
equipment, character, allocation cursor, result and watermark changes. Each
operation must leave valid state before the next operation; no temporary overflow
is allowed. Newly allocated IDs are returned in candidate results, and are not
symbolic references usable by later operations in the same command.

| Operation | Semantics |
| --- | --- |
| AddItem | Creates one new stack; requires 1..maximumStack and free capacity. No automatic merging or overflow splitting. Returns ID in Results::items. |
| RemoveItem | Removes a positive quantity from an unequipped stack; exact depletion retires its ID. This is an explicit sink, not automatic loss. |
| SplitItem | Requires 0 < quantity < source quantity. Original ID retains the remainder; fresh ID owns the split quantity in the same bag. Requires free capacity. |
| MergeItems | Both stacks must be distinct, unequipped, in the named inventory and have identical definition IDs. Checked sum must fit the definition maximum. Destination ID survives, source ID retires. No partial merge. |
| TransferItem | Moves an entire unequipped stack between distinct existing inventories, preserving its ID and quantity. Split first for partial transfer. Self-transfer rejects. |
| EquipItem | Requires a single unequipped item in a character loadout and an eligible slot. Displaced equipment goes into the same bag. Final capacity and modifiers are checked together. |
| UnequipItem | Moves an equipped item into its bag, requiring capacity; removes its equipment modifier contribution. |

Remove, split, merge and transfer reject equipped items: explicitly unequip first.
Supplying the wrong owner or a retired/foreign ID rejects. Merging compatibility
is exact definition equality within the retained catalogue; future per-instance
properties will require an explicit compatibility extension.

## Equipment modifiers

`CharacterState::equipmentModifiers` is derived from equipped ItemIds and is
read-only through Runtime/Transaction. SetModifiers/RemoveModifiers cannot forge
or remove these contributions. Catalogue validation checks modifier kind, scale
and duplicate targets; equipping checks the actual character's targets and units.
An incompatible character or integer overflow rejects the whole transaction.

Recomputation adds ordinary sources in ContentId order, then equipment sources in
ItemId order, with checked int64 addition at each step and the R2 final clamps.
Replacement recalculates once after both items reach their final locations. It
does not temporarily remove a maximum bonus and lose current resource. Explicit
unequip followed by equip remains two ordered operations: shrinking maxima clamps
current, and increasing them does not refill. Aggregate modifier limits apply
both when equipping and when changing ordinary sources.

## Validation and next milestone

[R3 evidence](rpg-r3-validation.md) covers bounds, conservation, full destinations,
identity retirement, stale/repeated commands, arithmetic failure, modifier rollback,
replacement at full capacity and deterministic generated valid/invalid sequences.
Two fixed canonical state fixtures run unchanged on native Windows, Linux and
Wasm. Fixture text is test-only, not the future R8 wire format.

R4 now extends Operation and the same candidate state for abilities, item use,
statuses and candidate RNG; see [R4 contracts](rpg-abilities.md). It preserves these ownership and quantity rules;
costs/effects must publish with inventory changes in the existing transaction.
