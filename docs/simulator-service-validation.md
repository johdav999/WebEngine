# Simulator service validation (stage 4)

Recorded 2026-10-25 for local uncommitted source. No release, remote CI run or
CafeGame engine upgrade is claimed.

## Checks executed

- Rebuilt simulator library, standalone example and Node/browser contract targets
  with repository-local Emscripten **6.0.11**, Ninja and CMake **4.2.1**.
- CTest: **2/2 passed** (simulator_consumer and simulator_contracts).
- Direct Node contracts: **684 checks passed**, retaining the earlier inventory
  and production cases. All checks stay active in Release.
- Edge **155.0.4283.62** via scripts/simulator-browser-smoke.cjs: **684 checks
  passed**, no console errors or page exceptions.
- Standalone example printed:
  `Arrival -> order -> reservation -> preparation -> delivery -> payment: 500 minor units at 3000ms`.
  It verifies one Payment event, one receipt, no repeated delivery, zero remaining
  output claims and game-owned integer wallet handling.
- Documentation links, source whitespace and available skill reference consistency
  checked locally.

The first service suite passed 640 checks. Final acceptance/completion outbox
rollback and receipt-limit cases brought the final suite to 684 before the Node
and Edge runs above.

## Coverage

[Service cases](../tests/simulator_service_cases.hpp) cover:

- Arrival-order FIFO, queue-head rejection, waiting-customer expiry, no order reset
  of patience, one order per customer, and served customers never expiring again.
- Multi-item/quantity production with multiple recipes, partial readiness,
  order-owned output claims protected from unrelated consumers, complete delivery
  and exactly one payment despite repeated confirmation.
- Integer price, negative-price/invalid-recipe rejection, bounded jobs-per-order,
  receipt capacity and safe retry after acknowledged record retirement.
- Unavailable ingredients, unrelated inventory holds surviving abandonment, and
  failed acceptance creating no jobs or partial service state.
- Abandonment before assignment, during travel, during preparation and after some
  outputs are produced; cancel versus finish-for-stock behavior; no input refunds,
  no output destruction and no reward on abandoned work.
- Movement failure and station removal failing the order and cleaning up peer jobs.
  Order-owned jobs cannot be independently forgotten. Finish-for-stock jobs retain
  their service records until terminal.
- Complete/expiry ties, zero-remaining blocked work, elapsed-time partition
  equivalence and delivery rejection after patience has elapsed.
- Outbox exhaustion during acceptance, cancellation cascade, completion/readiness
  and delivery preserves authoritative state, claims, time and receipt state.
- Retained receipt polling, game-owned deduplication, idempotent acknowledgement
  and joint retirement of customer/order/jobs/receipt without ID reuse.

See [service contract](simulator-service.md) for exact semantics. Build commands
are unchanged from [simulator.md](simulator.md). The browser page is
examples/simulator/browser.html, copied into the standalone Wasm build; serve the
build over HTTP, then run scripts/simulator-browser-smoke.cjs with Playwright and
optional BROWSER_EXECUTABLE/SIMULATOR_TEST_URL. The local run used bundled Playwright
via NODE_PATH and installed Edge. Temporary test server was stopped after checks.

## Limits

Native compiler lookup still found no cl, clang++, g++ or nmake. Native Windows
compilation/runtime remains unverified. The simulator CI workflow already builds
these shared tests on Linux/MSVC/Wasm when run; it was not pushed or run remotely
in this task. Browser Wasm execution does not establish GPU rendering, real
navigation, physical mobile performance or native-host behavior.

No game renderer, game rules, asset generation or provider credentials changed.
CafeGame still pins its previous engine revision. Persistence, durable payment
receipt deduplication, satisfaction, refunds and visible customer gameplay remain
later work. The sample's wallet/receipt handling is in-memory only.

Available WebEngine and CafeGame repository-local engine-skill guides were updated
and synchronized, preserving project-context.md. AGENTS.md's legacy installed
C:/Users/Johan engine-skill and CatGame mirror locations remain absent and could
not be updated. Unrelated concurrent RPG work was preserved.
