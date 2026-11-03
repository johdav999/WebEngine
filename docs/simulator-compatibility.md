# Simulator compatibility and scale

Optional engine/simulator/compatibility.hpp provides ContentStamp (version and
uint64 fingerprint), checkContent, ContentHash and checkpointHash. Hashing uses
FNV-1a 64, explicit little-endian 64-bit words and length-prefixed UTF-8. Raw
checkpointHash hashes only the supplied bytes. This is deterministic identification,
not authentication or a cryptographic integrity check. checkContent rejects zero
versions, different versions and mismatched fingerprints using existing Error codes.
Hosts own content definitions, version policy, bounded envelope parsing and explicit
migrations; the core snapshot codec remains version 1 with exact catalog checking.

Runtime::metrics() is an allocation-free O(1) copy of retained record/event counts
and Limits. Terminal records count until explicit forget operations; the query
neither scans/transitions jobs nor retains references. It is not an RSS/allocator
measurement. Hosts collect their own peaks and compare actual configured budgets.

Clock-only Runtime::advance scans jobs/customers for a due transition and directly
publishes only time when none exists. assign with no queued jobs returns zero
without cloning. Due completions/expiry still use candidate publication, deadline/ID
ties, transitionsPerAdvance and event backpressure. Real mutation cost remains a
bounded full-state copy; this release has no unbounded/high-concurrency promise.
Drain events and retire terminal ownership promptly. Neither fast path bypasses
negative/overflow time validation, and a full outbox still blocks due events.

Shared simulator_compatibility_cases.hpp and simulator_scale_cases.hpp test hash
encoding, version rejection, clock boundaries, saturated limits, unchanged state
on failure, rebased restore, conservation and retirement. The scale helper reports
wall microseconds independently from deterministic records/snapshot sizes; it
adds no engine clock or platform dependency. CafeGame runs longer 100 ms policy
sessions with repeated joint checkpoint restore, bounded record assertions and
native/Wasm checksum comparison. Cafe migrations, scenarios and JSON/CSV remain
consumer code. See the consumer docs/simulator-compatibility.md for report commands
and the dated simulator-release.md for actual evidence/remaining release gates.
