# Navigation update validation — 2026-10-18

Contracts: [navigation revisions](navmesh.md#navigation-revisions-and-replacement)
and [shared navigator](ai.md). This increment adds explicit invalidation and
whole-resource replacement for collision changes, preserving immutable meshes.

## Native verification

Built `navmesh_tests`, `ai_tests`, `application_tests`, `physics_tests` and
`movement_tests` with the configured native compiler, then ran:

```powershell
ctest --test-dir build/native -R '^(navmesh|ai|application|physics|movement)$' --output-on-failure
```

All five suites passed. New revision tests exercise real collision geometry and
character motors, including:

- Invalidating before bridge removal stops the old route without horizontal
  travel or search/clearance work while dirty.
- Failed rebakes and null publications preserve previous diagnostic resources
  while keeping the source unavailable; stale tokens and duplicate completions
  cannot publish.
- Replacing topology with two disconnected floors replans the original goal
  without crossing the gap. Restoring support and publishing resumes that same
  goal, preserving replan statistics.
- Waiting for a replacement retains the total request deadline. Publication
  cannot restart a timed-out request; explicit retry remains available.
- Cancellation releases interest in future publications. Switching to a direct
  immutable surface releases a stale source binding.
- Repeated revisions cannot reset the finite search allowance: the default
  request stops after one initial search and four replans.

The existing AI, application, physics and movement suites also passed. These
focused checks do not accept all unrelated work in the shared checkout.

## Browser verification

Built `web_assets` from `build/web-navmesh-release` with the repository-local
Emscripten **6.0.11** SDK, publishing matching main/worker JS/Wasm and host files.
Served `dist/` at `http://localhost:8137`. Tested artifact SHA-256:

| Artifact | SHA-256 |
| --- | --- |
| `webengine.wasm` | `F3AABAE9EE7046528FA0738E91E80AF008266AF64E5B5F9A329498E32E568055` |
| `asset_decode.wasm` | `2E652C2586686EA8C8E3E2C32BDA1BDEBCCD2285BD0561E3F592A3F938E0FEA6` |

`scripts/navmesh-smoke.cjs` passed in Chrome **155.0.8059.80**, forcing actual
WebGPU and WebGL2 independently, with zero captured page/console errors.
Evidence: `build/evidence/navmesh-updates/results.json` and per-backend PNGs.

The script uses real DOM controls and pointer clicks. It confirms live simulation,
pause, arbitrary destinations, static-wall detours, dirty/ready revisions, zero
dirty search work, stopping before removed support, disconnected-route handling,
original-goal arrival after floor restoration and passage opening, camera drag
ownership, invalid/unreachable goals, mode switching, resize and return/re-entry.
Pixel comparisons prove visible floor removal and stale-overlay suppression;
captured viewport images show the bridge gap and stopped agent.

Also reran `scripts/ai-smoke.cjs` with
`EVIDENCE_DIR=build/evidence/ai-navigation-updates` on the same final build. Both
backends passed with zero captured errors, covering existing patrol/sight/memory,
predictive checks, two detours, query/retry limits, automatic/manual recovery,
pause, target removal, restart, resize, save after return, switching and reload.

## Documentation and acceptance

Updated README, architecture and AI/navmesh contracts. Updated the installed
engine-skill and CatGame copy, including source ownership, invalidation ordering,
publication tokens, lifecycle/retry/deadline behavior and validation guidance.
Both copies retain unrelated terrain/vehicle and CatGame-specific guidance.

Collision changes must explicitly invalidate every affected source before
mutation; no automatic physics/terrain subscription or affected-region tracking
is delivered. Whole-mesh baking remains synchronous preparation work with existing
budgets and no frame-time guarantee. The source is owner-thread state, not a
thread-safe scene or worker API. Gravity continues while navigation waits;
removing support directly under an agent can still make it fall. No tile carving,
crowds, general stair locomotion, physical Android/iOS acceptance or sustained
mobile performance is established.
