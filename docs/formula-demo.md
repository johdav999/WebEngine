# Formula circuit demo

Open `?scene=formula` after `./scripts/build.ps1 -Configuration Release` and
`./scripts/serve.ps1`. This application demo composes existing engine systems;
it does not introduce a reusable racing, road-network or vehicle solver API.

W / Up accelerates, S / Down reverses, A/D or Left/Right steers, Space brakes,
Shift applies the rear handbrake, and R / Reset car returns to the grid. Pause
holds simulation and lap time. Narrow/coarse-pointer layouts offer held touch
steering, drive and brake buttons through the existing BrowserInput sources.
The existing standard gamepad mappings remain available. Focus/blur and pause
use the host input cancellation policy. The chase camera has one presentation
writer through CameraFollowRig.

Mouse steering is on by default. Click the track to focus, then move the mouse
left/right relative to the canvas centre; centre straightens the wheels. Mouse
steering is inverted as requested: left movement turns right and right movement
turns left. Keyboard, gamepad and touch steering keep their existing direction. A 4%
dead zone rejects centre jitter, with full steering at 35% of canvas width from
centre. The HUD shows steering position and a Mouse steering toggle. A/D and
Left/Right override the mouse while held; mouse input resumes on subsequent
movement. Mouse steering uses its own existing Move-action source, preserving
throttle and brake sources. Leaving the canvas, focus loss, pause, reset, resize,
hidden pages, disabled driving contexts and shutdown neutralize mouse input.
Touch/pen movement does not steer this mouse source; held touch buttons remain.

The default now uses an easier handling preset. All tuning is in
`src/formula_handling.hpp` (`FormulaHandlingSettings` / `formulaHandling`):

| Parameter | Earlier setting | Easy setting and effect |
| --- | --- | --- |
| `maxSteerAngle` | .36 rad (20.6°) | .30 rad (17.2°), smaller low-speed steering lock |
| `steeringHalfLife` | Instant | .12 s, smooths sudden input changes |
| `steeringSpeed` | No speed scaling | 18 m/s (64.8 km/h), where steering authority is halved |
| `minimumSteering` | Full input at every speed | .25, retains high-speed corrections |
| `tyreFriction` | 1.6 | 2.1, higher tyre grip combined with the contacted surface |
| `engineTorque` | 650 N m | 450 N m, gentler acceleration |
| `frontTorqueFraction` | 0 (rear drive) | .25, spreads drive to the front wheels |
| `topSpeed` | No application limit | 33.33 m/s (120 km/h), forward throttle taper |
| `reverseSpeed` | No application limit | 8 m/s (28.8 km/h), reverse throttle taper |
| `speedTaper` | None | 5 m/s (18 km/h) before the limit, progressively fades throttle |

Steering scale is `max(minimumSteering, 1 / (1 + (speed/steeringSpeed)^2))`.
`FormulaDrivingAssist` applies it and the exponential filter on fixed simulation
ticks, so the camera/render rate does not control handling. Neutral and lifecycle
cancellation clear filtered steering immediately. The speed policy reduces engine
input near the limit; slopes, impulses and coasting are not hard-clamped. Service
braking remains immediate. The existing suspension (2.4 Hz / .7 damping), anti-roll
stiffness (3000) and chassis COM offset (-.10 m) also affect stability.

This is application tuning; other vehicles retain their existing defaults. The
component has transient desired/filter state and no archive/editor Play codec.
Browser `formulaInputState` exposes raw desired steering/throttle for diagnostics;
`vehicleState` continues to expose commands actually submitted to physics.

The Jolt adapter now inverts its internal steering sign to honor the existing
portable `VehicleCommand` contract: positive steering turns right toward +X
when facing +Z. This fixes keyboard/gamepad steering direction for all vehicles;
the directional regression checks test physical travel, not only command values.

The circuit uses a closed Hermite spline, collidable asphalt/kerb sweeps,
grass heightfield terrain with lower tyre grip, and solid outside barriers.
Pit garages, timing gantry and grandstands are ordinary application meshes.
The full source terrain owns collision, independent of rendered chunk LOD.

The HUD shows signed-drive gear, absolute speed, RPM bar, progress/minimap,
current lap time and session best. Timing starts on movement. Eleven ordered
gates followed by the forward start/finish crossing complete a lap and begin
the next immediately. Leaving the 9 m centerline envelope or implausible
progress jumps invalidates the current lap until Reset; crossing the seam
backwards or oscillating across the line cannot farm laps. Reset keeps session
best/completed count and restarts the current lap. Race state is transient,
application-owned and not part of scene archives/editor Play.

`assets/formula1-red-driving.glb` is derived from the user's `formula1_red.glb`
using `tools/prepare_formula_car.py` with Blender 3.5. The original is untouched:
all 18,977 source triangles are retained, textures are limited to 2048 pixels,
and the derived file is 2,689,600 bytes. It uses normal GLB importer budgets.
The preparation tool only reduces meshes above 200,000 triangles; this car
requires texture resizing only. This red model replaces the earlier silver car.
Reproduce with:

```powershell
& 'C:/Program Files/Blender Foundation/Blender 3.5/blender.exe' -b `
  --python tools/prepare_formula_car.py -- formula1_red.glb assets/formula1-red-driving.glb
```

The supplied model is a single merged visual; visible wheels are baked into it.
Four independent physical wheels provide steering, support, grip and braking.
The demo is an approachable automatic five-speed handling example; dedicated
aerodynamics, rigged wheel animation and real Formula 1 telemetry are not added.

Validation: build/run native `formula_demo_tests` (`ctest -R '^formula_demo$'`)
and browser `scripts/formula-smoke.cjs` with Playwright, `NODE_PATH`, and optional
`DEMO_URL`, `BROWSER_EXECUTABLE`, `EVIDENCE_DIR`. Actual current results are recorded
in [recorded validation](formula-demo-validation.md) and the evidence JSON;
desktop tests do not establish physical-mobile acceptance.
