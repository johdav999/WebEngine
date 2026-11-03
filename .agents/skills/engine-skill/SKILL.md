---
name: engine-skill
description: Build or extend games using WebEngine's public C++ APIs, application lifecycle, assets, animation, physics, UI and optional gameplay libraries. Use for WebEngine integration in RPGGame, CafeGame or another consumer, and for deciding whether a requested game feature is already supported by its engine revision.
---

# WebEngine game integration

For Blender-authored physical garments, read [cloth-exporter.md](references/cloth-exporter.md).
Use the versioned exporter and validated rig-remapping loader; verify the consumer pin.

For character garments and outfits, read [clothing.md](references/clothing.md).
Milestone one shares one rig/pose, hides authored body regions and persists item/color
choices; physical cloth is optional in milestone two; automatic fitting is not included. Verify consumer pins.

Use [engine.md](references/engine.md) for ownership, frame execution, public targets, feature routing, persistence and validation. Read the sections relevant to the task; follow their engine-root documentation pointers for details.

Read [project-context.md](references/project-context.md) for this installation's host, dependency and validation guidance. Recheck its facts against the current project's instructions and build configuration.

Before proposing or writing integration code:

1. Resolve the consumer's actual engine root and pin. The guide describes a source snapshot, not a guarantee that the consumer pin contains every feature.
2. Read applicable `AGENTS.md` files, the game's design/architecture and the engine contracts for the affected subsystem. Confirm API names and exported targets in public headers and CMake.
3. Prefer existing engine mechanisms. Keep game policy and durable game-session state in the game; keep renderer and platform internals behind public host interfaces.
4. Preserve the single owner of frame advance, deferred scene destruction, handles, immutable assets and callback lifetimes. Stage changes before publishing valid state.
5. Validate the actual consumer and affected platforms. Clearly separate proposed functionality, delivered source, uncommitted work and tests run in this task.

Do not silently update dependency pins, enable optional systems, broaden the game's platforms or turn an integration request into an engine rewrite. If an engine extension is required, scope it explicitly and follow the engine repository's architecture, documentation and cross-platform requirements.

For a question, explain the existing capability and any missing seam. For an implementation request, carry out the authorized change and relevant verification. Documentation is guidance, not permission to run paid asset generation or publish externally.

For RPG inventory/equipment, read the R3 extension in the bundled guide and the
selected engine revision's docs/rpg-inventory.md. Keep operations in the existing
RPG transaction, and verify the consumer pin before using API 1.2.0.

For RPG abilities, statuses and randomness, use the R4 supplement in the guide
and engine-root docs/rpg-abilities.md. Preserve candidate RNG/effect atomicity
and explicit phase commands; do not infer turn progression from a render tick.

For RPG encounters and turn budgets, use the R5 supplement and engine-root
docs/rpg-encounters.md. Keep lifecycle/status/cooldown changes within the shared
transaction and preserve encounter/turn epochs. R5 allowances do not yet authorize
or spend tactical actions; that integration is R6. Read rpg-r5-validation.md for evidence.

For tactical RPG actions, read the R6 supplement and engine-root docs/rpg-tactics.md.
Use stamped immutable quotes and explicit world-stamp publication; retain the
presentation gate through visual recovery. Raw combat UseAbility now requires the
tactical wrapper. Validation is recorded in docs/rpg-r6-validation.md.

For RPG quest rewards, read the R7 supplement and engine-root docs/rpg-quests.md.
Use the private-runtime coordinator and its joint preflight/publish contract;
never commit quest completion separately from reward validation. Base RPG remains
independent of Quests. Evidence is in docs/rpg-r7-validation.md.

For RPG save/load, read the R8 supplement and engine-root docs/rpg-snapshots.md.
API 1.7.0 supplies bounded canonical bytes, exact catalogue/handler compatibility,
detached validation and rebased restore. Use Coordinator save/restore for joint
quest receipts and rewards; clear host input and rebuild projection after load.
Storage/migration remain consumer policy. Evidence: docs/rpg-r8-validation.md.

R9 adds the optional root-build RPG rendered sample. Read engine-root
docs/rpg-hosts.md for browser/Windows commands and docs/rpg-r9-validation.md for
actual platform coverage. Its scene/controller are consumer policy, not kernel APIs.

For physical cloth milestone two, read [cloth-dynamics.md](references/cloth-dynamics.md). Preserve simulation-time ownership, rigid transforms, reset and collision limits.

For cloth self-contact and environment collision, read [cloth-collision.md](references/cloth-collision.md). M3 adds particle separation and borrowed world-space planes/boxes; preserve the documented discrete collision limits.

For cloth triangle surfaces and swept contact, read [cloth-surfaces.md](references/cloth-surfaces.md). M4 adds vertex/face response and bounded linear sweeps; it does not provide full mesh/mesh CCD.

For edge-to-edge cloth self-contact, read [cloth-edges.md](references/cloth-edges.md). M5 derives bounded garment edges and adds four-endpoint response and optional linear sweeps.

For weighted fabric, aerodynamic wind and translating collision proxies, read
[cloth-materials-motion.md](references/cloth-materials-motion.md). M6–M8 retain
simulation-time ownership and require explicit application-owned collider motion.

For simulator games, read [simulator.md](references/simulator.md) for production, service, wallet persistence, host rebinding and release validation. Verify the bundled module separately from the renderer pin.

For Mixamo animation-only FBX, explicit joint matching and offline Blender binding,
read [mixamo.md](references/mixamo.md). Review the selected engine revision and
actual browser/Windows validation before claiming support.

R10 release candidate: read docs/rpg-release-1.md, docs/rpg-r10-validation.md and
docs/rpg-game-adoption.md. API 1.7.0 / wire 1; RPGGame adoption remains separate.

For simulator actor movement, read engine-root docs/simulator-movement.md. TravelBinding guards assignment/epoch/request identity; hosts own scene handles, interaction points, navigation cancellation and restore rebinding.

Library stabilization L1: read docs/rpg-l1-validation.md. Direct D3D12 diagnostics
can explicitly select WARP with WEBENGINE_D3D12_WARP=1; normal play remains hardware.

For simulator queues, seating or equipment access, read engine-root docs/simulator-occupancy.md. OccupancyPool is optional and host-owned; validate owners, preserve FIFO, guard arrival tokens and release claims on departure. CafeGame uses schema-2 spatial envelopes; core snapshot remains schema 1.

For simulator workflows and staff scheduling, read engine-root docs/simulator-workflows.md. TaskBoard is above production jobs: hosts supply time, report arrival/effects, bind core worker IDs, preserve interrupted work and validate joint saves. CafeGame uses envelope schema 3; core snapshots remain schema 1.

L1 renderer diagnostics: WEBENGINE_D3D12_GPU_VALIDATION=1 additionally enables GPU
binding validation when WEBENGINE_D3D12_VALIDATION=1. Read the L1 report for gate
status and Xvfb browser commands; isolated diagnostic probes are never full rendered acceptance.

For simulator supply/restocking, read engine-root docs/simulator-supply.md. SupplyBoard borrows Runtime, owns incoming-capacity reservations, polls explicit simulation time and validates rebased typed saves. Use Runtime::transfer for atomic inventory movement. CafeGame now uses schema 4.

L1 RPG acceptance passed at c7db4b4; see the validation report for scope and remaining general-engine failures.

For simulator cash, costs or wages, read engine-root docs/simulator-economy.md. Ledger provides contiguous source watermarks, compact history and staged allocation-free publication; the game owns wage/price policy and exclusive cross-system coordination. CafeGame now uses envelope schema 5.

For simulator quality, freshness or maintenance, read engine-root docs/simulator-quality.md. Freshness and Condition are optional value mechanisms; hosts own expiry disposal and task/ledger policy. Use Runtime::remove for atomic unreserved stock disposal. CafeGame now uses envelope schema 6.


For simulator placement and layout, read engine-root docs/simulator-layout.md. GridLayout validates bounded footprints/access/reachability; hosts own navigation and prices. OccupancyPool::setEnabled disables unclaimed stable points. CafeGame uses envelope 7 with closed/drained editing and joint layout/ledger/claim validation.


For simulator demand and satisfaction, read engine-root docs/simulator-demand.md. demand.hpp supplies explicit seeded DemandRandom, ArrivalTimer and validated pure scores. Hosts own profiles, arrival boundaries, candidate RNG publication and exactly-once review/receipt coordination. CafeGame envelope 8 validates guest clocks/outcomes and paid-service totals together.

RPG library L2 uses API 1.8.0: read docs/rpg-parties.md and docs/rpg-l2-validation.md
before party/team integration. This supersedes earlier RPG API 1.7.0 inventories.

For native clothing builds, use the installed Visual Studio generator in a fresh
build directory; see references/cloth-edges.md for VS2022 retry guidance. Preserve
the shared shader literal splitting required by MSVC.
VS2022 native clothing uses direct D3D12; the pinned Dawn binary requires a newer
compatible MSVC runtime (see docs/windows-native.md).

RPG library L3 uses API 1.9.0: read docs/rpg-targeting.md and docs/rpg-l3-validation.md
for targeting/area effects. Pure preview and quoted execution share exact geometry;
check the consumer pin before use. L2 snapshot compatibility is unchanged.


Simulator authoring stage 15: include engine/simulator/diagnostics.hpp for detached
validateDefinitions(resources, processes, limits) with field paths and read-only
inspectJob(Runtime, JobId) state/blocked-error explanations. These reuse core rules,
never advance/assign/drain events and own no host references, IO or scenes. Game
hosts own typed definitions/scenarios, action availability and balancing policy.
CafeGame has four atomic-reset presets, browser inspection/JSON export and the
native/Wasm cafe_balance reference-v1-zero-travel-100ms runner. Core schema 1 and
Cafe envelope 8 remain unchanged; hot reload, content migrations and scale are
not supplied. See docs/simulator-authoring.md and preserve native/browser parity.

RPG L4 (API 1.10.0) adds optional damage, defeat and revival; read
docs/rpg-vitality.md and docs/rpg-l4-validation.md before adoption. Check the
consumer pin: this implementation is local pending publication approval.


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

RPG L5 (API 1.11.0) adds bounded combat AI planning and cancellation; read
docs/rpg-combat-ai.md and docs/rpg-l5-validation.md. L4/L5 remain local pending
publication approval; check the consumer pin before adoption.

RPG L6 (API 1.12.0) adds bounded weighted loot and atomic reward bundles; read
 docs/rpg-rewards.md and docs/rpg-l6-validation.md. L4-L6 remain local pending
publication approval. Check the consumer pin before using these APIs.

RPG L7 (API 1.13.0) adds equipment prerequisites, multi-slot equipment and
selectable progression/respec. Read docs/rpg-builds.md and docs/rpg-l7-validation.md.
The implementation is local pending publication approval; verify consumer pins.

RPG L8 (API 1.14.0) adds exact payments, receipt-backed trades/crafting and
bounded detached exploration replay. Read docs/rpg-economy.md and the actual
platform evidence in docs/rpg-l8-validation.md. Verify consumer pins: this
implementation remains local pending publication approval.


For deterministic vegetation, bounded cell streaming, wind or saved harvesting,
read [foliage.md](references/foliage.md). Link the optional WebEngine::Foliage
target; use separate visual/gameplay focus and explicit simulation-time GPU wind.
M6 dense foliage defaults to bulk instances; use component plus instanceKey for picks.
Verify the consumer pin and docs/foliage-dense-validation.md before platform claims.
Scene projections are transient; persist stable removal IDs.
