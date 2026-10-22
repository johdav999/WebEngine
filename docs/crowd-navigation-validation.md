# Crowd and dynamic navigation validation — 2026-10-21

Verified in the shared WebEngine development checkout, preserving unrelated and
concurrent work. No consumer dependency pin, deployment or paid service changed.

## Portable build and tests

Used the existing native Debug configuration and matching WinLibs MinGW runtime.
Final CTest passed **8/8** selected suites: `crowd_navigation`, `navmesh`, `ai`,
`ai_controller`, `ai_tasks`, `movement`, `physics`, `application`.

The new focused suite covers:

- Two head-on capsule motors sidestep, retain separation and both arrive. The
  native per-step minimum center distance was **0.690135 m**, for .30 m radii.
- Four perpendicular agents pass and arrive with per-step separation checks.
- Moving a tracked kinematic body triggers revision invalidation and a usable
  carved detour. Removing it rebuilds and restores the previously blocked area.
- A bounded local navmesh segment rejects a carved hole even when both endpoints
  are supported; a clear segment succeeds. Invalid bounds/endpoints reject.
- Duplicate/invalid goals retain registered state, removal stops commands,
  disabled agents cancel, re-enable needs a new goal, and stale handles cannot
  command replacement motors. Foreign scenes and invalid settings reject.
- Removing all floor support makes the bake fail without publishing stale
  topology; the previous resource stays diagnostic while the source is dirty.
  Unchanged failure does not retry repeatedly; explicit new topology can recover.
- The actual eight-character arena, including its coordinator, facing visuals,
  closed gate and teardown, completes all goals and restores its prior scene.

The existing navigator, source-revision, AI task/controller, movement, physics and
application suites also pass. Ordinary path-following calls retain their previous
behavior; the extra surface ray constrains altered avoidance trajectories while
the navigator owns existing route/waypoint tolerances.

Repository-local Emscripten **6.0.11** built Release `webengine`, `web_assets` and
`crowd_navigation_wasm_tests` in `build/web-release`. The complete focused suite,
including the eight-character demo, passed in Node. No new external library was
added; Recast/Detour stay private existing dependencies.

## Browser acceptance

Served the selected `dist` at `http://localhost:8137`. Browser suites ran serially
with Chrome **155.0.8059.85**, actual **WebGPU** and forced **WebGL2**.
`scripts/crowd-navigation-smoke.cjs` passed at **DPR1 and DPR2**:

- Real running simulation moves the characters. Explicit paused test stepping
  rejects calls without the `crowdTest=1` fixture query.
- Head-on pair passes and arrives, with a sampled **0.899 m** minimum center
  distance and **0.478 m** sidestep. Start/arrival screenshots visibly differ
  (1,639 pixels at DPR1; 6,576 at DPR2 on both backends).
- All eight opposing characters arrive after the gate changes. Final sampled
  gate-detour separation was at least **0.679 m** on both backends.
- Revision/build counters change, sources return ready, and no bake failure is
  reported. Avoidance candidate/probe counters remain within declared bounds.
- Pause freezes positions; real device/context loss retains agent positions and
  restores identical paused pixels. Compact controls, return and restart work.
- Final page/console error lists are empty.

Browser distances are sampled at 30-step fixture checkpoints; they are not
continuous-contact proofs. Native head-on/four-way checks sample each fixed step.
The fixture is separate from the actual running-movement check and is not a
large-crowd performance benchmark.

Evidence: `build/evidence/crowd-navigation/results.json` and
`build/evidence/crowd-navigation-dpr2/results.json`, with passing/gate/arrival,
recovery and compact screenshots.

Existing `scripts/ai-smoke.cjs` passed both backends: movement/pixels, pause,
patrol/perception/memory, target removal, predictive capsule checks, movable
blockers/detours, original-goal arrival, bounded queries/retries, unreachable/stop,
automatic/manual retry, resize, save/return/re-entry and demo switches.
Results: `build/evidence/crowd-existing-ai/results.json`.

Existing `scripts/navmesh-smoke.cjs` passed both backends: click-to-move, live
simulation/pause, static detours, source invalidation, removed-floor stop, no
queries while dirty, complete replacement, original-goal recovery after restored
support/opening, overlay pixels, camera drag, unreachable targets and return.
Results: `build/evidence/crowd-existing-navmesh/results.json`.

## Final staged build identity

SHA-256 for the served artifacts used by final acceptance:

| File in `dist` | SHA-256 |
| --- | --- |
| `webengine.wasm` | `107e081c3abac426282fb06b04c3fec0e30d3ba56f2dfc410d8f67128f978ff7` |
| `asset_decode.wasm` | `c29f77dac3ce3a21ecdea8eccb0a516ca84f7a6013465bdb3cf403418f554543` |
| `crowd.js` | `1fc6ef0e62f9e91574c0c0f758a69fb8c662c61d9d7e4bf3e593cc01c96d3dc3` |

This establishes functional local avoidance and changing-obstacle replanning on
desktop Chromium. It does not establish optimal/deadlock-free traffic, general
mutual character solver contacts, sustained large-crowd performance, physical
Android/iOS acceptance or other browsers. Dynamic updates use synchronous bounded
whole-resource rebakes; untracked support changes still need explicit invalidation.
See [contracts](crowd-navigation.md) for precise ownership, defaults and limitations.

Architecture and the installed engine-skill/CatGame skill copy were updated in the
same task. Existing CatGame-specific guidance, its selected engine pin and
concurrent native-proof documentation were preserved.
