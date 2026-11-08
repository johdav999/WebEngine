# Simulator authoring and diagnostics

Include `engine/simulator/diagnostics.hpp` and link WebEngine::Simulator.
The optional header has no renderer, IO, clock or callback dependencies.

`validateDefinitions(span<ResourceId>, span<Process>, Limits)` registers against
a detached Runtime using the same rules as live construction/defineProcess.
It returns DefinitionIssue records with a path and Error. Invalid resources/limits
return resources/limits; process count over Limits returns processes; individual
registration failures return processes/INDEX. Valid processes accumulate in order;
invalid ones do not register. Duplicate checks therefore follow live registration
semantics. Allocation failure may throw. No consumer session is touched.

`inspectJob(const Runtime&, JobId)` returns Result<JobInspection> containing a
copied Job, remaining milliseconds (only for Running) and a state/blocked-error
explanation. A queued job with no blocked Error awaits explicit assignment; the
query does not run a scheduler or predict future availability. Foreign/stale
handles return normal Runtime errors. Queries neither draw RNG nor drain events,
change time, perform assignments or retain host references.

Authors own definition storage, labels and game validation. Hosts compose these
with existing stock, order/customer, TaskBoard, OccupancyPool and Ledger queries.
Scenarios, prices, JSON, report formatting, histories and benchmarking policies
belong outside the module. CafeGame provides typed definitions/scenarios, atomic
reset through its joint envelope, browser inspectors/action explanations and a
native/Wasm deterministic reference-policy balance host. Its zero-travel reports
are logical balance experiments, not renderer or navigation performance evidence.

Core schema 1 is unchanged; content version migrations and scale/stress work
remain future stage 16. Portable tests are in tests/simulator_diagnostics_cases.hpp
and run from the standalone native/Wasm/browser contract targets. Read the current
validation evidence before claiming native Windows or hardware GPU acceptance.
