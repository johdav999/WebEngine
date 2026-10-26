# RPG trading, crafting and diagnostics (L8)

API 1.14.0 adds portable operations to `engine/rpg/runtime.hpp` and detached
diagnostic replay in `engine/rpg/replay.hpp`. Link the existing WebEngine::RPG
target. Both Windows and Wasm compile the same sources; no new dependency,
scheduler, filesystem, network service or renderer ownership is introduced.

## Exact payments and trading

`TransferResource { payer, payee, resource, amount }` transfers a positive integer
amount between distinct characters using an existing resource. Both definitions
must match the supplied unit and scale. Insufficient funds, recipient maximum,
invalid units or missing participants reject the complete command. Currency is
conserved rather than silently clamped. Zero-price trades omit payment. Characters
need not own the item inventories: games may use shared bags and treasury accounts.
The game authorizes participants, access, prices and currency choice.

`TradeItems { id, source, destination, item, quantity, payment }` buys or sells
a positive quantity from an explicit unequipped stock stack. Inventories must
differ. The game reverses item and payment directions for a sale. Expansion is
receipt, remove stock, optional exact payment, add purchased stack. Partial trades
retain the source ID for the remainder. The destination receives a fresh ItemId;
fully consumed stock IDs retire. Existing definitions have no per-instance
affixes/durability; identity-preserving transfers remain `TransferItem`.
There is no auto-merge, price multiplication, restocking or implicit infinite stock.

## Recipes

`CraftItems { id, ingredients, products, payments }` takes 1..32 explicit
RemoveItem inputs, 1..32 AddItem outputs and at most 16 exact payments. Input ItemIds
must be unique. All inputs are consumed before payments and outputs, so fully
consumed stacks free capacity. Partial consumption does not free a slot. Equipped
inputs, wrong owners, insufficient quantities, invalid outputs or any later
failure roll back the entire command. The library does not infer a recipe from
these values: recipes, skill/station access, output destinations, quantities,
balance, price arithmetic and recipe revisions are trusted game authoring policy.

## Transaction and save contract

Use command/prepare/commit for previews and publication. Economy operations,
including TransferResource, require no active or finished encounter (exit first).
This prevents turn-boundary effects from interrupting a conserved payment.
All expanded operations, including wrappers and receipts, share the 256-operation hard cap and caller's
Budget::operations. Payments expand into two ordinary validated resource changes.
Receipt, resources, inputs, outputs, allocation cursors, RNG and producer watermark
publish together. Stale, foreign and repeated commits preserve the live state.
Results expose created items and award records; inspect candidate State for exact
resource balances (ordinary AdjustResource does not emit ability-effect records).

Trade and craft require stable game-issued AwardIds, shared with the existing
bounded award ledger. Duplicate or ReplaySuppressed receipts reject. Reserve one
ID per intended order, retain it across retries, and issue a new ID for a new order.
Full bags and insufficient funds leave no receipt; fix the problem and retry the
same ID. The ledger has no eviction. Loading a post-transaction save prevents
duplicates; loading an older save restores that older world's transaction history.
This is local save consistency, not an online anti-cheat or external payment API.

Existing snapshot versions persist all consequences without new wire fields.
Exact catalogue/binding compatibility remains enforced. RPGQuests prepareRpg and
applyRpg support these operations while rejecting their reserved quest receipt
source. Quest reward mappers cannot supply trade/craft wrappers. Combined saves
retain independent market and quest receipts atomically.

## Detached diagnostic replay

`replay(catalog, session, initialSnapshot, steps, budget)` creates a private Runtime
and restores an exploration snapshot. Up to 64 ReplaySteps supply producer,
sequence and typed operations. Each command uses the ordinary transaction path.
The report contains committed step count, the first failed step's zero-based
index/ErrorCode/message, and the resulting prefix snapshot. Rejected commands
do not mutate the prefix. Initial snapshot errors and excessive step count throw.
Other non-RPG exceptions from application callbacks propagate to the caller.

Replay supports exploration/economy/rules diagnostics, with normal per-command
budgets. It rejects encounter starts and transient tactical/presentation commands;
combat replay requires host regeneration of world facts and private quotes.
There is no command serialization, automatic recording, clock or task runner.
The game owns journal capture, content/version selection and storage. Trusted
handlers must remain pure. Restore rebases generation/revision, so compare two
equally restored replay runs, not their bytes against an un-restored live session.

## Shared sample and release boundary

The shared Windows/browser controller adds a stock inventory, a separate example
coin resource and Buy tonic / Sell tonic / Craft shield controls outside combat.
The partner supplies the example merchant wallet. Buying costs three coins;
selling returns one; crafting consumes one tonic for a shield. These deliberately
small sample rules are not RPGGame content. A deliberate click creates a new order
using the next durable producer sequence. Pending asynchronous order capture is
left to consumer code. The changed sample catalogue intentionally rejects older
sample saves through exact compatibility checks.

See [L8 validation](rpg-l8-validation.md). RPGGame merchant/station content and
adoption remain game-r1 G12. Publication, consumer pin updates and fresh rendered
GPU acceptance are separate from local portable-library implementation.
