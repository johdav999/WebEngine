# RPG R2 validation

Scope: character/resource/progression definitions, checked modifiers and amounts,
command stamps, atomic preparation/publication, lifecycle invalidation and pure
unlock proposals. See [contracts](rpg-characters.md). R1 tests remain enabled.

## Reproduction and coverage

Use the [standalone build commands](rpg.md#build-without-engine-dependencies), with
RPG, RPG tests and module tests enabled. The same commands work on native Linux,
Windows/MSVC and pinned Emscripten/Node (prefix configure with `emcmake` for Wasm).
CTest now runs six cases: two consumers, configuration omission/failure matrix,
module regressions, R1 foundation tests and R2 character tests.

R2 tests cover invalid/duplicate definitions and units, resource underflow/over-cap,
positive/negative integer overflow, source replacement/removal and derived clamps,
sequential invariant checks, rejected compound commands, private runtime identity,
duplicate/stale/generation-invalid input, consumed/moved/competing candidates,
producer/character/source limits, maximum sequence numbers, multiple level
crossings, callback rejection/exceptions/reentry, advancement budgets and retained
sequence/identity state on failure. A fixed canonical state fixture covers two
different authored rulesets on every platform. Release-mode checks remain active.

Implementation validation results are added after execution. No rendered browser,
GPU, native-window or physics test is claimed: R2 adds only portable rules.
Inventory, combat, quest rewards, wire snapshots and rendered hosts remain later
milestones. RPGGame's engine pin is unchanged.

The required engine-skill installation and CatGame mirror were absent at their
configured and EJOHDAV-equivalent locations. Existing copies were not replaced or
created. Their synchronization handoff is the delivered API in rpg-characters.md.
