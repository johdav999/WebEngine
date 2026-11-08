# Simulator v0.1 API and integration guide

Status: release candidate source, not a published tag. See [release gates](simulator-release.md), [changelog](simulator-changelog.md), and the authoritative [public header](../include/engine/simulator/runtime.hpp).

## Consumer setup

Link WebEngine::Simulator and include engine/simulator.hpp. Enable WEBENGINE_BUILD_SIMULATOR before adding the simulator/ subdirectory; the standalone entry requires only C++20. No renderer or dependency download is configured. The complete service example is [headless.cpp](../examples/simulator/headless.cpp); [parity.cpp](../examples/simulator/parity.cpp) adds contention, restored travel, interruption and payment retries. Build commands are in [simulator.md](simulator.md).

API/source version is v0.1; snapshot schema is separately version 1. Rebuild consumers with the same toolchain. No cross-compiler binary ABI or compatibility with later prerelease source changes is promised. CafeGame bundles a reviewed source snapshot independently of its renderer pin.

## Owner-thread session loop

1. Construct Runtime with stable namespaced resource keys and explicit Limits. Define immutable processes, inventories, capacities, stock, stations and workers. Check every Result/Status; describe(error) produces a diagnostic.
2. Supply customer arrivals and patience from game rules. serviceQueue returns FIFO customers; acceptOrder accepts only the queue head. Each line names a direct single-output recipe and an exact number of batches. Prices are integer minor units supplied by the game.
3. Call assign explicitly. It scans FIFO jobs, holds ingredients and output capacity and acquires worker/slot together. Query Job.blocked to explain unavailable stock, capacity, station or worker. assign(true) is an explicit immediate-arrival adapter for simple hosts.
4. For navigation, bind Job.id plus assignment token to the existing engine movement API. Call arrive only on actual arrival; movementFailed terminates work. Old epochs/tokens cannot target a new assignment. Missing actors never imply arrival.
5. Convert Application simulation delta to integer milliseconds with a retained fractional remainder. Call advance; retain remaining time when blocked, drain/process events and retry within the host's work budget. Pause/suspension supplies no elapsed time. Assignment is separate from advance.
6. Project query copies to scene actors/HUD. A Ready order owns its outputs until deliver or abandon. Deliver before the customer's deadline; production at the exact expiry timestamp does not extend patience or pay automatically.
7. Apply each pending receipt to the game wallet at most once using stable receipt values, then acknowledgeReceipt. Drain events for effects/diagnostics; pendingReceipts is the durable retry source. Forget terminal customers only after all jobs terminate and payment is acknowledged.

## Operation map

| Area | APIs | Important behavior |
| --- | --- | --- |
| Inventory | createInventory, add, stock | Explicit per-resource capacity; available excludes input/order claims. |
| Holds | reserve, reserveOutput, commit, release, forget | Whole bundles succeed atomically; terminal repetition cannot double consume or produce. |
| Production | defineProcess, enqueue, assign, arrive, advance | Immutable definitions; one worker and slot per job; absolute simulation deadlines. |
| Interruption | cancel, movementFailed, removeStation, removeWorker | Release ownership; consumed ingredients are not refunded. |
| Service | arriveCustomer, acceptOrder, abandon, deliver | FIFO acceptance; order-owned outputs; explicit finish-for-stock or cancel policy. |
| Payment | pendingReceipts, receipt, acknowledgeReceipt, receiptSequence | Wallet belongs to game; acknowledge does not itself retire the service record. |
| Retirement | forget, forgetJob, forgetCustomer | Active/linked work cannot be arbitrarily removed; IDs are never reused. |
| Persistence | save, restore, epoch | Candidate validation, exact catalog match, fresh handles, no historical effects. |

## Failures, limits and storage

Expected failures return Error. Constructors reject malformed definitions/limits with invalid_argument. Allocation failures can throw; candidate publication preserves authoritative state. save throws length_error above snapshotBytes. Hosts must catch storage errors separately.

Outbox exhaustion rejects a command without partial publication. advance can either return successful blocked progress with remaining time, or fail a multi-event cascade with no state/time change; do not subtract elapsed time on failure. Drain events before retrying and before forgetting records that event handlers need to query.

Limits bound resource/record counts, bundles, slots, capabilities, transitions, outbox and snapshot bytes. Default snapshot limit is 8 MiB and transitionsPerAdvance is 64. Read Limits for all defaults and lower them for small games. Commands copy retained bounded state; large limits cost memory/time. This release makes no throughput or mobile-performance promise.

Save core bytes and the game's wallet, applied receipt values, fractional time and relevant policy together at an owner-thread boundary. Validate a complete candidate game envelope before swapping it into the live session. Rebind stable IDs using the new epoch, clear queued commands, reset host clocks and reconstruct travel bindings. Compare stable numeric receipt values, not epoch-bearing handles, for durable deduplication. No offline catch-up, migration beyond schema 1, authenticity protection or external-payment guarantee is provided.

See [production](simulator-production.md), [service](simulator-service.md) and [snapshot](simulator-persistence.md) contracts for detailed ordering. Navigation, rendering, audio, GUI, pricing, progression and provider generation stay outside the library.

## Authoring and inspection

Use optional `engine/simulator/diagnostics.hpp` to validate authored ResourceId /
Process catalogs against detached live-registration rules, or inspect a copied job
with its current blocked-state explanation. These queries never schedule work,
drain events or advance time. Hosts own scenarios, cross-system action reasoning,
report formats and balancing policy. See [contracts](simulator-authoring.md).

Simulator stage 16 adds optional content/checkpoint fingerprints, retained-record metrics and clock-only fast paths. Hosts own explicit migrations and stress policy. See docs/simulator-compatibility.md for contracts and simulator-release.md for evidence.
