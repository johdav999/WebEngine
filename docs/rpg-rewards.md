# RPG loot and atomic rewards (L6)

API 1.12.0 adds `RollLoot` and `GrantReward` to `engine/rpg/runtime.hpp`.
Windows and browser hosts share the portable implementation and existing targets.

## Authoring and execution

`RewardGrant` contains AddExperience, AddItem, AdjustResource or ApplyStatus.
Recipients, units, quantities, eligibility and balance are explicit game inputs.
`RollLoot { table, entries, rolls }` draws with replacement from an ordered table.
Each `LootEntry { id, weight, grants }` has a unique ContentId and positive uint32
weight. Empty grants represent an explicit no-drop outcome. Multiple grants in
one entry are all awarded; tables cannot nest. Rolls default to one.

Weights total at most UINT32_MAX. At most 64 entries, 32 rolls per table and
256 grant records across all entries are accepted. Every entry's structure,
weight and ID is validated before drawing. Selected grants undergo ordinary
target, content, quantity and provider validation when applied; unused entries
do not run gameplay callbacks. Game authoring tests should exercise every branch.

`GrantReward { id, grants, loot }` binds guaranteed grants and up to 16 tables
to one durable AwardId. A duplicate (including ReplaySuppressed) rejects with
DuplicateAward before any grant or draw. The game supplies a stable source and
receipt per entitlement, independent of retries and scene instances. An empty
bundle may explicitly consume an entitlement without granting anything.

Expansion preserves command order: receipt, guaranteed grants, then tables in
authored order and selected grants in entry order. Wrappers and every expanded
operation share Budget::operations (default/hard maximum 256). No recursive
tables or unbounded loops. All raw draws, including uniform rejection sampling,
share Budget::randomDraws and candidate PCG32 state. Results::loot reports table
ID, entry ID and zero-based roll index, counting toward the 512 result cap.

Identical initial state, ordered content and commands produce identical selection
and RNG across targets. Reordering entries changes cumulative weight intervals.
Tables are command values, not catalogue definitions: games version authored
reward content and pending-claim save policy themselves.

## Atomicity and retry policy

Use normal command / prepare / commit. Preview, rejection, stale commits and
dropped candidates publish no XP, item IDs, statuses, RNG, watermarks or receipts.
Successful commit publishes everything together. AddItem still creates one new
stack, with no automatic merging, splitting, mailbox, ground drop or loss.
Full inventory rejects the whole bundle with Capacity. Free space and issue a
fresh command with the same AwardId to retry. The game owns pending claims,
messages, alternate destinations, eligibility and world consequences.

The existing ledger blocks repeated grants across load and revisit, with no
automatic eviction. Restoring a pre-grant save restores the earlier entitlement
state. Retrying after an unrelated random action may select different loot: a
failed claim does not reserve its roll. Games requiring persistent preselected
drops must save the pending selection in their own state.

Raw RollLoot has no receipt and supports repeatable draws and quest mapping.
Use GrantReward for once-only encounter/chest claims. The engine trusts the game
to allocate entitlements; it cannot infer victory from a receipt string.

## Quests and persistence

RPGQuests RewardOperation accepts RollLoot. Existing coordination stages quest
progress, selected grants, RNG and quest receipts in one publication. Inventory,
provider or budget failures leave quest inputs available for retry. Replay
suppression runs no loot evaluation. Mappers cannot supply nested bundles or
arbitrary receipts.

Coordinator::prepareRpg / applyRpg accept GrantReward only for sources distinct
from the reserved quest award source. Direct RecordAward and the reserved
producer remain forbidden. Combined restore checks quest receipts against the
quest namespace; encounter receipts coexist independently. Existing v1/v2/v3
snapshots persist all consequences without migration or draws during restore.

## Shared example

Both hosts expose **Collect loot** after Finish / exit: five practice XP plus
a weighted one/two-tonic stack, keyed by encounter epoch. Only the latest exited
encounter is claimable under this sample policy. Full bags allow retry; repeated
collection and save/load reject duplicates. Parcel delivery uses a one-entry loot
table and retains two quest receipts. RPGGame adoption remains its separate G8.

See [validation](rpg-l6-validation.md) for checks and platform limits.
