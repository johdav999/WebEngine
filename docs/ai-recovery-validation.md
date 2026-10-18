# AI obstacle recovery validation — 2026-10-17

This increment extends optional `engine_ai` with clearance-aware waypoint search
and `WaypointNavigator`, preserving the original node-index search and simple
`PathFollower`. Source contracts: [Game AI](ai.md).

## Native verification

Built `ai_tests`, `application_tests`, `movement_tests` and `physics_tests` with
the configured native compiler, then ran:

```powershell
ctest --test-dir build/native -R '^(ai|application|movement|physics)$' --output-on-failure
```

All four suites passed. The AI suite adds:

- Traversal-filtered alternate paths, blocked nearest connectors and bounded
  segment/expansion exhaustion with no partial route publication.
- Predictive capsule checks stopping persistent movement before a new obstacle.
- Two consecutive detours when a blocker moves onto the alternate path, followed
  by real collision-motor arrival at the original destination.
- Per-step query/expansion limits, finite retry count, total request timeout,
  explicit terminal outcomes and no retry loop after exhaustion.
- Automatic recovery when an obstruction disappears during Waiting; explicit
  retained-goal retry after terminal failure; disable/re-enable cancellation.
- Rejection of an off-axis obstacle that a center ray misses but the motor-width
  capsule cannot clear; ignored sensors and motor collision-layer filtering.
- The demo's obstacle insertion preserving a followed target/goal, completed detour, sealed-exit failure, clearing
  and retry, in addition to prior sight/memory/pause/scene replacement coverage.

Existing application, movement and physics regressions passed with the same
checkout. This does not assert acceptance of all unrelated shared-checkout work.

## Browser verification

Built and published `web_assets` from `build/web-fx-release` using the
repository-local Emscripten **6.0.11**, retaining the matching asset worker.
Served `dist/` on loopback port 8137. Final SHA-256:

| Artifact | SHA-256 |
| --- | --- |
| `webengine.wasm` | `363E887FE19E0303A1B351C204B4966B4F533C94560978A2F43A4A52F28B2331` |
| `asset_decode.wasm` | `843A45362690788A2EBD740DF073850E8FCEBA3B2059DACF680A20DE19A6E0E2` |

Ran `scripts/ai-smoke.cjs` with `EVIDENCE_DIR=build/evidence/ai-recovery` in
Chrome **155.0.8059.80**, forcing WebGPU and WebGL 2 independently. Both passed
with zero captured page, console or GPU errors. Evidence is
`build/evidence/ai-recovery/results.json` plus per-backend PNGs.

Actual inspector buttons insert a lower-route obstacle after live navigation is
planned. The next fixed step reports Waiting and unchanged XZ position; after
the delay it reports Moving with a new path and increased replan count. Moving
the same body to the upper detour triggers a second replan, and the original
patrol goal completes. Sealing both exits performs exactly five searches (one
initial plus four retries), reports Unreachable, stops motion and performs no
more searches during additional steps. Clear/retry recovers the retained goal;
clearing during Waiting also recovers automatically without manual retry.

The suite also checks query limits, actual canvas motion/visible-agent pixels,
normal live Application updates, pause, patrol, perception/occlusion/memory,
target removal, invalid input, restart, resize, explicit active-demo save
rejection, successful saves after return, re-entry, switching demos and reload.
Deterministic test stepping remains bounded to 600 fixed steps per call and is
available only while paused with `aiTest=1`. Visual review of detour captures
confirmed the agent, blocker, authored route and next segment remain visible.

## Ownership, documentation and scope

README, architecture, AI contracts and both installed/CatGame engine-skill copies
describe the new APIs, defaults, budgets, lifetimes and validation. CatGame-specific
dependency/pin guidance is preserved; its engine gitlink is not advanced.

Graphs stay immutable. Requests own shared graph definitions, path/goal/probe
state and a generation-checked motor identity, without borrowed scene pointers,
retained predicates, workers or delayed scene callbacks. Replans run only in
simulation and do not depend on render visibility/LOD or interpolation.

Clearance is conservative swept-volume testing, not proof of walkability. Authors
still guarantee support/access, connector policy and terrain residency. There is
no navmesh baking, graph topology rebuild, future-motion prediction, crowd
avoidance, character-to-character collision, behavior-tree execution, AI editor
or built-in AI persistence in this increment. Budgets are per request/agent;
there is no global agent scheduler or hard frame-time guarantee. Desktop browser
checks do not establish physical Android/iOS acceptance or large-crowd performance.
