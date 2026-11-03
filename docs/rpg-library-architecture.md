# Optional RPG library: release 1 architecture

L7 adds equipment requirements, multi-slot displacement, selectable unlocks and
atomic respec through the ordinary transaction API. API 1.13.0; catalogue v2 /
runtime v4 extensions retain legacy formats when unused. Both sample hosts use
the same rules. See [contracts](rpg-builds.md) and [validation](rpg-l7-validation.md).

L6 is implemented locally: API 1.12.0 adds bounded weighted loot and atomic
receipt-backed reward bundles, including quest coordination and both shared host
adapters. Existing snapshot formats remain sufficient. Game content and pending
claim policy stay outside the engine. See [contracts](rpg-rewards.md) and
[validation](rpg-l6-validation.md); publication is pending approval.

Status: release architecture with R1 foundation, R2 characters/transactions,
R3 inventory/equipment, R4 abilities/statuses/RNG, R5 encounters, R6 tactical actions,
R7 quest bridge, R8 snapshots/restore, R9 shared sample hosts and R10 release handoff implemented.
L1 closes the [RPG acceptance gates](rpg-l1-validation.md); consumer adoption remains separate. Created
2026-10-25 against WebEngine `56080eca124c9a62e15b146799a8004bdad5a9b2`.
The delivered subset is documented in [rpg.md](rpg.md): optional RPG target,
identities, metadata bindings/catalogue and runtime ownership, plus
[R2 characters, modifiers, progression and transactions](rpg-characters.md).
[R3 inventory and equipment](rpg-inventory.md) extend the same transaction.
[R4 abilities, statuses and candidate randomness](rpg-abilities.md) are delivered.
[R5 encounters, initiative, allowances and cooldown phases](rpg-encounters.md) are delivered.
[R6 tactical actions, spatial quotes, reactions and presentation gates](rpg-tactics.md)
are delivered. [R7 coordinated quest rewards](rpg-quests.md) are implemented.
[R8 snapshots and combined restore](rpg-snapshots.md) are implemented.
[R9 shared sample hosts](rpg-hosts.md) are implemented; see [platform evidence](rpg-r9-validation.md).
Use [validation](rpg-validation.md) for actual evidence. Existing
[Modules](modules.md), [Quests](quests.md), [SDK composition](module-sdk.md),
[Application](application-lifecycle.md) and [host contracts](consumer-hosts.md)
remain authoritative for delivered functionality. Implement using the
[ten release prompts](rpg-library-release-1-prompts.md), in order.

## Purpose and precedent

Deliver an optional, portable C++20 RPG rules library usable by unrelated games.
The Shattered Sea is its first intended consumer: a single-player 3D game with
turn-based combat and a third-person camera. Neither that camera nor its setting
defines the engine API. WebEngine's puzzle-game extraction established the right
precedent: reusable module composition and quest evaluation in the engine,
application-specific puzzles, economy, rewards, saves and UI in the consumer.
There is no existing `WebEngine::Puzzle` target to copy.

Release 1 proves a small end-to-end loop: create characters, equip items, resolve
a turn-based encounter, grant a quest reward once, save and restore at a stable
decision boundary. Provide a neutral sample on browser and Windows plus a
standalone headless consumer. A second, differently configured ruleset must run
without engine edits. This is library delivery, not completion of RPGGame.

## Ownership boundary

| Area | Generic WebEngine RPG mechanism | Game-owned definitions and policy |
| --- | --- | --- |
| Characters | Stable entity IDs, bounded attributes/resources, modifiers, explicit resource operations | Classes, species, names, background choices, health/coherence meanings and values |
| Progression | Validated threshold tables and explicit advancement results | XP sources, curves, level caps, unlocks, respec and narrative restrictions |
| Inventory | Item definitions/instances, bounded stacks, atomic transfers, equipment slots and modifier sources | Item catalogue, slot names, weight limits, prices, loot, trading and crafting recipes |
| Abilities | Typed costs, target checks, compiled pure evaluators, validated effect proposals | Damage/hit formulae, spells, range rules, resurrection, friendly fire and balance |
| Statuses | Bounded stacking, explicit turn-boundary durations, source identity and removal | Poison/stun meanings, resistance rules, visual effects and localized text |
| Combat | Encounter state machine, explicit turn order, budgets, commands and bounded reactions | Initiative score calculation, AP allowance, movement allowance, victory/escape rules and AI choices |
| Spatial legality | Borrowed immutable query results stamped with world revision | Navigation queries, line of sight, terrain cost, reach, collision and destination residency |
| Quests | Optional adapter to existing quest facts/events and staged RPG rewards | Quest stories, objectives, reward mapping, journal/dialogue and moral decisions |
| Persistence | Canonical RPG catalogue/snapshot bytes, strict validation, identity/version checks | Outer save envelope, migration, checkpoints, autosave, storage and backups |
| Presentation | Read-only committed state and ordered result records | Third-person controls, animation, HUD, targeting feedback, sound, art and camera composition |
| World | Entity references independent of live scenes | Archipelagos, sailing, crews, routes, factions, reputation, Lattice, resonance and Unmaking |

Generic mechanisms do not imply universal rules. The library owns validation and
execution of explicitly selected rules; games supply content and bounded compiled
policy. No built-in strength/dexterity list, dice system, damage formula, alignment,
party size, skill tree, economy or lore is mandatory. A neutral sample may choose
them as example data. ElevenLabs/Tripo remain external authoring tools in RPGGame.

## Targets, dependencies and source layout

| Delivered target (except future game target) | Required dependencies | Responsibility |
| --- | --- | --- |
| `engine_rpg` / `WebEngine::RPG` | `WebEngine::Modules`, C++20 standard library | Catalogue, session state, transactions, characters, inventory, effects, turns, snapshots |
| `engine_rpg_quests` / `WebEngine::RPGQuests` | `WebEngine::RPG`, `WebEngine::Quests` | Typed projection/reward helpers and coordinated publication contract |
| `examples/rpg` consumers | RPG; optional RPGQuests; existing engine/host targets for rendered samples | Neutral content, scene/input/UI binding, storage and browser/Windows lifecycle |
| Future RPGGame rules target | RPG; optional RPGQuests | Shattered Sea definitions, custom policy and campaign/session composition |

Dependencies point from adapters and consumers toward kernels. Neither Modules,
Quests, `engine_core`, renderer nor host gains a dependency on RPG. RPG does not
depend on Quests unless the separate bridge is enabled. Do not add a scene bridge
library before two real consumers demonstrate reusable binding code; keep the
first adapter in the sample and RPGGame. Do not introduce a second ECS, scene
registry, scheduler, navigation system, physics engine, script VM or plugin loader.

Source layout (bridge is composed in rpg/CMakeLists.txt, not a separate subdirectory):

```text
rpg/CMakeLists.txt                 Standalone portable target and test options
src/rpg/quest_bridge.cpp          Optional bridge, composed by rpg/CMakeLists.txt
cmake/rpg.cmake                   Root opt-in integration and demo selection
include/engine/rpg/               Public typed contracts
  types.hpp bindings.hpp catalog.hpp state.hpp runtime.hpp snapshot.hpp
  quests.hpp                     Bridge-only header
src/rpg/                         Kernel implementations, no platform code
tests/rpg_*_tests.cpp             Focused native/Wasm contract tests
examples/rpg/                    External consumer, neutral policies and hosts
docs/rpg.md                      Delivered contract, created during implementation
docs/rpg-validation.md           Per-platform evidence, created during implementation
```

Use `webengine_add_module`, explicit `engine::rpg::Bindings` and a
`webengine.rpg` registration. Immutable catalogues retain module snapshots and
provider captures; factory-created sessions are independent. A game registration
such as `shatteredsea.rpg-rules` declares its dependency and owns its handlers.
RPG and quest bindings remain separate typed registries; the bridge explicitly
composes them instead of using a string service locator or unsafe registry cast.

Options default OFF: `WEBENGINE_BUILD_RPG`, `WEBENGINE_BUILD_RPG_QUESTS`,
`WEBENGINE_BUILD_RPG_TESTS`, `WEBENGINE_BUILD_RPG_DEMO`. Missing explicitly required
dependencies produce actionable CMake errors. Standalone `modules/` + `rpg/`
configuration must perform no renderer, physics, SDK or network discovery.
Disabling RPG leaves existing targets and tests unchanged. Publish source/CMake
composition with one toolchain, following the existing SDK; no binary ABI promise.

## State, identity and numeric contracts

Immutable Catalog owns definitions, stable namespaced content IDs, revision,
fingerprint, handler versions and selected policies. A Runtime owns mutable
characters, resources, inventory, progression, statuses, encounter, deterministic
random stream and command watermarks. Public views are read-only and expire on
publication/restore. Definitions never own live actors or runtime callbacks.

Durable entity/item IDs are monotonically allocated unsigned 64-bit values in a
saved session namespace, never reused; overflow rejects. Scene ActorId/ComponentId
values are transient bindings only. Commands additionally identify runtime owner,
generation, base revision, encounter/turn epoch, producer and sequence. Restoring
rebases transient generations, so old commands cannot target restored identities.

Use checked signed 64-bit integral quantities for resources, quantities, XP and
costs, with catalogue-declared units. If fractional values are needed, declare an
integer scale per definition. Reject overflow and inconsistent scales; do not
silently wrap. Core outcomes avoid floating-point-dependent rounding. Spatial
adapters supply explicitly quantized costs and IDs; this does not promise identical
physics or navigation results across platforms. Specify modifier order and tie
breaks; release 1 uses bounded additive modifiers followed by declared clamps.
Avoid arbitrary expression graphs or multiplication chains in the first release.

## Transaction and extension contract

Proposed flow is `prepare(command, context, budget)` then `commit(candidate)`.
R2-R7 deliver prepare/commit for character, inventory/equipment, abilities, statuses,
candidate RNG, encounter lifecycle, cooldowns, tactical actions, immutable spatial
quotes, reactions and coordinated quest rewards/receipts. R8 adds detached snapshot validation and restore; host integration
below is delivered by R9 with the recorded platform limitations.

1. Check command owner/generation/revision/turn and producer sequence.
2. Resolve definitions and targets against one immutable snapshot and world stamp.
3. Validate all costs, capacity, path/target legality and bounded work.
4. Evaluate pure compiled handlers against candidate state and a candidate random
   stream. Stage the complete effect chain, resource changes and result records.
5. Validate resulting invariants. Reject the entire command if any step fails.
6. At one owner-thread boundary, recheck base state and external world revision;
   publish by a no-allocation, no-callback, no-throw state swap.
7. Only after publication may the host consume results for animation, audio,
   journal or quest input. Result records cannot themselves grant rewards again.

Rejected commands, exceptions, budget exhaustion and stale candidates preserve
state, RNG, producer watermarks and unconsumed input. Successful publication
advances revision once and consumes the sequence once. Preview operates on a
disposable candidate; it never consumes the live random stream. No arbitrary
callback executes in commit. Reentry and concurrent mutation are rejected.

Handlers accept typed immutable context and return bounded effect proposals.
They cannot mutate scenes, external state, IO, providers or another runtime.
Registries validate duplicate handler IDs, parameter schemas and handler versions
before catalogue publication. Owned captures must outlive all retained snapshots;
destroy runtime services before catalogue/provider ownership. Work budgets cannot
preempt malicious compiled code: providers are trusted, reviewed application code.

## Turn-based encounters and movement

R5 commits inactive/awaiting-command/finished encounter states. R6 delivers tactical
spending and a separate committed presentation gate that also covers terminal
outcomes. R9 supplies host reservation/playback adapters in the optional sample. The following describes
the complete release architecture. Release 1 uses an explicit state machine: inactive, awaiting command, preparing
resolution, committed presentation pending, next turn, finished. Preparation is
synchronous and private; the host observes only committed states. The encounter
records a bounded roster, round, turn epoch, active participant, ordered initiative,
AP/resource balances, movement allowance, cooldowns and statuses. A new round
evaluates the selected initiative policy once; ties use stable entity ID. Mid-round
summons, simultaneous turns and delay-turn actions are deferred from release 1.

Commands cover begin encounter, move, use ability/item, end turn and an explicit
policy-approved exit. Every action checks eligibility and costs. Candidate outcome
includes deaths/incapacitation and skips ineligible future turns; zero eligible
participants terminates or returns a defined policy outcome without looping.
Exploration and combat have explicit ownership handoff; unrelated real-time AI
cannot issue damage during player decision waits.

Turn durations have named start/end-of-owner-turn or start/end-of-round phases,
not render frames. Document application-turn inclusion and expiry order. Refresh
AP and movement exactly once at the declared start phase. Cooldowns and status
durations do not advance while the player rotates the camera or pauses. Outside
combat, games explicitly remove/convert statuses or supply simulation-time actions;
no implicit conversion from seconds to turns is allowed.

Prepared reactions are bounded responses to declared action phases. Order by
phase, initiative and stable source ID; each reaction token fires at most once
per root action. No recursive unbounded trigger chains. Budget exhaustion rejects
the complete root action, including RNG and costs. Release 1 does not promise
an open-ended interrupt stack or arbitrary opportunity-attack rules.

Spatial context contains immutable entity positions/target eligibility and a
game-owned world revision, plus a validated path quote: start, destination,
quantized cost, occupancy/residency result and bounded route data. The host prepares
it through existing navigation/physics APIs. Revalidate immediately before commit;
world changes make the candidate stale. A quote is valid only for its runtime,
actor, turn and world revision; it is not a lasting authorization to move.

Committed tactical destination and cost are authoritative game state; animation
projects that result. In release 1, the host reserves the validated destination
and freezes conflicting simulation writers during the movement presentation.
Presentation completion/skip only releases the next-command gate; it never spends
AP or applies damage again. On interruption, snap/rebuild presentation from
committed state before accepting another action. If the world cannot reserve a
safe route/destination, reject before commit. Continuous physics-controlled combat
movement with partial refunds/replans is explicitly beyond release 1.

## Quest coordination and progression

R7 now implements the private-runtime coordinator and proves sequential no-throw
publication in [rpg-quests.md](rpg-quests.md). R8 adds combined restore through one detached kernel-aggregate swap; see
[rpg-snapshots.md](rpg-snapshots.md).

Reuse Quests for objectives, stages, facts, events and completion receipts. The
bridge projects explicit RPG facts and maps typed quest effects to staged RPG
operations. It supplies no stories, reward amounts or universal reputation system.
RPG progression applies validated XP deltas and threshold crossings atomically;
game-owned unlock proposals participate in the same transaction or reject it.

The owner first prepares quest progress and all corresponding RPG rewards, then
validates both candidates, game-owned effects, receipt ledger and base revisions.
Before any mutation, recheck both `canCommit` predicates. During publication there
must be no callbacks, allocations, queue posts, interleaving or external mutation.
Under that owner-thread invariant both preflighted commits must be guaranteed to
succeed; demonstrate this against the existing quest API in bridge tests. If the
implementation cannot guarantee the second publication, stage an independently
owned aggregate and publish it in a single swap instead. Never implement
best-effort sequential commits or a compensating reward rollback.

Use the quest receipt as the stable award identity and persist both quest receipts
and the RPG award ledger together. Repeated completion, retry, replay and restore
cannot duplicate rewards. Failure from full inventory, invalid effects or stale
state keeps both kernels and their queues unchanged. Non-RPG campaign effects
remain in the host's enclosing transaction, with the same no-throw publication
contract. Neither kernel provides disk/service transactions.

## Persistence and budgets

Separate module API version, catalogue content revision/fingerprint, handler
versions and snapshot wire version. Canonical bounded little-endian records with
explicit tags/lengths and integrity checksum must not encode raw struct memory.
Persist durable state, entity allocation counters, command producer watermarks,
random algorithm/version/state, turn order/epoch, pending status durations,
inventory ownership and award receipts. Exclude pointers, scene handles,
callbacks, transient candidates, uncommitted queues and presentation state.

Release 1 supports save at stable exploration or awaiting-command encounter
boundaries only. Presentation-pending saves reject clearly; a host may first
complete/skip projection to reach a valid boundary. Decode into a detached
candidate, validate all references/ranges/content versions, then publish. Restore
rebases generations, clears transient commands and emits no effects/rewards.
Unknown versions reject without destroying the active session or source bytes.
Games own migrations and combined campaign/quest/RPG save envelopes and storage.

Initial hard caps for implementation and stress tests (limits reject, never truncate):

| Resource | Release 1 cap |
| --- | --- |
| Definitions / key length | 4096 total / 128 ASCII bytes |
| Durable characters / encounter participants | 1024 / 64 |
| Attributes or resources per character | 64 each |
| Item instances / slots per inventory | 16384 total / 256 |
| Equipment slots / statuses per character | 32 / 128 |
| Effects per root action / reaction depth | 256 / 8 |
| Path quote points / command producers | 256 / 64 |
| Pending host commands / result records per action | 128 / 512 |
| Snapshot bytes / award receipts | 16 MiB / 16384 |

Use checked global totals as well as per-entity bounds. A caller budget may be
smaller than hard caps; exceeding either aborts preparation. No partial event
prefix commits inside one RPG command. Ledger capacity must fail before granting
an unrecordable award; games migrate/compact through explicit save policy.

## Hosts, RPGGame adoption and release evidence

The neutral sample owns Application/scene bindings, input contexts, third-person
camera configuration, GUI and audio. DesktopHost advances the Windows Application;
the existing browser host owns browser advancement. No RPG callback calls advance,
updates a scene a second time or starts its own physics clock. UI/AI enqueue typed
intent; host drains it at supported owner-thread boundaries. Dispose callbacks,
pending commands and scene bindings before the referenced scenes/session die.

Both hosts share rules/content and exercise a small encounter, inventory, quest
award, save/load, pause and scene replacement. Browser page suspension and Windows
focus/minimize cancel pending input, preserve turn state and reset host timing.
Camera inspection remains responsive during decision waits and does not advance
turns. Rendered samples prove integration, not the entire game's camera design.

RPGGame adopts only a published, validated engine commit. Its `GameSession`
composes RPG and Quests; `src/gameplay` keeps Lattice/coherence policy, named
abilities, balance and campaign effects. `src/world`, `src/narrative` and
`src/presentation` retain setting, story, sailing, HUD and third-person controls.
The generic library does not import RPGGame files. The current starter and its
cloud scene tests remain unchanged until a separate implementation/adoption task.

Release gates: standalone Linux and Windows native tests; pinned Emscripten/Node
tests with identical canonical state/RNG/result fixtures; option-omission and
independent-consumer builds; browser WebGPU/WebGL2 and Windows D3D12 rendered
acceptance; corruption/stale/reentry/overflow/limit tests; two distinct rulesets;
combined save and no-duplicate-reward tests. Report each platform's evidence
separately. Node execution does not prove rendering, and desktop browser testing
does not establish physical mobile acceptance. Defer multiplayer authority,
network replication, real-time combat, crafting/economy, dialogue graph editors,
procedural campaigns and a full RPG editor from release 1.

## Engine-skill synchronization handoff

R10 synchronizes available installed and game-local skill copies with the
release handoff, API 1.7.0 / wire 1 and the rendered acceptance contract. L1 updates
the copies for validated software CI and separate hardware evidence.
See [L1 evidence](rpg-l1-validation.md). Consumer pins must still be checked;
RPGGame adoption is tracked in its own repository; this library task does not
advance consumer pins.

## Library milestone L2 (implemented)

API 1.8.0 provides persistent parties with ordered active/reserve membership,
leader and control metadata, plus complete immutable encounter team assignments.
Party replacement/removal use the existing transaction lifecycle. Hosts retain
recruitment, AI, targeting and victory policy. Runtime snapshots retain v1
compatibility and add a v2 party/team extension. Both hosts share the updated
sample. See [contracts](rpg-parties.md) and [validation](rpg-l2-validation.md).

## Library milestone L5 (implemented)

API 1.11.0 provides bounded combat candidate evaluation through ordinary tactical
transactions, pure game utility scoring, stable ID ties, validated end-turn
fallback and stop-token cancellation through final publication. Plans retain
private command identity and revalidate before execution. The optional quest
coordinator and both sample hosts use the same implementation. No scheduler,
navigation or enemy-specific policy enters the kernel; wire formats are unchanged.
See [contracts](rpg-combat-ai.md) and [validation](rpg-l5-validation.md).

## Library milestone L4 (implemented)

API 1.10.0 adds optional vitality profiles over existing resources, typed
damage/healing/defeat/revival effects, turn/reaction eligibility and team defeat
queries. Game handlers retain formulas, resistance, revival eligibility/costs,
outcome and checkpoint decisions. Runtime snapshot v3 persists life state;
untracked v1/v2 saves retain legacy behavior. Shared Windows/browser examples
exercise the pipeline. See [contracts](rpg-vitality.md) and
[validation](rpg-l4-validation.md).

## Library milestone L3 (implemented)

API 1.9.0 adds bounded deterministic 3D single, sphere, 90-degree cone and line
selection, cast range, explicit self/LOS filtering and L2 team relations. Pure
previews share the selection implementation enforced by tactical preparation when
a query is attached to the spatial quote. Existing transactions apply complete
area effects/costs/RNG atomically and retain the presentation gate. Hosts own
physics/LOS facts and ability-specific shape/friendly-fire/cover/falloff policy.
Snapshots retain L2 wire compatibility. See [contracts](rpg-targeting.md) and
[validation](rpg-l3-validation.md).
