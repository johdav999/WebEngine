# L5: bounded combat AI

API 1.11.0 adds `engine/rpg/combat_ai.hpp` to portable `WebEngine::RPG`.
It supplies candidate legality evaluation, game-defined scoring, deterministic
selection and an explicitly validated end-turn fallback. It owns no scheduler,
scene, navigation, renderer or enemy personality. The optional RPGQuests
coordinator forwards the same API without exposing either mutable kernel.

## Planning and execution

The host enumerates up to 64 `CombatOption{id, TacticalAction}` values using
current ability/item choices, target selection and navigation facts. IDs must be
unique and stable. All options must name the requested active actor. Quotes come
from the usual runtime/coordinator `spatialQuote`; no AI-specific facts bypass
targeting, range/LOS facts, path occupancy, AP, resource/item costs or cooldowns.

```cpp
auto plan = runtime.planCombat(producer, sequence, actor, options,
    scorer, CombatAiBudget{}, currentWorld, stopSource.get_token());
if (plan.status() == CombatPlanStatus::Ready) {
    auto transaction = runtime.prepareCombat(plan, observedWorld);
    // Reserve movement and exclude conflicting writers, as for player actions.
    runtime.commit(std::move(transaction), freshlyObservedWorld);
}
```

`planCombat` sorts options by ContentId and evaluates a bounded prefix (default
32, hard cap 64). Each trial uses the ordinary complete transaction pipeline,
including reactions and encounter reconciliation, against the same live base.
Trials are discarded. Live state, RNG, producer watermarks and presentation are
unchanged. `evaluations()` lists rejected options with ErrorCode and accepted
options with their optional policy scores. A candidate rejected by the configured
action budget is unavailable under that budget; it is not proof of universal
illegality. `truncated()` explicitly marks unevaluated options.

The pure scorer receives borrowed before/after states, results and the option.
It returns a signed 64-bit utility, or nullopt to decline a legal option. The
highest score wins, with ascending ContentId as the tie-breaker. No tie-break RNG
is consumed. Identical state/RNG, quotes, options and pure scoring reproduce the
decision regardless of enumeration order. Preview exposes concrete simulated
rolls; games that should not use that knowledge must score from permitted facts.

When no evaluated option is accepted by the scorer (including a zero evaluation
budget), the planner previews a normal EndTurn. If valid, Ready plus `endTurn()`
identifies the fallback and `selected()` is empty. If phase/status/rule validation
rejects EndTurn, the result is NoAction with `fallbackRejection()`. The planner
does not loop, force a transition or bypass rules in a blocked encounter.

`prepareCombat` revalidates the selected private stamped command through the
same pipeline. It does not publish a cached simulation or rerun the scorer.
Runtime identity, revision/generation and turn changes invalidate retained plans.
World checks apply to both tactical selection and EndTurn fallback, including at
commit. Presentation acknowledgement and route reservations remain host duties.
Copying a plan does not permit repeated publication; the first commit advances
revision and producer sequence. Plans are transient and are not serialized.

Coordinator users call `planCombat`, `prepareCombat`, then existing `commitRpg`.
The coordinator guards both kernels during planning and reserves its quest-award
producer as before. AI cannot post quest events, issue arbitrary administrative
operations or publish rewards through this interface.

## Bounds, cancellation and callback lifetime

`CombatAiBudget` caps trial count and supplies existing per-trial action budgets.
One additional trial is allowed for EndTurn fallback. Maximum work is bounded by
64 trials plus fallback, each with the kernel's existing hard limits. Returned
evaluation records are at most 64. Callback implementation cost itself cannot be
preempted: scorers and all rules callbacks must remain pure, bounded and fast.
Do not retain borrowed references, mutate the supplied option storage or use
wall time/uncontrolled randomness in scoring. Exceptions propagate from scorers;
trial RPG validation errors become diagnostics except reentrancy errors.

An optional `std::stop_token` is checked between evaluations and after callbacks.
Cancelled plans have no executable command. Cancellation after planning rejects
preparation; cancellation after preparation makes canCommit/commit return false.
Calls remain synchronous on the owner thread. Only the stop request may originate
elsewhere; this is not permission for concurrent runtime access. Host lifecycle
changes can also invalidate commands. No worker, retained callback or hidden turn
driver is introduced.

## Integration and validation

Both Windows and browser samples expose an explicit **AI step** control. Their
shared controller proposes opponent strikes, scores applied damage and uses
EndTurn when no strike is available. It retains normal presentation recovery.
Automatic enemy scheduling, path generation, melee/ranged tactics and enemy
content belong to the game (RPGGame G7). This sample is not that integration.

API changed; L4 snapshot formats v1/v2/v3 and catalogue/combined envelopes did not.
See [L5 validation](rpg-l5-validation.md) for actual tested targets and limitations.
