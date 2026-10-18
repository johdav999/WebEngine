# Terrain stage-three validation

Verified on 2026-10-15 against the stage-three implementation. Earlier stage-one
and stage-two records remain historical; this run exercises the current terrain
contracts in [terrain.md](terrain.md).

## Build and environment

- Windows desktop, Chrome 155.0.8059.79, real forced WebGPU and WebGL 2 contexts.
- Repository-local Emscripten 6.0.11, C++20 Release, isolated
  `build/web-terrain-stage3-release`, target `webengine`.
- Local Jolt source: `build/dependencies/JoltPhysics-5.3.0`.
- Staged matching host, generated main/worker artifacts, assets and Noto fonts in
  `build/terrain-edit-browser-site`; loopback preview port 8185.
- Verified `webengine.wasm` SHA-256:
  `DAD797053F71899E361B3955B169A5281907544C658B6BFE9FDF8CE86BE954D0`.

## Native checks

`cmake --build build/native-physics -j6` and
`ctest --test-dir build/native-physics --output-on-failure -j6`: **46/46 passed**.
The WinLibs runtime directory was prepended to PATH for native execution.

Terrain tests establish immutable source preservation, localized pointer sharing,
seam-corner dirty halos and base/LOD positions/normals equal to complete
regeneration. They exercise raise, flatten, frozen-neighbor smoothing, normalized
paint bytes, global material UV transforms, no geometry/collision rebuild for
paint, paint-mask texture limits on a 4097-sample axis, source/physics agreement
after sculpting, stable component handles, stale
edit rejection, invalid/no-op brushes and scene/library round trips.

Source-only archive loading succeeds within a budget that rejects eager geometry;
resident reconstruction is also charged and rejected under an insufficient
budget. Streaming tests enforce one admission per update, four residents,
constant geometry payload over repeated distant moves, weak mesh expiry and
stale-handle rejection after eviction, removal of old collision, unloaded-source
edits appearing on admission, dirty resident collision updates and clear/teardown.
The suite retains the real format-10 terrain compatibility fixture.

## Browser checks

Run with `NODE_PATH=build/detail-browser-tools/node_modules`, the installed Chrome
executable in `BROWSER_EXECUTABLE`, and `DEMO_URL=http://localhost:8185`:

```powershell
node scripts/terrain-edit-smoke.cjs
$env:EVIDENCE_DIR='build/evidence/terrain-stage3-detail'
node scripts/terrain-detail-smoke.cjs
$env:EVIDENCE_DIR='build/evidence/terrain-stage3-base'
node scripts/terrain-smoke.cjs
```

All three scripts pass in both backends. Their results/screenshots/downloaded
archives are in the matching evidence directories; the edit suite defaults to
`build/evidence/terrain-edit`. Native CTest output is recorded there too.

The edit suite checks actual rendered sculpt/paint changes, raise/lower/flatten/
smooth values, query/collision agreement, affected/rebuilt counters, undo/redo
pixels, invalid-tool preservation and real pointer clicks/drags/release/cancel.
Edited full/mixed-LOD overhead coverage is identical. Real inspector controls
exercise stamping, undo/redo, streaming enable/disable and ball contact after
sculpting. New admissions retain selected camera/shadow policies and the same
surface-inspection pixels as explicitly reapplying that mode. It verifies format-14 scene download/open, edited source and resident
coverage, malformed-load preservation, paused renderer recovery and demo reset/
return/restart. Canvas focus decoration is removed by blurring before archive
pixel comparisons; terrain pixel thresholds remain unchanged.

| Observation | WebGPU | WebGL 2 |
| --- | ---: | ---: |
| Local sculpt geometry chunks rebuilt | 1 of 16 | 1 of 16 |
| Paint changed canvas pixels | 6,484 | 6,691 |
| Paint geometry rebuilds | 0 | 0 |
| First streaming admission / queued | 1 / 3 | 1 / 3 |
| Settled resident chunks | 4 | 4 |
| Resident visual geometry bytes, all LODs | 341,760 | 341,760 |
| Cached meshes across eight focus moves | 8 | 4 |
| Saved edited source height at (18,18) | 8.4799995 m | 8.4799995 m |
| Downloaded resident-scene archive | 484,494 bytes | 484,494 bytes |

Both actual browser edit archives load and validate in native
`scene_archive_tests.exe --load <file>`. The previous terrain/detail scripts also
cover heightmap import, full-resolution ball contact, fresh-page restore, material
isolation/weights, camera/shadow submission independence and recovery. Both
browser result files report no page/console errors. WebGPU shadows remain
independent; WebGL 2 reports its existing lack of shadow maps.

## Delivered boundary

Streaming bounds generated chunk geometry and optional per-chunk static bodies.
Source heights/global weights remain resident under existing source limits;
staging may retain one additional admission batch. Brush source copying and
chunk/collision generation are synchronous. Render/GPU cache release follows
existing frame references; external retained resources/history can extend lifetime.
This is not network/disk source paging or a frame-time/performance guarantee.

Scenes save current edited source and resident actors, not stream-owner policy,
pending admissions or undo history. Applications recreate runtime ownership and
choose gameplay-safe residency and dynamic-body wake/reposition policy. Legacy
whole-instance collision still recooks the complete collider for height edits.
Physical Android/iOS and sustained mobile performance were not tested.
