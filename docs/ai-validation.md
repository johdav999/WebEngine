# Game AI validation — 2026-10-17

This is the initial milestone record. Later obstacle-recovery implementation and
fresh evidence are in [AI recovery validation](ai-recovery-validation.md); current
contracts are in [Game AI](ai.md).

Implemented and verified in the shared development checkout. Existing unrelated
changes were retained; this report describes the AI increment, not acceptance of
every other working-tree feature.

## Native

Built `ai_tests`, `application_tests`, `movement_tests` and `physics_tests` with the
configured WinLibs compiler in `build/native`. Ran:

```powershell
ctest --test-dir build/native -R '^(ai|application|movement|physics)$' --output-on-failure
```

All four suites passed on the final AI/demo code. AI coverage includes shortest
directed paths, disconnected goals, start=goal, search budget/no partial output,
bounded nearest snapping, invalid graph/request data, decision catch-up control,
real collision-motor corner following/arrival, atomic invalid-route preservation,
explicit cancellation, disable/re-enable, stalled movement/stop, free XYZ movement
without physics, sight range/cone/occlusion/sensors, disabled/pending/stale/foreign
targets, pause, investigation/memory expiry, immediate removed-target cancellation,
blocked-route restart, actual camera framing/restoration, inactive scenes,
replacement/unloading, demo cleanup and restoration of ordinary scene saves.

The first native run exposed incorrectly parented demo physics actors. They were
moved to scene-root ownership before passing runs. No engine physics contract was
changed to accommodate the example.

## Browser

Built Release `webengine`, the matching `asset_decode_worker`, and `web_assets`
using the repository-local Emscripten **6.0.11** in `build/web-fx-release`.
Published to `dist`, then copied that publication into `build/ai-browser-site`
and served it on loopback port 8137. Verified published `app.js` and `index.html`
hashes match current `web/` sources. Publication now copies generated site files
before current host adapters, preventing old staged HTML/JS from shadowing them.

Final artifact SHA-256:

| Artifact | SHA-256 |
| --- | --- |
| `webengine.wasm` | `B98883B23D8E5E18B9545348470A7BE95B589F2510700E3050D47782430BABED` |
| `asset_decode.wasm` | `843A45362690788A2EBD740DF073850E8FCEBA3B2059DACF680A20DE19A6E0E2` |

Ran `scripts/ai-smoke.cjs` using Playwright and Chrome **155.0.8059.80**, with
forced WebGPU and WebGL 2 backends. Both passed, with zero captured page, console
or GPU errors. Evidence: `build/evidence/ai/results.json` and per-backend PNGs.

The suite verifies actual live Application movement, paused state stability,
authored patrol completion, changed canvas pixels, real inspector clicks,
follow/green agent visibility, wall occlusion and retained last-known position,
memory expiry, removed-target cancellation, solid route obstruction/stall/stop,
restart, rejected action/step budgets, resize, explicit unsupported active-demo
save rejection, successful saves after return, re-entry, switching to water and
back, and page reload. Bounded explicit test steps run only while paused with
`aiTest=1`; initial movement uses the ordinary running Application.

Visual review caught an orbit-state-only restoration that did not apply a camera
pose. The demo now explicitly frames from above and restores its original orbit
pose on return; the final smoke also requires visible green-agent pixels.

## Documentation and limits

Updated README, architecture, AI contracts, the installed engine-skill and the
CatGame engine-skill copy (SKILL.md, contract map, integration, build/validation
guidance and dedicated AI reference). Verified both copies and retained CatGame's
pinned-engine/developer-settings guidance. Refreshing guidance did not advance
the CatGame engine pin.

At this initial milestone, authored-waypoint navigation had synchronous bounded searches, game-owned
decision/memory policy and explicit motor cancellation. It does not deliver
navmesh baking, dynamic graph rebuild/replanning, crowd avoidance, character-to-
character collision, behavior-tree execution, built-in AI archive codecs or AI
editor tooling. Unsupported custom demo saves fail explicitly. Desktop browser
evidence does not establish physical Android/iOS acceptance or large-agent-count
performance. No performance improvement or deterministic-networking claim is made.
