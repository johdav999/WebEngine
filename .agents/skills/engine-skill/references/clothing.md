# Clothing milestone one

Resolve the consumer pin first. `engine/clothing.hpp` and `WebEngine::Clothing`
exist in the milestone-one source change; older RPGGame/CafeGame pins may lack them.
Read engine-root `docs/clothing.md` and `docs/clothing-validation.md` before use.

- Enable `WEBENGINE_BUILD_CLOTHING`; tests/demo use the matching `_TESTS`/`_DEMO`
  options. Link the optional target above engine_core. Both host examples live in
  `examples/clothing`; the shared scene contains procedural body and garment assets.
- `ClothingCatalog` maps stable IDs/slots to immutable meshes and disjoint body
  region pieces. Require exactly the driver's shared Skeleton, binding indices,
  inverse binds and mesh bind space. Same names/counts do not establish compatibility.
  Do not simply replace an imported skeleton pointer to bypass validation.
- `ClothingInstance(driver.id(),catalog)` retains assets and scene-scoped handles.
  Call `apply(scene,choices)` at idle boundaries, and `synchronize(scene)` in the
  application's `present` callback, also while paused. One driver evaluates all
  animation/IK/root motion; generated players are disabled and share its final palette.
- Use a hidden-draw skeleton carrier (`visibilityMask=0`) when the catalog supplies
  the full body. Never disable the driver node or independently advance followers.
  Rigid hats use single-bone skinning of geometry authored in mesh bind space.
- Region masks hide whole authored body pieces. Color is a per-instance RGB material
  multiplier. Milestone one has no physical cloth; see [cloth-dynamics.md](cloth-dynamics.md) for optional M2 dynamics. Automatic fitting, garment retargeting, wardrobe editing and automatic body-morph propagation remain outside these milestones.
- The scene owns generated children; driver destruction removes them. The controller
  destructor does not edit a scene; explicit `clear(scene)` removes its projection.
  Do not reparent/edit generated children. Incompatible rig replacement hides them.
- Save `saveClothingSelection` text through the game's storage; decode with
  `loadClothingSelection`, then `apply` for catalog validation. The bounded `WECL 1`
  format stores item IDs and colors, never handles or asset paths. Generated nodes are
  archive-transient. Recreate the controller after restoring the character.
- Inventory ownership, purchase permissions, armor stats, uniforms and persistence
  migration remain application policy. Neither RPGGame nor CafeGame gets a pin bump
  or gameplay rules from installing this guide.

Build `clothing_tests` and `clothing_demo` with repository-local Emscripten; run
CTest `clothing_contracts` and `scripts/clothing-browser-smoke.cjs` on WebGPU/WebGL2.
On Windows build `clothing_demo_windows` and run `--smoke output-directory` for
Dawn and with `--d3d12` separately. Check current evidence; this machine's native
configuration attempt could not find a Visual Studio C++ installation.
