# Simulator customer service (v0.1 stage 4)

`WebEngine::Simulator` now supports the complete headless service loop:
arrival → order acceptance → reservation → preparation → explicit delivery →
payment receipt. It builds on [production](simulator-production.md) and
[inventory](simulator.md) without adding rendering, navigation, storage or providers.
This is local uncommitted implementation; CafeGame's dependency pin is unchanged.

## API and game responsibilities

See [public header](../include/engine/simulator/runtime.hpp) and
[complete executable example](../examples/simulator/headless.cpp).

- `arriveCustomer(patienceMs)` creates a Waiting customer with an absolute deadline.
  Patience must be positive and fit the simulation clock. Waiting and preparation
  share this deadline; accepting an order does not reset it.
- `serviceQueue()` returns Waiting customers in arrival-ID order. `acceptOrder`
  accepts only the queue head; otherwise it returns NotQueueHead. Each customer
  has one order. InService customers no longer occupy the acceptance queue.
- `acceptOrder(customer, lines, inputInventory, outputInventory, totalPrice, policy)`
  atomically creates the order and linked production jobs. Each OrderLine specifies
  a game-defined item ResourceId, positive quantity and direct recipe ProcessId.
  Acceptance does not consume or reserve ingredients: unavailable stock can be
  replenished while the customer waits, subject to patience. Recipe/capacity/limit
  validation failure leaves the customer waiting and creates no jobs.
- Direct order recipes must produce exactly one resource matching the requested
  item. Quantities must be exact whole batches; duplicate item lines are rejected.
  Multi-item orders may use different recipes, and quantities create multiple jobs.
  Recipe byproducts, substitutions, recursive production and partial delivery are
  deferred. Generic production still permits multi-output processes.
- `assign`, navigation/`arrive`, and `advance` remain explicit. Orders are Accepted
  until a linked job starts, Preparing during production, and Ready only after all
  linked jobs complete. OrderReady is emitted once. Assignment still uses job FIFO
  and skips unavailable work; acceptance FIFO does not guarantee completion order.
- `deliver(order)` requires Ready state and time strictly before the customer's
  deadline. It atomically consumes all order-owned output claims, marks Delivered,
  marks the customer Served and creates one Receipt plus one Payment event.
  Repeated delivery returns AlreadyTerminal without consuming or emitting again.

The game supplies recipes, requested items, total price in integer minor units,
patience, arrival schedule, satisfaction and refund rules. Price must be nonnegative;
the core never computes a menu price, owns a wallet, or charges before delivery.
No provider keys, generation or network services enter the loop.

## Claims, abandonment and failures

Each completed linked job adds its outputs to stock and reserves them for its
active order in the same transaction. Another order or standalone inventory
reservation cannot consume those items. Order-owned jobs cannot be forgotten with
`forgetJob`; they are retired together with their customer record.

`abandon(customer)` explicitly departs a Waiting or InService customer. Deadline
expiry performs the same operation automatically. The order's game-supplied policy
is selected at acceptance:

| Policy | Queued/assigned work | Already-running work | Already-produced items |
| --- | --- | --- | --- |
| CancelProduction (default) | Cancel and release all holds | Cancel; no consumed-input refund; release output capacity/worker/slot | Release claims into ordinary stock; preserve items |
| FinishForStock | Cancel and release all holds | Finish using existing holds; outputs enter ordinary stock with no order claim | Release claims into ordinary stock; preserve items |

No payment occurs for abandonment. Finishing after abandonment does not re-open the
order or emit OrderReady. `forgetCustomer` rejects while finish-for-stock work is
still active, preserving its ownership references.

Cancelling/failing an individual linked job, including movement failure or resource
removal, marks its active order Failed, cancels other unfinished linked jobs and
releases already-produced claims. Failure uses cancellation even if the order's
abandonment policy was FinishForStock. Running inputs are never silently refunded;
produced inventory is never destroyed by cleanup. The customer becomes Departed.
Repeated terminal calls cannot release claims twice. Unrelated inventory holds and
other orders remain untouched.

All mutation paths stage a bounded candidate state. Cascaded cancellations and
notifications are committed together. If the event outbox cannot hold the entire
change, return LimitReached without changing live jobs, stock, claims, time or
receipts. This extends the production transaction guard to protect failure paths
that produce more than one event. Drain and retry. Configure enough total event
capacity for the largest atomic order transition; draining cannot make a batch
larger than the configured outbox fit.

## Time and expiry ordering

The host supplies simulation milliseconds; pause means no advance. `advance` merges
job deadlines and customer deadlines chronologically. At equal timestamps, all job
completions run first by JobId, then customer expiries by CustomerId. Completing at
exact expiry may produce an item but does not deliver it: the customer departs and
the output claim is released to stock. **Delivery must occur before the deadline.**
This explicit stage-4 boundary rule prevents a small work budget from extending
patience by allowing delivery between completion and pending expiry.

`Advance.completed` counts jobs; `Advance.expired` counts customers. The shared
transitionsPerAdvance budget counts both. On ordinary budget/full-outbox stopping,
`blocked` plus consumed/remaining time describes committed progress; continue even
when blocked has zero remaining time. If a multi-event transition exhausts the
outbox mid-candidate, the entire advance returns LimitReached and publishes no
progress, including earlier completions in that candidate. Drain and retry the
original delta on error; retry only remaining time on a successful progress result.
Do not interleave new host work while earlier pending simulation time remains.

A single customer expiry may cancel up to jobsPerOrder jobs; work is bounded by
both per-advance and per-order limits. Explicit assignment is still separate from
advance, and large elapsed time does not invent travel arrivals or assignments.

## Receipts and retirement

Payment events identify a stable ReceiptId. `receipt(id)` returns the order and
amount; `pendingReceipts()` returns unacknowledged receipts even after events have
been drained. The game applies each receipt ID to its wallet at most once, then
calls idempotent `acknowledgeReceipt`. An acknowledged receipt remains queryable
until its service record is retired; acknowledgement alone does not reclaim its
record-limit slot.

`forgetCustomer` accepts only Served/Departed customers with all jobs terminal and
any payment acknowledged. It removes the linked jobs, order and receipt together;
IDs are never reused. Drain/handle events before retirement if handlers need those
records. Repeated commands against forgotten IDs return InvalidId, not a new payment.

Exactly-once delivery/payment emission applies inside one runtime. Durable wallet
and receipt deduplication across saves/crashes belongs to the game save envelope;
see simulator-persistence.md for core saves. There is no distributed payment guarantee. The example
uses an integer wallet and processed-receipt set in game-owned code.

New default bounds: 1024 retained customers, 1024 orders, 1024 receipts, 32 jobs per
order; nonzero limits are required. Existing bundleEntries bounds line count and
existing total job limits also apply. Receipt-capacity failure leaves a Ready
order and its claims intact for retry. Retire completed acknowledged records to
recover space. There is no automatic silent eviction.

## Evidence and remaining scope

[Service tests](../tests/simulator_service_cases.hpp) cover FIFO fairness, multi-item
orders, unavailable stock, both abandonment policies, failure propagation, repeated
delivery, receipt polling/deduplication, retirement, expiry ties and transactional
outbox failure. See [actual validation](simulator-service-validation.md).

Customers have logical states only. Movement, visible characters, order tickets,
satisfaction and refunds remain game work or later
milestones. This portable module changes neither browser renderer nor native host.
