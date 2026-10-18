# Game AI

`engine_ai` is an optional portable C++20 library, publicly linked to
`engine_core`. Include `engine/ai.hpp` and link `engine_ai`. It does not require
a renderer, browser adapter, physics world, worker, network or language model.
Patrol, combat, quests, needs, memory policy and action selection remain game code.

## Waypoint navigation

`WaypointGraph(points, edges)` validates and owns an immutable graph with 1–4096
finite world-space points and at most 32768 directed edges. Duplicate edges are
deduplicated; self/out-of-range edges reject construction. Each edge costs its
Euclidean length. Add the reverse edge explicitly for bidirectional travel.
`nearest(position, maxDistance)` returns the lowest-index nearest point in the
inclusive finite snap range, or no result. It does not test access or clearance.

`findPath(start, goal, expansionBudget=4096)` performs synchronous A* with a
Euclidean heuristic and deterministic index tie-breaking. Budget must be 1–4096.
The result distinguishes Complete, Unreachable and BudgetExceeded, includes an
expansion count, and publishes node indices/points only on success. Start and
goal are included. Requests validate before searching. Callers bound total
requests per tick; this is a per-request work bound, not a frame-time guarantee.

Graphs are authored navigation data, independent of camera visibility, mesh LOD,
render interpolation or terrain residency. Authors must ensure ground support,
slopes, permitted areas and access. For collision-baked static navigation use
[NavMesh](navmesh.md). Dynamic graph rebuild and jump-link execution are absent. Optional clearance-aware search and automatic
replanning below reuse immutable graphs and authoritative physics geometry.

### Clearance-aware search

`findPath(Vec3 start, Vec3 goal, WaypointSearchSettings)` augments the graph with
temporary source/destination vertices. It considers every start/goal connector
within `snapDistance`, avoiding the blocked-nearest-waypoint trap. There is no
direct source-to-destination shortcut: successful paths contain at least one
original waypoint. Returned `nodes` contains original indices only; `points`
includes exact start and goal plus original waypoints, with coincident endpoints
permitted. Original node-index search remains unchanged.

`canTraverse(from, to)` is a synchronous, non-retained predicate applied to graph
edges and connectors. Do not mutate the graph/scene in the predicate. Default
settings are 5 m snap range, 256 expansions and 64 tested segments. Expansion
budget is 1–4098 (including the two temporary vertices); segment budget is
1–65536. Snap range is finite and nonnegative. `testedSegments` and `expanded`
report actual search work; budget exhaustion publishes no partial points/nodes.
Distance scanning of the bounded waypoint set is separate from segment testing.
Callers remain responsible for total work across agents.

## Following a path

`PathFollower` owns an independent copy of 1–4098 finite points. `follow(span)`
validates/copies before replacing a route. It does not borrow the graph or scene.
It reports Idle, Moving, Arrived, Blocked or Cancelled, plus its next point and
owned path. Definitions and runtime state are application-owned and not built-in
scene archive types.

```cpp
// In an application Actor subclass, with one direct CharacterMotorComponent:
void fixedTick(double dt) override {
    follower.step(*getComponent<engine::CharacterMotorComponent>(), dt);
}
void onDisable() noexcept override {
    if (auto* motor = getComponent<engine::CharacterMotorComponent>())
        follower.cancel(*motor);
}
```

Opt the actor into ticks using `setTickEnabled(true)`. Actor fixed ticks execute
before child motor ticks. Do not rely on sibling component insertion order.
`step(motor, seconds)` accepts `(0, .25]`, submits persistent world-space velocity
and clamps the requested displacement to the next point. Collision-aware motors
receive XZ movement; their gravity, grounding and collision solver remain owners
of vertical movement. Free motors receive XYZ movement without creating physics.
Arrival uses XZ radius plus world-Y height tolerance for both modes. Defaults:
2 m/s, .15 m radius, .5 m height tolerance, .05 m progress threshold, 1.5 s without
progress and 60 s maximum route duration. Reaching a point resets the stuck timer;
actual simulation position measures progress. This detects stalled motion, not
all possible navigation failures. Arrival and blocking stop persistent commands.

While following, one producer owns that motor. Call `cancel(motor)` immediately
on target loss, owner disable/detach, action interruption or control handoff. It
clears the route and motor command, without undoing gravity or an airborne jump.
Stopping calls to `step` alone does not clear a motor's persistent velocity.
Do not transfer one active follower between motors. No retained raw pointers,
callbacks, asynchronous completions or subscriptions are introduced.

## Automatic obstacle recovery

`Navigator` composes the existing follower with clearance-aware search through
`NavigationSurface`. `WaypointGraph` and [NavMesh](navmesh.md) implement that
interface; `WaypointNavigator` remains a compatibility alias:

```cpp
engine::NavigationSettings settings;
settings.maxReplans = 4;             // Initial search plus up to four replans.
engine::WaypointNavigator navigator(settings);
navigator.navigate(sharedGraph, destination);

// Owning actor fixedTick, before child motors:
navigator.step(motor, fixedSeconds);

// Owner disable/detach, target loss or control handoff:
navigator.cancel(motor);
```

`navigate(shared_ptr<const NavigationSurface>, goal)` validates before replacing the
request, owns the immutable surface and takes effect on the next `step`. It never
retains scene/motor pointers or search predicates. It binds the first stepped
motor's scene-scoped component handle and cancels if that handle changes. Do not
transfer active control between motors; cancel the original motor explicitly.
Game code still owns target selection and must cancel/replace a request when its
target disappears or changes. `retry()` explicitly restarts Unreachable,
BudgetExceeded or TimedOut requests with a fresh budget and the retained goal;
it returns false for other states. `cancel()` stops movement and releases graph,
probe and failed-corridor interests. No subscription or worker is introduced.

`navigateSource(shared_ptr<const NavigationSource>, goal)` follows an explicitly
versioned publication slot. Invalidate before collision changes; the next fixed
step stops stale commands and enters `WaitingForSurface` until a complete matching
replacement is published. Original goals, deadlines and retry bounds survive
replacement. See [revision and publication contracts](navmesh.md#navigation-revisions-and-replacement).

Every eligible planning step performs at most one synchronous search. While
moving, each step checks the whole next pending segment before submitting
movement, including a corner's outgoing segment when the previous point has
already been reached. A blocked segment cancels the persistent command
immediately and enters Waiting. After the configured simulation-time delay,
search tests current geometry and can choose a different route/connector. An
unsuccessful search also waits and retries within the same request budget;
removing an obstruction during that wait can recover automatically. Already
reached waypoint goals complete without a search; navmesh goals still require
projection and profile validation. The motor remains the position and
collision authority if geometry moves after a check.

Clearance uses an upright capsule derived from the motor's half-height/radius,
with default radial margin .02 m and bottom lift .03 m. The bottom lift avoids
rejecting ordinary floor contact; the probe top is correspondingly conservative.
A nonzero segment uses one physics sweep, while a stationary connector uses one
overlap. Probes use authoritative poses, ignore sensors and the agent's own
bodies, intersect the configured obstacle layers with the motor's simulation
collision-layer matrix, and preserve the other caller query filters. No physics
world is created when absent; in that case clearance has no collision geometry
to test. Free XYZ motors can use the same navigator.

This is conservative translational clearance, not walking simulation: it does
not prove ground support, infer door/jump actions, emulate step climbing, predict
future moving bodies, detect other virtual characters, or keep paged terrain
resident. It can reject a step the motor could climb. Authors still guarantee
support/access along edges and allowed connectors. A motor stall can blacklist
its failed directed corridor for the remaining request, allowing another
approach; explicit retry/new requests clear that conservative history.

Default `NavigationSettings`: existing follower settings, 5 m snap distance,
.5 s replan delay, 60 s total simulation deadline (including waits), 256
expansions/search, 64 clearance queries/step and four replans after the initial
search. Settings validate before use; `maxReplans` is 0–64 and delay/deadline
must be finite and positive. Each moving step uses at most one query; a planning
step uses at most `queryBudget`. The two phases do not stack query budgets in
one step. Request statistics expose searches, replans, total queries/expansions
and last-step counts. These are work bounds, not a hard frame-time guarantee or
a global multi-agent scheduler.

`NavigationStatus` distinguishes Idle, Planning, Moving, Waiting, Arrived,
Unreachable, BudgetExceeded, TimedOut, Cancelled and WaitingForSurface. Search budget exhaustion is
terminal immediately; blocked/unusable routes exhaust their delayed retry count
before Unreachable. This means no usable route was found within the authored
graph, clearance policy and recovery budget, not that the whole world was proved
unreachable. Terminal requests stop and never poll/retry indefinitely. The
deadline spans all replans so replacing a follower route cannot reset it.

## Sight and decisions

`observeActor(scene, observerId, targetId, forward, settings)` performs one
candidate query using generation-checked scene handles and authoritative world
positions. Missing, foreign, pending-removal, disabled and self targets return no
observation. Finite nonzero forward, finite offsets, positive finite range and
half-angle `(0, pi]` are required. Defaults: range 10 m, half-angle pi/3, world
eye/target offsets `(0,1,0)`. Successful observations return the target handle,
root position and eye-to-aim distance. Nonfinite poses produce no observation.

When physics already exists, a filtered ray tests occlusion. Observer/target
bodies and sensors are excluded; all other caller query filters apply. Without
physics, only range and angular tests apply; no world is created. This does not
infer visibility from visual meshes and does not enumerate all actors. Callers
choose candidates, cap query count and supply spatial indexing if required.

`DecisionClock(.1)` defaults to a .1 s interval. A positive first advance fires
immediately; zero time never fires. Each advance permits at most one decision,
discards excess catch-up time and starts a fresh interval. Invalid time rejects.
Advance only with simulation time; do not tick it while paused/suspended. This is
a soft frequency cap, not deterministic networking or exact 10 Hz scheduling.
Use application state/utility logic or the reusable [state-machine runtime and
graphical editor](ai-state-machines.md). No behavior-tree interpreter or generic
blackboard is delivered.

## Demonstration and validation

Open `?scene=ai`, or **Game AI → Open AI demo**. The example is
`src/ai_demo.cpp`, compiled into the browser application and native AI tests,
outside reusable libraries. A collision-aware agent patrols six waypoints, sees
a nearby target through physics-tested sight, follows, investigates the last
known position for two seconds after losing sight, then returns to patrol.
Target destruction cancels tracking on the next fixed tick. All-around demo sight
uses half-angle pi and range 7 m; the library defaults remain narrower.

Blue/green/amber/red agent colors show patrol/follow/investigation-or-return/failed.
Cyan geometry shows the authored route; yellow shows the next path segment;
green shows a visible target sightline; amber marks the last-known location.
Inspector buttons show/hide/move/remove the target, block the lower route, move
the blocker onto the upper detour, seal both exits, clear obstacles, retry the
retained goal and restart patrol. Lower-route blocking preserves the active goal;
the navigator checks it and detours. Sealing exits resets to a known start and
demonstrates bounded unreachable handling. Status shows waiting/planning/movement
and lifetime replan counts. Restart preserves diagnostic counters. Return destroys all
example actors and restores prior enabled state/orbit. Application owns pause,
fixed ticks, scene activation and suspension; debug rendering never advances AI.

The demo's custom actor intentionally has no archive codec: saving an active AI
demo rejects explicitly under existing archive rules. Return to scene removes
the custom actor, restoring ordinary scene saves. Applications must register
custom codecs and reconstruct logic, stable target references, routes and clocks
if AI instance persistence is needed. The [AI graph editor](ai-state-machines.md)
saves definitions separately as `.weai` assets. No crowd avoidance/character-to-character
collision, navigation workers or physical-device acceptance is
implied.

Build `ai_tests`, then run `ctest --test-dir build/native -R '^ai$'
--output-on-failure`. `scripts/ai-smoke.cjs` exercises both forced renderers on
the exact generated site, actual controls, live movement, deterministic paused
test steps, investigation, target removal, blocking/restart, pause, resize and
return/re-entry. `?aiTest=1` enables bounded (0–600) explicit fixed steps only
while the application is paused; normal game code never uses that bridge.
See [initial validation evidence](ai-validation.md),
[obstacle recovery validation](ai-recovery-validation.md), and
[static navmesh contracts](navmesh.md) / [validation](navmesh-validation.md).

The standard `web_assets` target publishes the selected generated site first and
then the current `web/` host. Follow the same order when staging a separate test
site: reused build directories may contain old HTML/JS copies. Deploy matching
`webengine.js/.wasm` and `asset_decode.js/.wasm` together, along with host adapters,
fonts and fixture assets. AI itself adds no worker or renderer-specific assets.
