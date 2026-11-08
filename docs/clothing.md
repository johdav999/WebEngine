# Character clothing: milestone one

For optional physical garments added in milestone two, see [cloth dynamics](cloth-dynamics.md).
The outfit/asset/persistence contracts below still apply; the new feature adds an
explicit simulation update and a physical garment vertex stream.

Enable `WEBENGINE_BUILD_CLOTHING=ON` and link `WebEngine::Clothing`. Include
`engine/clothing.hpp`. This optional library adds interchangeable skinned garments,
rigid single-bone accessories, body-region hiding, per-instance color and a bounded
outfit save format. The procedural example has shirt/vest, two trousers, two boots
and hat/cap choices, with one animation controller. It requires no downloaded art.

## Asset contract

The [Blender cloth exporter](cloth-exporter.md) supplies a validated import/remapping
path for prepared physical garments. Its loader establishes the shared-rig contract
below before returning resources; arbitrary separate GLB imports still do not.

Author body regions and garments in the **same mesh bind space**, with the **same
shared immutable Skeleton object and binding table** as the driver. Matching bone
names or joint counts is insufficient. Importing two unrelated FBX/GLB files does
not automatically establish this contract: prepare compatible resources offline
or validate/remap their complete binding data before assigning a shared skeleton.
Milestone one deliberately rejects a different skeleton pointer, even if its names
match. Slot zero is the engine's identity binding; actual joint bindings start at
one. A rigid hat weights every vertex to the head binding, with its geometry already
positioned in character bind space. Do not use a skin matrix as a node transform.

Meshes need indexed triangles, normalized nonnegative skin weights for every vertex,
valid binding indices and finite vertex data. Static mesh LODs are rejected.
Published meshes/materials/rigs must remain immutable. Catalogs have at most 64 body
pieces and 256 items. IDs/slots are 1–63 ASCII letters, digits, dot, underscore or
hyphen. Body pieces have disjoint, nonzero 64-bit region masks. Equipping an item
hides any body piece intersecting its mask; author separate pieces for finer hiding.
Masks may overlap between garments. Hiding does not fit garments or prevent
intersections between garments. Clothing does not alter collision or simulation.

## Ownership and frame execution

```cpp
auto outfit=std::make_unique<engine::ClothingInstance>(driver.id(),catalog);
outfit->apply(scene,{{"shirt",{0.2f,0.6f,0.9f}},{"hat",{1,0.7f,0.2f}}});
engine::ApplicationFrameCallbacks callbacks;
callbacks.present=[&](engine::Scene& world,const engine::ApplicationFrame&) {
    outfit->synchronize(world);
};
```

The driver must be a registered `SkeletalMeshComponent`. If the catalog provides
the complete body, use a skeleton-bearing driver mesh with `visibilityMask=0`;
this hides the driver draw while preserving its child rendering and animation.
Keep the driver and generated clothing transforms in the same local space.

`ClothingInstance` copies the catalog definitions and retains immutable assets.
The application owns the instance; the scene owns its generated subtree beneath
the driver. Runtime handles avoid retained scene pointers. Destroying the driver
destroys its clothing. Destroying the C++ instance alone leaves the scene subtree;
call `clear(scene)` to remove it early. Do not edit/reparent generated children or
enable their private animation players. Make changes through `apply`.

`apply` accepts at most 16 selections and one item per slot. It stages meshes and
material overrides before adopting the replacement. Invalid IDs, duplicate slots,
invalid colors or a mismatched driver leave the previous outfit unchanged. Colors
are finite RGB multipliers in [0,1], applied independently to every material slot.
Apply/clear at an idle hierarchy boundary, never during scene traversal. Flush
pending child destruction before replacement. An empty selection restores the body.

Only the driver evaluates animation, layers, IK, markers and root motion. Generated
skeletal draws override the virtual `skinPose()` query to return the driver's final
palette; their private players are disabled. Browser WebGPU/Windows GPU skinning,
WebGL2 CPU skinning, bounds, shadows and picking use the ordinary skeletal paths.
No second clock or renderer API is added. Consume root motion only from the driver.
Call `synchronize` from `present` on paused frames too: it refreshes pose ownership
after skeleton reset and hides stale clothing if the source rig/root is replaced.
It returns false when projection is unavailable. A new incompatible rig needs a
new catalog/instance. Body-shape morph propagation is not automatic.

## Persistence

`saveClothingSelection(selection)` writes locale-independent `WECL 1` text.
`loadClothingSelection(text)` parses at most 4096 bytes and rejects malformed,
duplicate, out-of-range, truncated, unsupported-version and trailing data. Decode
then call `apply` to validate catalog membership/slot conflicts before publication.
Save only durable item IDs and RGB colors. The game owns catalog versions, character
identity, inventory permissions, migration and missing-content policy.

Generated nodes are archive-transient. Save the outfit separately and recreate it
after loading the driver; built-in scene archives do not persist this controller.
Browser demo uses localStorage key `webengine.clothing.v1`; native demo uses bounded
`DesktopFiles` atomic user save `outfit.wecl` in application `ClothingDemo`. Both
report storage failures and keep the current outfit on failed load.

## Build and validate

Follow README setup with repository-local Emscripten from `.emscripten-version`.
For an existing browser build directory:

```powershell
cmake -S . -B build/web-release -DWEBENGINE_BUILD_CLOTHING=ON -DWEBENGINE_BUILD_CLOTHING_TESTS=ON -DWEBENGINE_BUILD_CLOTHING_DEMO=ON
cmake --build build/web-release --target clothing_tests clothing_demo -j 4
ctest --test-dir build/web-release -R '^clothing_contracts$' --output-on-failure
python -m http.server 4193 --directory build/web-release/clothing-site
node scripts/clothing-browser-smoke.cjs
```

Use an actual Python interpreter (`-DPython3_EXECUTABLE=...`) if the Windows Store
alias is selected. The smoke script requires Playwright and a browser; optionally
set `BROWSER_EXECUTABLE`, `CLOTHING_URL`, `CLOTHING_SMOKE_OUTPUT` and `NODE_PATH`.
Open `http://127.0.0.1:4193/?backend=webgpu` or `?backend=webgl2` interactively.

For the README native Windows build, add the same three clothing options and build
`clothing_tests` and `clothing_demo_windows` in Release. Run CTest with `-C Release`.
Run `clothing_demo_windows.exe --smoke output-directory` for Dawn and add `--d3d12`
for direct D3D12. The smoke exercises outfit changes, disk save/restore, pause,
frame capture and shutdown. Each backend needs its own successful evidence.

Milestone one alone does not add cloth dynamics, collision-driven draping, automatic
fitting, retargeting, garment import UI or a wardrobe editor. See
[validation evidence](clothing-validation.md) for checks actually run.
