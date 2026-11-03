# Physical cloth milestone two

Resolve the consumer revision before using `engine/cloth.hpp` or the extended
`ClothingItem`/`ClothingInstance`. Read engine-root `docs/cloth-dynamics.md` and
`docs/cloth-dynamics-validation.md`; older game pins do not gain these APIs from
installing this skill. The existing `WebEngine::Clothing` target/options are reused.

- Author immutable `ClothDefinition` with one maxDistance per mesh vertex (zero
  pins), and unique structural/shear/bend distance links. Same shared rig/bind space
  as M1; no authored morph targets on physical garments. The sample is a 117-vertex
  cape; larger bounds are safety limits, not performance targets.
- Call `advanceCloth` once from `afterSimulation` with `frame.simulation.delta`,
  current world-space capsules and total acceleration (gravity plus wind). Both
  paused callbacks and normal frames occur; zero simulation delta must stay zero.
  Keep `synchronize` in `present`. Never advance cloth from rendering or visibility.
- The portable solver uses 120 Hz substeps, max eight per call, drops/reports excess
  whole steps and retains a fractional remainder. No extra animation player runs.
  Limits: 1024 vertices, 8192 links, 16 iterations, 32 capsule proxies; defaults and
  finite input bounds are in the feature contract.
- Character world transforms must be rigid and unit scale. Cloth particles/proxies
  are world-space; presentation converts to driver-local vertices without a second
  skinning pass. Existing dynamic vertex, normals/tangents, bounds/shadow/picking
  paths handle the result on browser and Windows.
- Call `resetCloth` after teleport, animation seek, rebasing or load, including
  paused changes. Large target jumps automatically reset next positive update.
  Save durable item/color choices only; load/equip rebuilds transient cloth state.
- Game code owns body/environment capsule authoring. Pins override collision;
  overlapping proxies and incompatible tethers can leave intersections. Collision
  is discrete point/capsule projection, without continuous collision, triangle
  collision, tearing, rigid-body feedback or automatic fitting. M3 adds optional
  particle self-contact and plane/box proxies; see [cloth-collision.md](cloth-collision.md).
- Updates stage all physical garment states before commit. Invalid input retains
  prior simulated/rendered state. Outfit/scene handles, subtree ownership and
  archive-transient rules remain M1's contracts; removing the character removes
  its garment solvers. Do not reparent or directly edit generated children.

Run `clothing_tests`/CTest `clothing_contracts`, including physical solver cases;
run `scripts/clothing-browser-smoke.cjs` on WebGPU and WebGL2. The Windows example's
`--smoke output-directory` supports Dawn and `--d3d12` and checks cloth motion,
pause and reset too. Record native validation independently: this machine's native
configuration currently cannot find a Visual Studio C++ installation.
