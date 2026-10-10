# Application lifecycle validation — 2026-10-10

Implementation: portable `Application`, owned scene IDs and lifecycle hooks,
deferred scene transitions, independent input/simulation/presentation clocks,
paused reconciliation and browser pause/visibility/shutdown integration.

| Check | Result |
| --- | --- |
| Full native regression suite | 31/31 passed in `build/detail-native` |
| Isolated related native suites | Application, actor, input, physics, root motion and scene archive passed in `build/lifecycle-native` |
| Final application tests | Passed, including actual Jolt body freeze/resume, ownership cleanup, failed loaders/binding and deferred shutdown |
| Emscripten release build | Built with the local 6.0.11 SDK in `build/web-lifecycle-release`; published to local `dist` |
| Browser lifecycle acceptance | 7 checks passed against `build/lifecycle-browser-site`, port 8097 |
| Existing browser archive regression | 8 checks passed against the lifecycle build; physically saved 91,736,487-byte scene |

Browser: Windows Chrome 154.0.8037.99, real WebGPU rendering. The lifecycle run
verified running clocks, frozen simulation/physics clocks and exact animation
pixels during pause, working paused camera input, all clocks frozen during host
suspension, explicit pause preserved across restoration, no hidden-time
simulation catch-up, archive replacement/return without accumulating owned
scenes, and terminal/idempotent shutdown with zero loaded scenes. There were no
JavaScript, console or GPU errors in the lifecycle run.

Lifecycle evidence: `build/evidence/application/results.json`, PNGs and
`lifecycle.wescene`. Archive regression evidence:
`build/evidence/application-archive-regression/results.json`, PNGs and saved
archives. The archive script's optional native-origin fixture check was skipped
because its fixture was absent from that evidence directory; eight applicable
checks ran and passed.

The scripts control RAF timestamps for deterministic images and intercept
external Google Fonts CSS with an empty stylesheet. Browser suspension coverage
calls the same exported suspension API used by the visibility handler; it does
not automate OS minimization. Native lifecycle tests additionally verify actual
physics transforms/step counts across pause and suspension. The evidence covers
this desktop browser/GPU and does not establish physical mobile acceptance,
cross-browser compatibility or a performance benchmark.

Loading is synchronous in the runtime; the browser stages asynchronous archive
fetch/decode before ownership transfer. Transitions switch scene ownership and
presentation binding at frame boundaries; visual fade effects are application
presentation policy. Custom renderer bindings must remain alive through explicit
shutdown and preserve their old binding if target validation throws.
