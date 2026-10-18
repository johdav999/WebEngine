# Static navmesh validation — 2026-10-17

Contracts: [static navmeshes](navmesh.md), [shared navigator](ai.md).

## Native checks

Built `navmesh_tests`, `ai_tests`, `application_tests`, `physics_tests` and
`movement_tests` with the configured native compiler, then ran:

```powershell
ctest --test-dir build/native -R '^(navmesh|ai|application|movement|physics)$' --output-on-failure
```

All five suites passed. New navmesh coverage includes owned static collision
snapshots, category/handle exclusions, collider-local offsets, capacity rejection,
immutable replacement after bake failure, finite/index/work validation, arbitrary
endpoint paths around a wall, projection rejection inside solid geometry and on
incorrect floors, ray picking, no partial paths on budget failure, traversal policy,
motor profile compatibility, radius/headroom/step/slope filtering, real motor
arrival, and transient obstacle stop/recovery at the original goal. Existing AI,
application, physics and movement regressions also passed.

The scene occupancy regression exposed an enclosed floor inside a tall solid box;
the scene adapter now removes solid-occupied voxel spans before polygonization.
Caller-supplied raw triangles retain documented surface/interior semantics.

## Browser checks

Built `web_assets` from isolated `build/web-navmesh-release` with repository-local
Emscripten **6.0.11**, serving generated `dist/` on loopback port 8137.
Matching main and asset-worker JS/Wasm were published together. SHA-256:

| Artifact | SHA-256 |
| --- | --- |
| `webengine.wasm` | `5B3BCA2CDE61F4CA31B445E3AED2201AF46352EB0B31C2FCC4B4402F8F7FACA7` |
| `asset_decode.wasm` | `F1AF9569CE76E58BB0A2BC7C8EA877C8E99E801F0D51C787C316DD16FB4228C4` |

`scripts/navmesh-smoke.cjs` passed in Chrome **155.0.8059.80** with actual WebGPU
and WebGL2 selected independently, with zero captured page/console errors.
Evidence: `build/evidence/navmesh/results.json` and per-backend PNGs.

- Live simulation advances; pause freezes navigation state.
- Walkable overlay toggle changes actual viewport pixels.
- Real pointer clicks select arbitrary non-waypoint destinations. The collision
  motor reaches them and routes around static geometry; the full route is visible.
- Camera drag preserves the movement goal. Invalid values reject; wall-interior
  goals reach bounded Unreachable handling.
- Navigation mode toggles, resize, return and re-entry preserve lifecycle ownership.

During final integration, concurrent terrain shader changes used reserved
`active` identifiers in WGSL and GLSL. Both were renamed to `activeScores` without
changing the calculations, then the final two-backend navigation run passed.
Initialization errors now fail the navmesh smoke immediately with the page status.

Also reran `scripts/ai-smoke.cjs` on the same final build with
`EVIDENCE_DIR=build/evidence/ai-navmesh-regression`. Both backends passed with zero
captured errors, covering patrol/sight/memory, predictive capsule checks, two
detours, original-goal arrival, retry bounds, automatic/manual recovery, pause,
target removal, saves after return, switching, resize, re-entry and reload.

## Dependency and documentation checks

Compared all 32 vendored Recast/Detour source files plus license byte-for-byte
against the downloaded upstream 1.6.0 archive. The archive digest and provenance
are recorded in `third_party/recastnavigation/README.md`; browser deployment copies
the license. Updated architecture, README, physics/AI/navmesh contracts and both
installed/CatGame engine-skill copies. Verified the delivered navigation sections
and references in both copies, their relative links and retained CatGame-specific
developer guidance, preserving concurrent terrain skill updates.

## Acceptance boundary

This proves bounded static baking/query contracts, flat-ground motor navigation
and desktop browser input/debug presentation on both backends. Step/slope tests
verify bake connectivity, not general stair walking through the conservative
runtime capsule probe. No dynamic carving/rebuild, crowd avoidance, navigation
streaming/workers/editor/archive codec, sustained mobile performance or physical
Android/iOS acceptance is established. Static topology must be explicitly rebaked
after source geometry changes. Unrelated shared-checkout work is not accepted by
these focused suites.
