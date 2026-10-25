# RPG canonical snapshots and restore (R8)

L2 extends runtime snapshots to v2 for party/team data, while reading v1 and
preserving v1 output for states without that data. Catalogue and combined outer
envelopes remain v1. See [L2 compatibility](rpg-parties.md#persistence-and-compatibility).

API 1.7.0 adds `engine/rpg/snapshot.hpp` to `WebEngine::RPG`. No new target,
scene archive version, filesystem, renderer, migration service or automatic save
schedule is introduced. The same C++20 codec runs on Windows, Linux and Wasm.
See [validation](rpg-r8-validation.md) and the [sample envelope](../examples/rpg/save_game.hpp).

## Catalogue and snapshot API

`encodeCatalog(const Catalog&)` writes all normalized definitions plus a sorted
binding manifest. `decodeCatalog(bytes, bindings)` retains the supplied immutable
module snapshot, checks the exact manifest, reconstructs definitions through the
existing catalogue validators and rejects noncanonical encodings. Trusted pure
definition validators may run here; gameplay evaluators never run during restore.

The manifest identifies every registered handler by ID, kind and exact version;
ability schemas and encounter authorization/reaction capabilities are included.
Providers must increase their version when semantics change. Executable code and
captured provider state cannot be identified by serialization. Additional or
missing bindings reject deliberately; migrations select an explicit compatible
catalogue rather than silently substituting rules.

`Runtime::save()` returns canonical snapshot bytes. `decodeSnapshot(catalog, bytes)`
returns a detached `Snapshot` containing session namespace, state and the exact
canonical catalogue compatibility descriptor. `encodeSnapshot` and
`validateSnapshot` check that descriptor against the supplied catalogue, including
unchanged-revision content edits. `Runtime::restore(bytes)` and `restore(snapshot)`
validate before replacing live state. The runtime requires the same durable
session namespace; construct a new runtime for another namespace.

Save/load accepts exploration without an encounter and an encounter awaiting a
command. Complete, skip or recover presentation before saving; presentation-pending
saves throw `PresentationPending`. Exit a finished encounter before saving. A load
may replace a live pending presentation; the host must discard that old projection
and rebuild from the loaded committed positions. It must not acknowledge old work.

## Wire version 1

All envelopes are limited to **16 MiB**, including headers, nested catalogues and
checksum. Fields are written explicitly; no C++ object representation, native
`size_t`, pointer or floating-point representation is copied.

| Field | Encoding |
| --- | --- |
| Magic | Eight bytes: `WERPG`, CR, LF, `0x1a` |
| Wire version | Unsigned little-endian 64-bit, currently 1 |
| Tag | Unsigned LE64: 1 catalogue, 2 RPG snapshot, 3 coordinated RPG/quest save |
| Payload length | Unsigned LE64 byte count |
| Payload | Versioned positional schema, exact length |
| Checksum | LE64 FNV-1a over every preceding envelope byte |

FNV-1a starts at 14695981039346656037 and multiplies by 1099511628211 modulo
2^64 after XORing each byte. It detects accidental corruption, not deliberate
tampering. The complete catalogue descriptor is compared byte-for-byte, so its
compatibility check does not depend on a checksum collision assumption.

Integers (including enum tags and counts) use eight bytes; signed values use
two's-complement LE64. Narrow values are range-checked when read. Booleans and
optional-presence flags use one byte, strictly 0 or 1. Strings have an LE64 byte
length followed by ASCII bytes. Content keys use their existing 128-byte validator;
receipt strings allow at most 257 printable non-space ASCII bytes. Collections
have an LE64 entry count. Maps and sets require strictly increasing keys. Definition
vectors follow normalized catalogue order; initiative follows descending score
then ascending entity ID. Modifier vectors follow target kind then target ID.

The exact version-1 positional fields are listed by `Fields<T>` and the explicit
`Codec<State>` in [snapshot.cpp](../src/rpg/snapshot.cpp). A catalogue payload is
`CatalogDefinition`, then the binding manifest. An RPG payload contains a
length-delimited catalogue envelope, session ID and durable State fields. A bridge
payload contains award-source ID, publication sequence, length-delimited RPG
envelope and length-delimited existing quest snapshot. Unknown tags, versions,
trailing data, unordered keys, duplicate keys and invalid lengths reject.

Readers bound input before parsing, enforce a 1,048,576-node decoding budget,
cap each collection at 16,384 during parsing, then enforce all tighter domain
limits and global status/award caps. String lengths are bounded before allocation.
Writers also enforce the byte cap while appending. Input is never truncated.

## Durable state and validation

The snapshot persists revision, entity/item allocation cursors, characters,
attributes/resources, modifier sources, equipment, progression/unlocks, statuses,
inventories/items, PCG32 algorithm version and state, producer watermarks, phase/
encounter/turn/action epochs, encounter roster/order/cursor/active actor/allowance,
positions, cooldowns and award dispositions. Zero allocation cursors mean
exhaustion, never reset to one. Deleted/reserved identities remain below their
saved cursor; maximum identity can have been issued exactly once.

Transient runtime generation is excluded from RPG bytes. Presentation, world
quotes, paths, callbacks, commands, transactions, queued events, result records and
scene handles are excluded. Quest serialization retains its existing format;
restore rebases its generation/revision and clears its queue without evaluation.

Validation checks namespaces/cursors, distinct character/inventory identities,
single loadouts, item ownership/stack/capacity/slot compatibility, exact derived
equipment/status modifiers, modifier units/targets, recomputed clamped values,
XP/threshold levels and allowed unlocks, status/tick compatibility, RNG version,
cooldown owners, position references, encounter handler/version/order/turn
references, producer sequences and receipt syntax/dispositions. No ability,
advancement, encounter or reward evaluator runs to reconstruct these values.

## Publication and stale work

Standalone restore validates and allocates a replacement State first. It rebases
generation and revision above the maximum of live and supplied values, rejecting
counter overflow, then swaps the immutable state. Old commands, transactions and
spatial quotes fail their existing owner/generation/revision checks. Watermarks and
RNG are restored without consuming a draw or producing effects. No implicit input
queue exists in base RPG; the host clears its own queued input on successful load.

`quest_bridge::Coordinator::save/restore` joins both kernels. Restore decodes both
snapshots, verifies that this bridge's award ledger exactly matches quest receipts
and that its reserved producer watermark does not exceed its publication sequence,
then restores two **new private runtimes**. Any decode, validation or allocation
failure destroys only those candidates and leaves both live states and the live
quest queue untouched. Publication swaps one owned kernel aggregate and a prepared
owner token, then sets its saved sequence. Those operations cannot allocate, call
providers or throw; owner-thread observation cannot interleave. Old kernels are
retired after complete publication. Old bridge commands/events/candidates are
foreign to the new owners. Successful restore clears uncommitted quest input.

R7 normal-operation preflight/commit retains its guarantee; changing the storage
to an owned aggregate does not expose mutable runtimes. The policy and retained
catalogues outlive the aggregate. Calls remain owner-thread-only and non-reentrant.

The sample `SavedGame` adds a checkpoint counter and its own `RPGEXSAV` version-1
envelope (LE64 version/checkpoint/payload length, bridge payload, FNV-1a checksum).
It validates and restores a new sample bundle, then swaps the whole bundle once.
Failure preserves sample state as well as both kernels. Its namespace, checkpoint
meaning and storage/migration policy are sample choices, not engine defaults.

R9 implements [browser/Windows storage and projection rebuilding](rpg-hosts.md) above this boundary.
No disk atomicity, backup recovery, rendered UI or GPU acceptance is claimed here.

L6 (API 1.12.0) extends these historical contracts with bounded loot and atomic
reward bundles. See [current reward contracts](rpg-rewards.md).
