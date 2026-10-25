# RPG L2: parties and combat teams

API 1.8.0 adds renderer-independent party state and explicit encounter teams to
`WebEngine::RPG`. Include `engine/rpg/runtime.hpp`; no additional module is required.
The browser and Windows samples share the same implementation.

## Persistent parties

`SetParty{partyId, PartyState}` creates or replaces an entire roster through the
normal prepare/commit transaction. Use replacement for recruitment, dismissal,
reordering, active/reserve changes, leader changes and control changes.
`RemoveParty{partyId}` removes a known party without deleting its characters.
Both produce `Results::parties` entries. Failed commands leave all live state,
producer watermarks and random state unchanged; prepared transactions become
stale after another commit or restore.

Party IDs are namespaced `ContentId`s. Each roster preserves authored order and
contains existing characters, each belonging to at most one party globally.
Capacity is 1–64; active limit is 1–capacity. There may be at most 64 parties.
Empty parties are legal. An optional leader must be an active member. Reserve
members remain ordinary characters with their inventory and progression intact.
To transfer membership, remove it from the old roster before adding it to the new
roster in the same atomic command. All intermediate operations must be valid.

`partyOf(state, entity)` returns optional membership; `activePartyMembers(state,
partyId)` returns active members in authored order and rejects an unknown party.
`ControlMode` records Player, AI or Scripted control. It neither authenticates
commands nor runs AI. Hosts remain responsible for deciding who may submit input.

Party changes require no current encounter, including a finished encounter:
commit `ExitEncounter` first. This keeps participation stable during combat.
The existing presentation gate also applies.

## Encounter teams

`BeginEncounter::teams` maps each participant to `CombatMember{teamId, control}`.
When nonempty, it must cover exactly the encounter roster, use valid control modes,
and name at most 16 teams. The runtime validates assignments before invoking
encounter callbacks. Team IDs need no catalogue registration.

`teamRelation(state, a, b)` returns Ally for matching assigned teams, Opponent for
different assigned teams, and Unassigned if either is absent. `controlledBy`
queries encounter control metadata. Assignments persist for the entire encounter
and disappear on exit. Empty assignments retain the original unassigned behavior.

Party membership and encounter sides are intentionally independent: the game
explicitly constructs the encounter roster and team assignments, allowing
temporary allies, sparring and player-controlled opponents. Friendly fire,
target filtering, victory conditions, recruitment requirements, companion quests,
formation, shared inventory and AI planning remain game/ruleset policy. Callbacks
can query team relations when implementing those rules.

## Persistence and compatibility

Runtime snapshots with parties or team assignments use wire version 2. The v1
state payload is followed by the party map and encounter team map. Collection
ordering, duplicate rejection, checksums, byte bounds and semantic validation
still apply. Orphan team data and an empty v2 extension are rejected.
Snapshots without L2 data continue emitting byte-identical v1 layout. Loading v1
supplies empty parties and teams. Catalogue and combined-save outer envelopes
remain v1; combined saves embed the versioned runtime snapshot. Older libraries
reject v2 snapshots rather than silently losing data. API version and wire version
remain independent. Existing operation indices and error codes are preserved.

## Sample and validation

The shared host controller creates a two-member party with one reserve and starts
a sparring encounter with two opposing player-controlled teams. Host tests verify
combined save/load retains both. `rpg_parties` exercises membership constraints,
atomic rejection, stale transactions, control queries, team completeness/limits,
encounter mutation restrictions, v1 loading and v2 round trips. Existing snapshot
golden fixtures remain compatibility checks. See [L2 validation](rpg-l2-validation.md)
for executed checks and platform evidence.
