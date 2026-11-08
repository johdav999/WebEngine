# Simulator production (v0.1 stage 3)

The optional `WebEngine::Simulator` now provides immutable process definitions,
stations with bounded parallel slots, capability-matched workers and production
jobs. It uses the same standard-C++20 runtime and inventory counters as stage 2;
there is no renderer, navigation, platform, thread or external-service dependency.
This is local implementation, not a published release or CafeGame pin upgrade.

## Public API and usage

See [runtime.hpp](../include/engine/simulator/runtime.hpp) and the executable
[headless example](../examples/simulator/headless.cpp).

1. `defineProcess(Process)` registers an immutable namespaced ID, positive duration
   in milliseconds, nonempty positive input/output bundles and one required
   capability. Duplicate IDs, duplicate bundle resources and unknown resources fail.
2. `createStation(capabilities, slots)` and `createWorker(capabilities)` create
   typed runtime-scoped handles. Capabilities use the same namespaced key grammar
   as resources. One worker and one station slot serve each job.
3. `enqueue(process, inputInventory, outputInventory)` creates a queued job.
   Inventories must declare the required resources with sufficient absolute
   capacity; available stock/free space is checked later during assignment.
4. `assign()` walks queued jobs by monotonic JobId (FIFO), selecting the lowest
   compatible free StationId, slot index and WorkerId. It skips blocked jobs
   without changing their priority. Input stock, output capacity, station and
   worker are acquired together, or none is acquired. `Job::blocked` reports the
   last assignment attempt's reason, not a continuously recalculated condition.
5. Read `job(id)` to obtain its worker, station, slot and assignment token. Route
   movement through the existing engine navigation in the host. Call
   `arrive(jobId, assignment)` only after actual arrival. Late/foreign/wrong-state
   responses are rejected. Travel does not advance production. `assign(true)` is
   an explicit immediate-arrival adapter for tests/simple games, not pathfinding.
6. Advance simulation time, drain events and project read-only state into the UI.
   Delivery, customers and rewards remain later stages.

`assign` returns the number assigned, including zero on ordinary contention or
outbox backpressure. Check queued job states/blocked reasons and retry after the
condition changes. It emits JobAssigned and, in immediate mode, JobStarted.
`assign(true)` requires two event slots per assignment; insufficient space holds
nothing. Process registration, diagnostic queries and terminal-job forgetting
emit no events. Other state changes have typed events with affected handles.

## Explicit scheduling and bounded time

Assignment happens **only at calls to `assign`**, not implicitly inside `advance`,
arrival, cancellation or stock addition. Hosts own command order: process input,
assign at the desired boundary, report arrivals, and advance time. After completing
work, call `assign` again to schedule waiting jobs. This avoids inventing travel
completion times or another application scheduler. Equal timestamped host commands
produce deterministic results; adding extra assignment commands changes the input.

`advance(milliseconds)` now returns `Result<Advance>` instead of `Status`.
`Advance` contains consumed and remaining milliseconds, completion count and
`blocked`. Existing callers that only check success must also handle this progress
report when production is enabled. Completions are processed by deadline, then
JobId; events use the actual deadline even when a large delta spans it. When no
more work is due, the clock reaches the requested target.

At most `limits.transitionsPerAdvance` completions are processed per call. On a
work-budget or outbox limit the clock stops at the last processed deadline (or the
original time if none could run); unprocessed time is returned. Even a zero
remaining delta can have `blocked=true` when another completion shares the same
instant. Drain the outbox and call `advance(remaining)` again, subject to the host's
per-frame budget. Retain pending time between frames; do not drop it or add new time
twice. Do not accept later-timestamp host commands until the earlier pending time
has been processed. There is no offline catch-up policy.

Negative elapsed values and integer overflow fail before any changes. Durations
must fit the clock when work starts; a failed start retains its assignment for
explicit cancellation. Pause means no time advancement. Failed validation or
allocation leaves the live state and clock intact; large updates publish a staged
candidate before exposing time progress.

## Ownership and cancellation

Job records own their inventory holds directly. These holds share Stock.reserved
and Stock.reservedCapacity accounting with standalone reservations, but expose no
ReservationId that a caller could release behind the job's back. Input/output holds
are conservative even when input and output use the same resource: reserved inputs
do not count as free output capacity until actually consumed.

| Operation | Effect |
| --- | --- |
| Assignment | Reserve all input quantities and output capacity, occupy worker/slot; consume nothing. |
| Arrival/start | Consume input once, release input holds, retain output capacity and worker/slot; set absolute deadline. |
| Completion | Produce output once, release capacity and worker/slot; mark Completed. |
| Cancel Queued | Mark Cancelled; no resources were held. |
| Cancel Assigned | Return all holds and worker/slot; consume nothing. |
| Cancel Running | Release output capacity and worker/slot; no input refund and no output. |
| `movementFailed(id, token)` | Assigned jobs become Failed and release every hold. No automatic retry. Enqueue a new job if the game chooses to retry. |
| `removeStation` / `removeWorker` | Atomically cancel affected active jobs before removing the resource. Running input remains consumed. Queued jobs remain eligible for other resources. |
| Repeat cancel/arrival on terminal job | Return AlreadyTerminal; no new events, consumption or production. |

`setAvailable(worker, false)` prevents future assignments and lets existing work
continue; cancel explicitly to interrupt it. Remove cancels. A removal requires
room for all cancellation events plus its removal event; on insufficient room it
changes nothing. If the configured event capacity is smaller than that batch,
cancel jobs individually and drain their events before removing the resource.

There is no reservation timeout in stage 3. A host must report movement failure or
cancel stalled travel. Assignment tokens are immutable per job; jobs are never
silently reassigned. A retry has a new JobId/token, so delayed callbacks cannot
start it. Terminal records retain historical station/worker IDs for diagnostics;
those resources may have been removed. `forgetJob` accepts only terminal records
and never reuses their IDs. Drain events before forgetting records needed by event
consumers. Runtime destruction releases all state without callbacks.

## Limits, costs and deferred work

New defaults: 256 process definitions, 128 stations, 128 workers, 4096 retained
jobs, 16 slots per station, 16 capabilities per station/worker and 64 completions
or assignments per call. Limits must be nonzero. Each assignment scans at most the
configured job count and bounded station/worker lists. Each advance scans bounded
jobs per completion. Transactions copy bounded state before publication, favoring
clear failure guarantees over high-volume throughput. No performance guarantee is
inferred from these tests.

No game recipe, price, currency, refund rule, customer, service queue, delivery,
save/load schema or GUI is introduced here. CafeGame's game rules and dependency
pin remain unchanged. The same source builds for native and Wasm; see actual
[stage 3 evidence and limitations](simulator-production-validation.md).

## Stage 4 integration

[Customer service](simulator-service.md) now links jobs to orders, claims completed
outputs, expires patience and produces delivery receipts. Linked job failure or
cancellation atomically fails and cleans up its order. For service jobs, forgetJob
is rejected: use forgetCustomer after all work terminates and payment is acknowledged.
Advance also processes expiries and returns an expired count. A multi-event outbox
failure returns LimitReached with the entire candidate/time unchanged; a successful
blocked result still reports committed consumed/remaining time. See the service
contract before integrating those paths. Persistence and rendered adoption remain
future work.
