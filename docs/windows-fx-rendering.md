# Native FX rendering

Dawn and direct D3D12 consume the existing `ParticleEmitterComponent`,
`EffectComponent`, `SceneFrame` and camera-specific `FxFrame` outputs. Direct
milestone 7 adds rendering; no emitter API, simulation, archive or playback policy
changes. The same content remains supported on browser WebGPU and WebGL2.

## Delivered behavior

- Camera-facing, velocity-stretched and supplied-normal plane quads use the shared
  112-byte `ParticleInstance` payload. Size/color curves, rotation, local/world
  transforms, masks and flipbooks retain their existing meanings. Atlas frames use
  top-left ordering, half-texel insets and explicit mip zero to prevent cell bleed.
- Ribbons and supplied-contact surface trails consume the six 48-byte vertices per
  segment produced by `FxFrame`, including tangent joins, endpoint widths/colors,
  repeating texture phase and finite fallback axes. Surface contact policy remains
  application-owned; rendering does not query terrain or physics.
- Mesh particles follow ordinary material parts, maps, PBR/unlit shading, winding,
  alpha modes, fog, culling and LOD. Portable collection supplies transforms and
  bounds. They retain the existing no-cast-shadow contract; softDistance must be
  zero for Mesh mode. Direct mesh particles use individual mesh submissions.
- Transparent meshes, quads and segments share back-to-front alpha ordering,
  followed by additive draws. Compatible consecutive FX batch without regrouping
  across alpha order. Quads/ribbons depth-test without writing depth and use
  premultiplied color, alpha or additive blending. Fog adds haze to alpha effects
  and only extinguishes additive emission. HDR retains linear radiance for bloom.

See [FX](fx.md), [planes](plane-particles.md), [ribbons](trails.md),
[surface trails](surface-trails.md) and [mesh particles](mesh-particles.md) for
definitions, simulation defaults, limits and authoring/persistence contracts.

## Soft fading and composition

Positive `softDistance` fades alpha and additive emission by the clamped opaque
view-depth gap divided by that world-unit distance. Clear depth preserves full
opacity. Perspective and orthographic cameras reconstruct depth independently.
The existing water allocator supplies one opaque World snapshot before transparent
FX; water transmission and soft FX share it. At 4x MSAA, depth selects the nearest
sample. Ordered view rectangles isolate reconstruction and sampling. Underwater
composition runs after World FX and before Overlay/GUI; HDR, resize, resolution
budgets and capture retain their normal order.

Active masked soft emitters conservatively request full-size scratch targets even
when paused/empty. Disabled subtrees and absent camera masks do not. When all
consumers are inactive, scratch targets shrink to 1x1 and skip snapshots. Particle
and transparent surfaces do not become soft-depth receivers. These are depth fades,
not collision, order-independent transparency or volumetric lighting.

## Private ownership and budgets

`d3d12_fx.inc` owns pipelines and per-view default-heap streams; HLSL expands quads
directly from shared instances. Byte comparisons skip unchanged paused uploads.
Changed streams stage once per view; barriers order prior reads and copies, while
submission fences protect staging and retired buffers. Stream caches retain numeric
payloads and GPU resources, never nodes or CPU asset borrows. Scene/view handoff
releases the caches behind outstanding fences. Identical per-frame view snapshots
retain streams and LOD history; changed camera identities invalidate them even if
an allocation reuses an old address.

Before recording, a conservative bound reserves all masked particle/ribbon bytes
for every submitted view, plus 512 bytes per view for alignment. That bound cannot
exceed 32 MiB, and the existing combined 64 MiB frame upload limit still applies.
Meshes and FX share the 40960 draw limit; the 32-view, 65536-descriptor and resource
cache bounds remain. These limits are rejection boundaries, not performance claims.
Textures use existing immutable semantic caches and wrapping validation.

Direct counters add `particles`, `meshParticles`, `particleDrawCalls`,
`trailDrawCalls`, `trailSegments`, `particleUploadBytes` and `trailUploadBytes`.
They count each rendered view; meshParticles includes collected pool records and
is not a count of visible triangles. Existing waterSnapshots includes soft-only
depth captures. Milestone 8 adds ordinary opaque mesh instancing, semantic BC7 uploads and bounded
direct device recovery. Mesh particles can share compatible ordinary batches;
quad/ribbon GPU streams and textures rebuild from retained CPU data. See
[recovery/performance contracts](windows-d3d12-recovery-performance.md).

## Run and validate

```powershell
./scripts/build-windows.ps1
./build/windows/Release/webengine_fx_windows.exe --backend d3d12
./build/windows/Release/webengine_fx_windows.exe --backend dawn
./scripts/windows-fx-smoke.ps1 -EvidenceDir build/evidence/windows-fx-new
```

The gallery exposes representative FX presets, restart, simulation playback, fog,
HDR/bloom and MSAA. Presets are deterministic paused examples; restart after a
one-shot effect drains. F11 toggles SDL borderless fullscreen; Esc exits.
`--capture image.ppm` captures the retained controls and exits; create its parent
directory first. It is an application example, not a new engine editor.

`windows_fx_smoke` captures 102 cases per backend; the verifier checks actual depth,
color, ordering, atlas, mask, fog, transform, material, MSAA, HDR, water and lifetime
behavior, then compares paired pixels. Run CTest `fx`, `trail`, `plane_particle`,
`mesh_particle`, `render_frame`, `material`, `water`, `desktop_api` and
`desktop_api_d3d12`. Test a separate `-Backend D3D12` build as well.

Build browser Release with `.tools/emsdk` pinned by `.emscripten-version`. Run
`fx-milestone-smoke.cjs`, `trail-depth-smoke.cjs`, `plane-particle-smoke.cjs`,
`mesh-particle-smoke.cjs` and `surface-trail-smoke.cjs` against a frozen build with
`NODE_PATH`, `BROWSER_EXECUTABLE`, `DEMO_URL` and `EVIDENCE_DIR` set. Each forces
WebGPU and WebGL2. Report native and browser evidence separately. See
[milestone 7 validation](windows-d3d12-milestone7-validation.md).
