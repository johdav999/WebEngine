# Simulator production validation (stage 3)

Recorded 2026-10-25 for the local uncommitted implementation. No remote CI,
release/tag or CafeGame dependency update is claimed.

## Executed

- Pinned repository-local Emscripten 6.0.11, CMake 4.2.1, Ninja: rebuilt the static
  simulator library, Node contracts, browser contracts and production example.
- `ctest --test-dir build/simulator-wasm --output-on-failure`: **2/2 passed**.
- Direct Node run: **354 simulator checks passed**, including the original 106
  inventory contracts. Checks remain active in Release.
- Headless Edge **155.0.4283.62**: **354 checks passed**, no page/console errors.
  Run through `scripts/simulator-browser-smoke.cjs` against a local HTTP server
  serving `build/simulator-wasm/browser.html`.
- Standalone example: **1 coffee completed at 3000ms**, no repeated production.
- Documentation/source links, available skill-copy consistency and whitespace
  checks completed locally.

The first production build passed 326 checks. Additional mixed-deadline and
multi-resource rollback regressions brought the final count to 354; the final
build and both execution environments used those updated tests.

## Contract coverage

- Queued → assigned → running → completed, explicit travel arrival, pause by not
  advancing, completion at the exact deadline and exactly-once output.
- Queued, assigned and running cancellation; no consumed-input refund; repeated
  terminal calls; safe terminal record retirement.
- FIFO worker contention, station slot contention, lowest-ID selection, worker
  availability, station/worker capability matching, and skipping blocked jobs.
- Multi-resource failed assignment leaves input/output/worker/slot untouched.
- Output capacity blocks preparation before consumption; held output space cannot
  be stolen by inventory additions. Standalone reservations and job holds coexist.
- Movement failure and stale tokens; no automatic retry or accidental late start.
- Station removal cancels simultaneous assigned/running jobs atomically; worker
  removal cancels its job and releases its slot.
- Equal-deadline JobId order and chronological completion with differing recipe
  durations; equivalent final state under differently partitioned elapsed time.
- Completion budget with zero remaining time but pending equal-time work; event
  outbox backpressure; cancellation/start failure preserves holds; overflow at start.

Commands to reproduce and API contracts are in [simulator.md](simulator.md) and
[simulator-production.md](simulator-production.md). Test implementation:
[production cases](../tests/simulator_production_cases.hpp).

## Limits and outstanding platform checks

Native tool lookup again found no cl, clang++, g++ or nmake on PATH. The earlier
native configuration failed for that reason. Native Windows compilation/execution
is not verified; the existing simulator workflow will compile the shared source on
Windows/MSVC and Linux when run, but no CI run is claimed here. No platform-specific
code or dependencies were added. Browser Wasm tests do not establish native
correctness, hardware WebGPU/WebGL 2 rendering or physical mobile performance.

No renderer, navigation, Application scheduler or game code changed. Tests validate
explicit assignment/arrival commands, not real navigation or rendered customer AI.
The selected output-capacity policy is conservative when inputs and outputs use
the same resource. The staged full-state copy is designed for bounded small
simulators, not a benchmarked high-throughput production scheduler.

The newly available repository-local engine-skill and CafeGame skill guide were
updated and synchronized while preserving project-context.md. AGENTS.md's older
installed `C:/Users/Johan/.codex/skills/engine-skill` and CatGame mirror paths remain
absent; these unavailable copies could not be updated. This does not claim they
are replaced by the repository-local packages. Concurrent RPG changes were preserved.
