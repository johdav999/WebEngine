# Simulator library (v0.1 release candidate)

`engine_simulator` / `WebEngine::Simulator` is an optional standard-C++20-only
library. The delivered foundation provides inventories, atomic resource bundles,
input and output-capacity reservations, typed runtime handles, explicit elapsed
milliseconds and bounded transition events. No renderer, scene, platform, AI,
Modules, RPG, Quests, IO or provider service is required.

Production stations, jobs and workers are implemented; see
[production contracts](simulator-production.md). [Customer service and payment receipts](simulator-service.md) are also implemented.
Version-1 persistence and CafeGame integration are implemented. Start with the [API guide](simulator-guide.md) and [release gates](simulator-release.md).

## Build and use

The engine root option `WEBENGINE_BUILD_SIMULATOR` defaults OFF. Tests are enabled
with `WEBENGINE_BUILD_SIMULATOR_TESTS`; requesting tests without the library fails.
For a dependency-free consumer, add only `WebEngine/simulator` after enabling the
library. Do not configure the engine root merely to get the simulator.

```cmake
set(WEBENGINE_BUILD_SIMULATOR ON CACHE BOOL "" FORCE)
add_subdirectory("${WEBENGINE_SOURCE_DIR}/simulator" engine-simulator)
target_link_libraries(my_app PRIVATE WebEngine::Simulator)
```

Standalone native build (requires a configured C++20 compiler):

```sh
cmake -S examples/simulator -B build/simulator-native -DWEBENGINE_SOURCE_DIR=/absolute/path/to/WebEngine -DWEBENGINE_BUILD_SIMULATOR=ON -DWEBENGINE_BUILD_SIMULATOR_TESTS=ON -DWEBENGINE_BUILD_SIMULATOR_EXAMPLE=ON
cmake --build build/simulator-native --config Release --parallel 2
ctest --test-dir build/simulator-native -C Release --output-on-failure
```

For Wasm, activate the repository's pinned SDK and prefix configure with `emcmake`;
use a separate build directory and Ninja. The same contracts run under Node. The
test configuration additionally builds browser.html and simulator_browser_tests.js when Emscripten is enabled.
The standalone example and tests default OFF. Its CMake checks reject accidental
core/graphics/physics targets and dependency population.

See [complete example](../examples/simulator/headless.cpp) and
[public API](../include/engine/simulator/runtime.hpp).

```cpp
#include "engine/simulator.hpp"
#include <array>
using namespace engine::simulator;
const ResourceId beans{"cafe:beans"};
Runtime runtime(std::array{beans});
auto inventory = runtime.createInventory(std::array{Amount{beans, 10}});
if (!inventory) return; // report describe(inventory.error)
auto added = runtime.add(inventory.value, std::array{Amount{beans, 4}});
if (!added) return;
auto reservation = runtime.reserve(inventory.value, std::array{Amount{beans, 2}});
if (!reservation) return;
auto consumed = runtime.commit(reservation.value);
if (!consumed) return;
// total=2, reserved=0. Repeated commit returns AlreadyTerminal.
```

## Contracts

- `Runtime` owns all state, copies resource definitions at construction, and is
  noncopyable/nonmovable. Calls occur on one owner thread. It retains no callbacks
  or external objects. Destruction releases storage without executing effects.
- `ResourceId` is a distinct string value. Runtime definitions must be unique,
  namespaced ASCII keys (one `:`, nonempty sides, lowercase letters/digits/`_.-`,
  at most 128 bytes). Invalid definitions/limits throw `invalid_argument` during
  construction. Methods return `Result<T>`/`Status` for ordinary failure;
  `describe(error)` gives a diagnostic. Allocation failures may throw, preserving
  authoritative state.
- `InventoryId` and `ReservationId` are distinct types. Their monotonic value is
  scoped by a runtime epoch; foreign, zero and forgotten handles are rejected.
  Terminal records remain until explicit `forget`; active records cannot be
  forgotten. IDs are never reused. Exhaustion rejects before wraparound. Epochs
  are runtime identity, not deterministic gameplay output or persisted identity.
- Inventories declare per-resource capacity. Quantities are signed 64-bit integers;
  capacity may be zero, operation quantities must be positive. Empty bundles,
  duplicate resources, unknown slots and negative quantities are rejected.
  Quantities are checked by subtraction before addition, avoiding overflow.
- `reserve` atomically holds the entire input bundle. `available = total-reserved`.
  `commit` consumes held inputs once; `release` restores availability without
  changing totals. Repeating either terminal operation returns `AlreadyTerminal`
  and never mutates stock or emits another event.
- `reserveOutput` holds free capacity. Ordinary `add` and other output holds cannot
  use it. Commit adds the promised output once; release returns capacity without
  adding stock. Input reservations do not free capacity until consumed. Atomic
  acquisition across input/output/station/worker resources is provided by assign;
  do not treat separate calls here as a combined transaction.
- All resource validation precedes publication. Inventory-changing commands stage
  a bounded full state copy and swap it into place after successful allocation.
  This favors clear failure guarantees for small simulators; command cost scales
  with retained records/events. It is not a high-volume performance claim.
- Limits bound definitions, inventories, retained reservation records, bundle
  entries and pending events. Defaults are 256/128/4096/64/4096. Games should lower
  them to their needs. Reservation amounts are canonical resource-key order.
  Explicit call order determines event order; no hash iteration affects behavior.
- `advance(int64 milliseconds)` rejects negative values and overflow; zero is valid.
  It processes due production completions and returns consumed/remaining time.
  Hosts supply simulation time, preserve
  fractional conversion remainder, and stop advancing while paused/suspended.
  There is no second clock or automatic catch-up. See the production progress contract.
- `drainEvents` returns owned ordered events and empties the outbox. Creation,
  stock addition, reserve, commit and release emit one event each. Events contain
  simulation time and affected handles; query state for quantities. If the outbox
  is full, the command fails before mutation, including commit/release. Drain and
  retry. Time advance emits production completion events; terminal-record forgetting
  emits no event. Drain events
  before forgetting records if consumers need to query those records.
- Queries return copies, never mutable references into the runtime. There is no
  retained callback/reentry path, scene handle, general ECS or event framework.

Use existing `Application` simulation delta and scene-scoped actor handles in host
adapters. Both WebGPU and WebGL 2 can project the same state; neither renderer is
linked by this library. See [validation and limitations](simulator-validation.md).

Stage 5 adds Runtime::save, restore, epoch and receiptSequence; see [snapshot contract and validation](simulator-persistence.md). This supersedes earlier persistence-deferred notes.
