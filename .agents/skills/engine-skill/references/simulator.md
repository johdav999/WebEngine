# Simulator integration

Use this guidance for WebEngine::Simulator in small simulator games. Verify the selected source before using APIs: CafeGame's renderer pin does not contain the simulator; CafeGame supplies vendor/webengine-simulator separately. Check its SHA256SUMS with node tools/verify-simulator.mjs from the CafeGame root.

The module is optional, C++20 and standard-library-only. Enable WEBENGINE_BUILD_SIMULATOR and add the engine simulator/ directory; link WebEngine::Simulator and include engine/simulator.hpp. It owns inventories, reservations, recipes, jobs, stations, workers, logical customers/orders and receipts. Game policy owns prices, wallet, arrivals and progression; engine navigation/rendering remain host adapters.

## Host invariants

- Mutate Runtime on one owner thread. Check Result/Status and use describe(error). Query records are copies.
- assign is explicit; it reserves inputs, output capacity, worker and station slot atomically. arrive uses the job handle and assignment token; missing actors never imply arrival. assign(true) is an explicit immediate-arrival adapter.
- Feed integer simulation milliseconds; retain fractional conversion and advance.remaining. Successful blocked progress and failed candidate publication have different retry semantics. Pause/hidden-tab suspension must not accumulate offline time.
- Drain events before retrying outbox failures or forgetting records needed by effects. Apply pending receipts idempotently using stable numeric receipt values, then acknowledge. Do not use epoch-bearing handles as durable receipt keys.
- Completed output remains owned by the order until delivery or abandonment. Deliver strictly before patience expiry. Cancellation after start does not refund consumed inputs.

## Save/restore

Runtime::save returns bounded canonical version-1 bytes. restore validates a candidate against the exact definition catalog before publication, rebases handle epochs and emits no historical events. Rebind stable values with epoch(), clear queued commands and rebuild travel requests. receiptSequence() exposes the receipt high-water mark.

Persist wallet, applied receipt bookkeeping, fractional time and core bytes together in a game envelope. Validate the entire envelope before replacing the live game. Core saves alone do not guarantee wallet consistency. Only schema 1 is supported; storage errors, migration and authentication remain host policy.

## Source and evidence

Relative to the selected WebEngine root, read docs/simulator-guide.md, docs/simulator-production.md, docs/simulator-service.md and docs/simulator-persistence.md for delivered contracts. Read docs/simulator-release.md for dated evidence and outstanding release gates. examples/simulator/headless.cpp is the basic consumer; examples/simulator/parity.cpp and scripts/simulator-parity.py exercise contention, interruption and exact state/event comparison.

The current v0.1 source is an unreleased candidate. Prior local Wasm/browser checks do not establish native Windows/Linux parity or hardware WebGPU/mobile performance. Verify CI for the exact revision before claiming release acceptance. CafeGame-specific storage/host details are in its docs/simulator-saves.md and docs/simulator-release.md.

Simulator supply stage 10: Runtime::transfer atomically moves bundles without consuming input holds or incoming capacity. Include engine/simulator/supply.hpp for SupplyBoard, explicit simulation-time delivery polling, output-capacity-backed supplier orders, cancellation/retirement, typed restore and threshold/target reordering. Runtime must outlive the board; do not externally finish its reservations. Restore core first, rebase typed IDs, then restore into a fresh board. See docs/simulator-supply.md. CafeGame envelope schema 4 persists storeroom, automation and pending deliveries; core wire format remains 1. Supplier costs, vehicles and hauling are game policy/future work.

Simulator economy stage 11: include engine/simulator/economy.hpp for optional Ledger. Signed integer postings require contiguous per-stream source sequences; compacted history retains totals and duplicate watermarks. Prepare all allocations before a related inventory/service mutation, preserve exclusive ledger ownership, then use allocation-free commit. Quotes reject stale revisions, foreign/consumed candidates and lifetime reuse. Hosts own prices, currencies, wages and joint persistence. See docs/simulator-economy.md. CafeGame schema 5 adds paid supplies/refunds, on-shift wages, arrears and finances; core wire schema remains 1.

Simulator quality stage 12: include engine/simulator/quality.hpp for Freshness (explicit-time integer shelf-life samples) and Condition (monotonic use IDs, saturating wear/dirt and independent clean/repair restoration). Runtime::remove atomically disposes unreserved bundles. The host owns expiry effects, equipment thresholds, task/ledger coordination and joint persistence. See docs/simulator-quality.md. CafeGame schema 6 adds coffee expiry/waste and staffed machine cleaning/paid repairs; core wire schema remains 1.


Simulator layout stage 13: include engine/simulator/layout.hpp for bounded GridLayout, finite snapping, quarter-turn footprints/access, overlap/clearance and connectivity validation with candidate publication. It supplies no actor path follower. OccupancyPool::setEnabled accepts a per-point 0/1 mask; disabling claimed points fails, enabling promotes waiters, and compact queues stay fully enabled. Hosts own definitions, saved masks, prices and cross-system coordination. CafeGame schema 7 composes layout with Ledger and existing engine navigation; edit only after closing/draining. See docs/simulator-layout.md for limits and current validation.


Simulator demand stage 14: include engine/simulator/demand.hpp for deterministic nonzero xorshift32 DemandRandom, bounded ArrivalTimer, weighted satisfaction and linear patienceScore. Keep random draws in detached candidates until admission succeeds; supply explicit simulation time and hold traffic while closed/disabled. These values own no customer/profile/payment policy. CafeGame schema 8 adds saved foot traffic, profiles, countdown, RNG and review aggregates tied to core deadlines/outcomes and paid service. Legacy saves get manual traffic and no retroactive reviews. See docs/simulator-demand.md for exact bounds, defaults and evidence.


Simulator authoring stage 15: include engine/simulator/diagnostics.hpp for detached
validateDefinitions(resources, processes, limits) with field paths and read-only
inspectJob(Runtime, JobId) state/blocked-error explanations. These reuse core rules,
never advance/assign/drain events and own no host references, IO or scenes. Game
hosts own typed definitions/scenarios, action availability and balancing policy.
CafeGame has four atomic-reset presets, browser inspection/JSON export and the
native/Wasm cafe_balance reference-v1-zero-travel-100ms runner. Core schema 1 and
Cafe envelope 8 remain unchanged; hot reload, content migrations and scale are
not supplied. See docs/simulator-authoring.md and preserve native/browser parity.


Simulator compatibility stage 16: read engine-root docs/simulator-compatibility.md.
Include engine/simulator/compatibility.hpp for ContentHash/ContentStamp/checkContent
and checkpointHash; fingerprints are not authentication. Runtime::metrics is an
O(1) read-only retained-record/limit query. Clock-only advance and idle assignment
avoid state copies; due transitions retain transactional budgets and ordering.
Hosts own known-content migration policy, checksums, metrics and stress reports.
CafeGame envelope 9 fingerprints catalog/profile values and declared ruleRevision;
bump that revision for saved-policy changes, never rewrite its frozen legacy stamp.
Known schemas 1..8 migrate only under the historical default content. Use the same
native/Wasm stress host for checkpoint replay; exclude wall timings from parity.
Core schema remains 1; this supersedes stage-15 future-migration guidance. GPU/mobile
acceptance and arbitrary content migrations are not implied by portable tests.
