# Optional RPG quest bridge (R7)

API 1.6.0 adds `engine_rpg_quests` / `WebEngine::RPGQuests`, depending on the
existing `WebEngine::RPG` and `WebEngine::Quests` targets. It supplies coordinated
quest progress, RPG rewards and award receipts. It does not add a quest evaluator,
story rules, merged registry, event bus, renderer or storage implementation.

Enable `WEBENGINE_BUILD_RPG`, `WEBENGINE_BUILD_QUESTS` and
`WEBENGINE_BUILD_RPG_QUESTS`, and add modules/, quests/, then rpg/. The bridge flag
defaults OFF and errors clearly if either dependency is absent. The base RPG
target still depends only on Modules and configures/builds without Quests.
[examples/rpg/quest_rewards.hpp](../examples/rpg/quest_rewards.hpp) shows two
game-selected rewards; [validation](rpg-r7-validation.md) records platform evidence.

## Ownership and projection

`engine::rpg::quest_bridge::Coordinator` owns both runtimes, independently retained
RPG/quest catalogues and a compiled `Policy`. RPG `Bindings` and quest `Bindings`
remain distinct typed module registries. The coordinator exposes const states,
commands and guarded mutation methods, never mutable kernel references. All calls
use one owner thread; external concurrent access is unsupported.

The game supplies a stable namespaced `awardSource` ID identifying this quest
catalogue's award stream. Keep it stable across retries/content updates and in the
future combined save. Do not reuse it for unrelated award streams. There is no
automatic content migration or receipt translation.

`Policy::facts(const State&)` optionally projects a fact patch from committed RPG
state before quest preparation. Overlap with explicit batch facts rejects; unknown
keys/types and merged bounds are checked by the existing quest kernel. The
projector does not override facts silently or directly advance quests.

`ProjectedEvent` names a quest, type, source, sequence and payload. `event()` stamps
current quest generation/activation/stage using the existing quest runtime;
`post()` validates and atomically appends one event to its queue. `projectEvent`
accepts RPG results and a pure game callback returning zero or one ProjectedEvent.
It stamps/posts only after the callback returns. Callback/input failure leaves the
queue unchanged. More than one event requires explicit individual posts; no
multi-event atomic append is claimed. Replayed inputs must retain stable source
sequences for the existing quest duplicate guard. Result authenticity/meaning is
trusted host policy; the bridge does not automatically convert every RPG result.

Callbacks may borrow state/results only for their invocation. They must have no
external side effects, IO or retained references. Owning captures live with the
Policy and outlive both runtimes; catalogues retain their separate module providers.
Every coordinator mutation is guarded during callback preparation, including RPG
commands, event posts and command invalidation. Read-only views expire on publication.

## Reward mapping and ledger

`Policy::reward(EffectRequest, const State&)` maps an existing quest effect to a
`RewardProposal`. `accepted=false` rejects the whole batch. Typed operations are
`AddExperience`, `AddItem`, `AdjustResource` and `ApplyStatus`; targets, quantities,
resources and balance belong entirely to the game. Empty accepted proposals record
an intentionally handled effect with no RPG reward. Missing/unknown mappings
should be rejected by the game callback. Every mapper sees the same committed RPG
state; mapped operations execute in quest effect order within one RPG candidate.

The RPG kernel adds generic `AwardId {ContentId source, string receipt}` and
`AwardDisposition {Granted, ReplaySuppressed}` state. Quest receipts retain their
existing stable `quest-id/effect-key` spelling, independent of retry activation and
content revision. `RecordAward` validates nonempty printable ASCII receipt bytes
(1–257), a valid disposition, uniqueness and the 16384-entry ledger cap. It is an
ordinary core operation with no dependency on Quests. A duplicate rejects; there
is no silent receipt overwrite or ledger eviction. Results report recorded awards.

Within the coordinator, only coordinated quest publication may write this ledger.
Direct `RecordAward` operations and the reserved `webengine.quest-awards` producer
are rejected by RPG mutation wrappers. Each mapped effect's operations and receipt
are staged in the same RPG command. A full bag, unknown target/content, invalid
amount, provider failure or ledger/budget exhaustion rejects all rewards, including
earlier XP/unlocks in that command, and preserves quest progress and input.

`Batch::replay` keeps the existing quest meaning: rebuild earned state/receipts
without emitting rewards. New receipts are recorded as ReplaySuppressed and the
reward mapper is not called. Later retries/completions cannot grant those rewards.
Replay is for rebuilding an authoritative history, not deferring a live award;
there is no API to turn a suppressed receipt into a grant. Games must reconstruct
their associated historical economy separately under the future combined restore
contract. First normal completion records Granted. Earned retries emit no effects
and add no receipts. No duplicate reward is produced by preview or repeated ticks.

## Prepare and publication

Create `command()` from the coordinator, populate its quest batch commands/facts,
then call `prepare(command, rpgBudget, questBudget)`. The wrapper stamps runtime
owner and RPG generation/revision; quest generation/revision and activation checks
remain in the quest batch. Preparation:

1. Rejects foreign/stale RPG stamps and pending presentation; validates bounds.
2. Projects facts and prepares the existing quest transaction, without consuming
   queued events. Quest budget exhaustion aborts rather than publishing a prefix
   of evaluator work. Its selected event-prefix behavior remains unchanged.
3. Computes newly inserted quest receipts, validates effect/receipt correspondence
   and remaining ledger capacity, and invokes bounded reward mappings (or records
   replay suppression). All operations plus receipts share the 256-operation cap.
4. Prepares one RPG transaction for the mapped operations and copies result data
   before publication. Batches without new receipts need no RPG mutation but still
   stamp its state revision to reject intervening changes.

Inspect `Transaction::questState()`, `effects()` and `rewards()` before publishing.
The move-only candidate belongs to one coordinator; consumed/moved quest views
reject. `canCommit` is an observation, not a concurrency lock. `commit` repeats all
checks before writing either kernel. A stale RPG revision, changed quest state,
new queue post or competing commit returns false without consuming state or input.

### Why the two publications cannot diverge

The implementation uses the existing kernel commits, with the following proof
against [RPG runtime](../src/rpg/runtime.cpp) and
[quest runtime](../src/quests/runtime.cpp), rather than compensating rollback:

| Preflight predicate | Why it remains true until its publication |
| --- | --- |
| Coordinator owner, unconsumed candidate and sequence | Private candidate, private sequence and owner-thread call; no callback or yield occurs. |
| RPG owner/generation/revision/busy/consumed | Checked immediately before publication; private runtime is not exposed to any intervening mutation. Reward operations are nonspatial, so no external world stamp is involved. |
| Quest owner/base revision/queue revision/busy and queue-counter headroom | Checked before the first publication. RPG commit only swaps its own state pointer and marks its own candidate consumed; it cannot touch quest fields, busy flag, queue or counter. |

After preflight, coordinator commit sets its busy guard, publishes RPG first (if
needed), then quest state/input/trace, increments the bridge sequence and marks
the candidate consumed. RPG commit performs no allocation/callback and retains
old state in its transaction. Quest commit moves its prepared snapshot, pops the
validated event prefix, increments the checked queue counter and swaps its trace.
The retired snapshot/events contain ordinary standard-library value fields, not
provider callbacks or custom live resources. There is no user code, allocation,
queue post, thread handoff or observer between the two kernel publications.
Provider/module ownership remains on the runtimes/catalogues throughout.

Thus the second commit's entire predicate is unchanged by the first. Both kernels
are published before owner-thread code can observe the result. An unexpected false
from either already-preflighted kernel is treated as an internal invariant violation
(`std::terminate`), not a recoverable partial-success return. No compensation or
best-effort retry exists after the first publication. Kernel changes that introduce
callbacks/interleaving or cross-runtime mutation must revisit this proof before use.

## Other RPG work and spatial boundaries

`applyRpg` is a guarded immediate helper for nonspatial setup/admin/acknowledgement
operations. It stages/copies results before commit. Spatial `TacticalAction` rejects
through this shortcut: a cached world stamp cannot replace a final observation.

Use `prepareRpg`, `canCommitRpg` and `commitRpg` for tactical gameplay, passing a
freshly observed world stamp at publication exactly as in [R6](rpg-tactics.md).
`spatialQuote` delegates to the owned RPG runtime. These candidates cannot be
committed through another coordinator and never advance quest state. A successful
RPG commit invalidates older joint candidates through their captured RPG revision.
Pending presentation rejects joint quest preparation, even a batch with no award;
recover/acknowledge before processing queued quest input. Posting input remains
explicit and may queue it for later. `invalidateRpgCommands` preserves the R6
presentation recovery behavior.

R7 deliberately does not atomically combine a tactical move and a quest reward in
one command. First complete/recover the action, then project its committed results
and prepare coordinated quest rewards. This keeps external route reservations out
of the nonspatial reward publication proof.

## R8 persistence and remaining limits

API 1.7.0 adds `Coordinator::save/restore` and retains both runtimes in a private
owned aggregate. [R8 snapshots](rpg-snapshots.md) validate both candidates and
receipt agreement before a single aggregate swap. Restore clears queued input,
rebases generations and invalidates old owners without emitting rewards. Normal
R7 reward preparation/publication retains the proof above.

Migrations, ledger compaction, storage and non-RPG campaign effects remain consumer
policy. The R8 sample demonstrates an outer envelope and whole sample-state swap.
R9 supplies [browser/Windows presentation and storage integration](rpg-hosts.md).
