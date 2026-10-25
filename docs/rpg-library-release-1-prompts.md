# RPG library release 1: ten implementation prompts

These prompts implement [the RPG library architecture](rpg-library-architecture.md).
R1 is implemented as documented in [rpg.md](rpg.md) with evidence in
[rpg-validation.md](rpg-validation.md). R2 is delivered in
[character/transaction contracts](rpg-characters.md) with
[R2 evidence](rpg-r2-validation.md). R3 delivers [inventory and equipment](rpg-inventory.md),
with [R3 evidence](rpg-r3-validation.md). Prompts 4-10 remain the sequential backlog.
The original prompt text below is retained as the milestone acceptance scope.
Run each prompt as a separate task in WebEngine; prompts 2-10 depend on the
preceding delivered milestones. Keep working changes isolated and preserve other
work. Do not implement later milestones to conceal a missing prerequisite.

Each prompt incorporates the following common contract. Paste it with the selected
prompt, or instruct the task to read this entire document before starting.

## Common contract for every prompt

Read AGENTS.md, architecture.md, docs/rpg-library-architecture.md, docs/modules.md,
docs/module-sdk.md and delivered RPG contracts before coding. Inspect actual APIs;
all names in the architecture are proposed until implemented. Reuse existing
module, quest, scene, input, movement, camera, storage and host contracts.

Keep reusable code standard C++20 under engine::rpg and optional public CMake
targets. Keep RPGGame content, Lattice rules, class names, balance, factions,
third-person presentation and provider generation outside the library. Do not
create a new ECS, scheduler, renderer, navigation system or script VM. All new
functionality must support native Windows and browser Wasm in the same milestone.
Use Linux headless tests additionally for cloud tasks; they do not replace either.

Preserve explicit ownership, immutable definitions/provider lifetimes, pure
bounded preparation, checked integers, stale-command rejection and no-throw
publication. Rejection must preserve state, RNG, input and award receipts.
Add meaningful invariant/failure tests rather than tests that restate source.
Respect the architecture's hard limits and release exclusions.

Update architecture.md, README.md navigation, docs/rpg.md and validation evidence
as capabilities arrive. Update the existing installed engine-skill and CatGame
mirror required by AGENTS.md, preserving their guidance; locate moved copies and
report unavailable copies truthfully. Record exact commands, results, platform
limitations and API changes. No claim of rendered acceptance based only on Node
or headless success. Do not advance RPGGame's engine pin until the release/adoption
gate is explicitly reached. End with completed scope and the next prompt's inputs.

## 1. Optional module foundation and external consumer

```text
Implement RPG release milestone R1, following this document's common contract.
Create standalone rpg/ composition using webengine_add_module, engine_rpg and
WebEngine::RPG. Add default-off root/test/demo/bridge options from the architecture;
unsupported not-yet-implemented options must fail clearly rather than claim support.
The kernel depends only on Modules and C++20. Root engine builds must stay unchanged
when RPG is disabled; standalone module/RPG configuration must make no engine-core,
renderer, physics, SDK download or network requests.

Define typed content/entity IDs, errors, bounds, immutable Bindings publication,
webengine.rpg registration and session ownership scaffolding. Separate module API,
content and save versions. Do not invent combat or inventory functionality yet.
Create an independent examples/rpg consumer that retains its module snapshot and
constructs two isolated runtimes. Test duplicate IDs/handlers, dependency failures,
installer failure, retained snapshots after replacement and provider teardown.

Add native Windows, Linux and pinned Emscripten/Node build/test coverage plus RPG-off
and Modules-missing configurations. Deliver actual build commands and ownership
documentation for R2. Rendered hosts are introduced in R9, not claimed in R1.
```

## 2. Character state, resources, modifiers and progression

```text
Implement R2 on R1, following the common contract. Add immutable character/resource/
attribute and progression definitions, catalogue validation and independent mutable
character state. Use durable IDs with exhaustion rejection and versioned session
identity. Define integer units, checked arithmetic, additive modifier ordering and
clamps. Resource maxima/current values and equipment-sourced modifiers need clear
invariants without hard-coded health, mana, classes or character stats.

Introduce the shared command prepare/commit boundary now: owner/generation/revision,
producer sequence, no-throw publication, read-only views and rejected-command
atomicity. Later milestones must extend it rather than add competing transactions.
Add bounded XP/threshold advancement with game-supplied unlock proposals; award and
unlock changes succeed together or reject. No automatic experience on kills.

Test resource bounds, overflow, duplicate/stale/cross-runtime commands, modifier
removal, multiple threshold crossings, rejected unlocks and independent sessions.
Use two neutral definitions with different resource/advancement rules to demonstrate
reuse. Run native Windows/Linux and Wasm tests and publish canonical state fixtures.
```

## 3. Inventory, item instances and equipment transactions

```text
Implement R3 on R2 using the common contract and the shared transaction boundary.
Add immutable item definitions, durable item instances, bounded stack quantities,
inventory capacity and application-named equipment slots. Support add/remove,
split/merge, transfer between inventories and equip/unequip. Preserve total quantity
and unique ownership; equipped items cannot simultaneously occupy another owner.
Explicitly define merge compatibility and instance identity through split/merge.

Stage transfers, replacement equipment, displaced items and attribute modifiers
atomically. A full destination, invalid slot, incompatible stack, bad quantity or
failed modifier must preserve both inventories and character state. Limits, weight
or slot eligibility may be supplied by bounded immutable game policy; no built-in
economy, vendor, loot table or crafting system. Do not silently destroy overflow.

Test capacity boundaries, self-transfer, multi-owner conservation, stale handles,
integer overflow, equip rollback and repeated commands. Add invariant tests across
generated valid/invalid operation sequences. Demonstrate a neutral weapon/consumable
and a differently named tool/loadout ruleset. Run native and Wasm equivalence checks.
```

## 4. Abilities, effects, statuses and deterministic randomness

```text
Implement R4 on R3 following the common contract. Add typed ability definitions,
costs, target requirements, registered pure evaluators, bounded effect proposals
and explicit status stacking/duration policies. Use the existing typed module
registry for custom handlers; validate parameters, versions and missing handlers
before publishing a catalogue. Avoid a script language or hard-coded damage model.

Choose and specify a portable integer PRNG algorithm, seed expansion, bounded
sampling and canonical saved state/version. Preparation consumes candidate RNG;
rejection and previews must never consume live randomness. Stage costs, item use,
resource changes and all status consequences in one transaction. Define named
turn-phase expiry hooks for R5, including application-turn inclusion, stacking,
refresh and dispel behavior. Do not tick durations from rendering or wall time.

Test known RNG vectors across native/Wasm, failed multi-effect rollback, overflow,
unknown handler/schema, duplicate sources, stacking/expiry and provider lifetime.
Supply neutral damage/healing examples as sample policies, plus a non-damage
resource effect. Document numeric and ordering semantics for encounter integration.
```

## 5. Encounter lifecycle, initiative and turn budgets

```text
Implement R5 on R4 using the common contract. Add the release-1 turn-based encounter
state machine, bounded participant roster, rounds, active participant and turn
epochs. Initiative policy provides scores; build order once per round with stable
ID tie breaks. Add begin, end-turn and policy-approved finish/exit commands; do not
implement simultaneous turns, mid-round summons or delay-turn behavior.

Define exactly when AP/movement refresh, cooldown/status start/end phases execute,
incapacitated actors are skipped and victory/defeat/escape is evaluated. Turn state
must be independent of Application timing and presentation. Pause or an idle human
decision cannot advance counters. No eligible actor and cyclic status consequences
must terminate safely within budget. Game rules choose allowances and outcomes.

Test equal initiative, duplicate EndTurn, stale turn epochs, defeated active/future
actors, all-ineligible rosters, action/status failure and encounter entry/exit.
Run a scripted encounter with identical state/result traces on Windows, Linux and
Wasm. Expose the minimum committed state needed by R6 without scene pointers.
```

## 6. Tactical actions, spatial quotes and bounded reactions

```text
Implement R6 on R5 under the common contract. Connect ability/item actions and move
commands to turn ownership, resource/AP/movement costs and explicit result records.
Define immutable spatial context and path quotes carrying actor, turn, runtime and
world revision, start/destination, quantized cost, bounded route and residency/
occupancy results. The kernel consumes validated game-supplied queries; it must
not link navigation, physics or scene targets. Recheck the external revision at
the owner commit boundary without callbacks inside publication.

Implement bounded prepared reactions with phase/initiative/stable-ID ordering and
one trigger per reaction token per root action. Reject the whole root action on
budget exhaustion. Add the presentation-pending gate: committed movement/outcomes
apply once; completion/skip unlocks input only. Invalid, duplicate or stale
acknowledgements must not spend resources or apply damage. Define cancellation and
projection recovery from committed state; do not roll back live physics.

Test blocked/missing paths, stale world quotes, occupied destinations, duplicate
actions/acks, nested reaction limits and full rollback including RNG. Document the
host route reservation and frozen-writer requirements that R9 will implement.
```

## 7. Optional quest bridge and exactly-once rewards

```text
Implement R7 on R6 following the common contract. Add the separately optional
engine_rpg_quests / WebEngine::RPGQuests target, depending on RPG and existing Quests.
The base RPG kernel must still configure without Quests. Keep the typed RPG and
quest registries distinct. Supply explicit fact/event projection and game-selected
effect-to-RPG-operation mappings; do not introduce another quest evaluator.

Build a coordinated owner-thread prepare/preflight/publish operation for quest
progress, RPG rewards and award receipts. Prove from the actual APIs that after all
canCommit checks no callback, allocation, interleaving or queue change can make
the second commit fail. If that guarantee cannot be met, prepare an independently
owned aggregate and publish once. Never commit a quest before reward validation
or rely on compensating rollback. Preserve queues on rejection.

Test full inventory, invalid reward mapping, stale RPG/quest revisions, callback
failure, repeated completion/retry, replay and no-duplicate awards. Demonstrate
two game-selected rewards and a rejected compound award. Preserve existing quest
tests and add RPG/bridge-off configuration tests on native and Wasm.
```

## 8. Canonical snapshots and safe restore

```text
Implement R8 on R7 following the common contract. Add bounded canonical catalogue
and RPG snapshot formats with explicit magic/version/tags/lengths/checksum, stable
ordering and content/handler compatibility checks. Persist durable IDs/counters,
characters, inventory/equipment, progression, statuses, RNG, command watermarks,
encounter order/turn state and award ledger. Never serialize pointers, handles,
callbacks, pending commands, transactions or presentation resources.

Allow saves at stable exploration or awaiting-command boundaries; reject a
presentation-pending save clearly. Restore validates a detached candidate before
publication, rebases transient generations and clears uncommitted input without
effects or rewards. Build an example outer envelope joining RPG, quest snapshot
and sample-owned state; both kernels must restore together. Storage and migrations
remain consumer policy. Unknown content/version data preserves the live state.

Test round trips, cross-platform byte equality, truncated/corrupt/oversized input,
invalid ownership/equipment/turn references, exhausted IDs, stale pre-load commands,
RNG continuation and quest reward receipts after reload. Add bounded malformed-input
coverage. No scene archive version change should be necessary.
```

## 9. Shared 3D browser and Windows sample integration

```text
Implement R9 on R8 under the common contract. Build a small neutral examples/rpg
vertical slice with shared rules/session/controller code, a 3D scene, inspectable
third-person camera, inventory, turn actions, one quest reward and save/load. Use
the public browser and DesktopHost targets and existing camera/input/GUI/navigation/
storage mechanisms. Keep assets primitive and local; no paid generation or RPGGame
lore. Both hosts must be delivered in this milestone.

Map durable entities to scene-scoped handles. Route GUI-consumed input correctly,
cancel stale input on scene/load/focus changes, reserve combat routes/destinations,
freeze conflicting simulation writers and rebuild interrupted presentation from
committed state. The existing Application owns ticks; camera inspection, pause,
suspension and animation completion cannot advance turns or duplicate effects.
Unbind views/callbacks before scene/session destruction. Storage uses a host-owned
combined envelope, including failed-write recovery.

Validate scripted gameplay plus actual pixels/input on browser WebGPU and WebGL2
and native Windows D3D12. Exercise pause, scene travel, interrupted presentation,
save/reload, rejection and shutdown. Report GPU/browser/device coverage separately
from headless tests, including any genuinely unavailable acceptance evidence.
```

## 10. Release gates, second consumer and RPGGame adoption plan

```text
Implement R10 on R9 using the common contract. Audit every release-1 architecture
promise against code and evidence; fix gaps before claiming release. Finish the
independent standalone consumer, including a second materially different ruleset
that changes resources, equipment and ability/initiative policy without engine
edits. Prove no renderer/physics/download dependencies in standalone configuration.

Complete CI for Linux/Windows native, pinned Emscripten/Node, RPG-off, bridge-off,
missing-dependency errors, existing Modules/Quests regressions and canonical fixtures.
Run the affected rendered browser/Windows sample checks and record exact commands,
commits, formats, supported APIs, bounds and known limitations. Add copyable consumer
CMake and module-registration examples, migration/version policy and a release
checklist. Keep every unmet gate explicit rather than labeling partial support done.

Prepare the RPGGame adoption mapping: public targets, GameSession composition,
game-owned Lattice/balance/content, existing third-person presentation and Windows/
cloud tests. Publish the validated engine commit through the repository's normal
workflow. Do not modify or advance a consumer submodule unless that adoption is
explicitly authorized in the task. Update architecture, contract, evidence and
available engine-skill copies. Report the precise release-ready scope and deferred
features; this task does not implement the full Shattered Sea game.
```
