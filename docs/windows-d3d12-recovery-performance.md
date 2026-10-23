# Direct D3D12 recovery, instancing and compressed uploads

Milestone 8 extends the existing selectable Windows backend. Browser WebGPU retains
its existing instancing, compression and recovery; WebGL2 retains individual draws,
RGBA fallback and context restoration. No new portable renderer or texture API is
introduced. See [backend selection](windows-d3d12.md).

## Device recovery

The private desktop recovery owner wraps the direct renderer. Device removed,
reset, hung and driver-internal HRESULTs, or a failed `GetDeviceRemovedReason`,
stop submission and queue reacquisition at an owner-thread service boundary.
Ordinary invalid content, user callbacks, resource-budget failures and shader
validation errors do not become recoverable-device signals.

The old renderer/device/swapchain, descriptors, mapped uploads, all GPU caches and
readback resources are released before constructing a replacement for the same SDL
window. Outstanding direct captures cancel with `captureError()`; request another
capture after recovery. No old-generation readback publishes into new resources.
Retained CPU scenes/assets, camera handles, GUI/font state, quality settings, shadow
policy, simulation time and transient water inputs survive. Reacquisition resolves
handles again, skipping cameras destroyed while unavailable. Scene handoff/unbinding
clears the scene-scoped water snapshot and scene/view borrows.

The new device rechecks capabilities and lazily uploads the current CPU resources,
including BC7 variants, morphs, skin palettes and FX. The host pumps platform events
while graphics is unavailable, cancels held input/capture and freezes application
advancement. Recovery resets timestamp baselines without outage-time catch-up and
preserves the explicit Running/Paused state. It does not reload scenes or recreate
gameplay. Same-backend recovery does not switch to Dawn or software rendering.

Automatic reacquisition permits three attempts per unstable cycle, with bounded
50/100/200 ms backoff depending on the attempt count. After 120 successful presented
frames the retry budget resets. Exhaustion reports an error; `retryGraphics()` starts
a fresh bounded cycle. Startup acquisition failures use the same bounded policy.
Unbound/shutdown scenes cannot acquire another device. A strict direct host retains
the recovery owner and a stable capabilities value across GPU replacements. As with
all native API fallback, query current capabilities after each host boundary.

`diagnostics().ready` gates use. `renderState` reports `generation`,
`deviceRecovery`, `recoveryAttempts` and `lastDeviceLoss`; `error` stays empty during
scheduled recovery and reports terminal failure on exhaustion. Presentation counts
remain monotonic; GPU resource/counter caches restart per generation.

With `DesktopSettings::allowGraphicsFaultInjection=true`, idle
`simulateGraphicsLoss()` invokes the actual
[ID3D12Device5::RemoveDevice](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12device5-removedevice)
diagnostic API when that interface is available, exercising the real removed-device
path. This marks that device unusable; it is not an OS/driver reset. The method
returns false when unavailable/not ready, and normal applications need not call it.
`simulateGraphicsFailure()` remains Automatic's separate terminal-fallback test.
Dawn retains its existing lost-device implementation and capture behavior.

## Mesh instancing

Direct rendering now consumes `RenderFrame::cameraBatches`, independent
`prepareShadow` batches and opaque World reflection batches. Compatible consecutive
opaque/masked parts share an indexed instance draw. Existing
`MeshComponent::allowInstanceReordering` defaults false; opt in only when changing
opaque equal-depth precedence is acceptable. The collector then sorts permitted
opaque runs to group bindings. Alpha/additive meshes retain individual sorted FX
submissions. They are never regrouped across transparency order.

Batches retain complete material bindings, geometry/ranges, winding, skin/morph
identities, layer and visibility decisions. Camera and shadow LOD/culling stay
independent. Shared skin/morph poses may instance together; independently animated
poses remain separate. Shadow caster participation splits batches; per-instance
receive flags, tint/opacity, model and inverse-transpose normal matrices come from
the existing 160-byte `ObjectUniform` in its initialized 256-byte slot. Deformation
precedes model transforms in both color and cutout shadows. Water and mirrored
reflection sources use the same instance path.

Each view owns a private default-heap object stream. `RenderFrame::prepareObjects`
supplies changed ranges; unchanged paused objects upload zero bytes. New/grown GPU
streams initialize the complete active payload. Ordered copies and retired buffers
share the existing fences. Camera identity/view-list changes and scene handoffs
invalidate streams. GPU streams never own scene nodes. Instance staging is capped
at 32 MiB per complete view candidate inside the combined 64 MiB frame arena;
existing descriptor/resource/draw bounds remain. These are budget limits, not
throughput guarantees. `capabilities().instancing` now reports true on direct D3D12.

Direct counters add `submittedInstances`, `instancedDrawCalls`,
`maxInstancesPerDraw` and `objectUploadBytes`. These count mesh submissions across
color, shadow and reflection passes; quad/ribbon counters remain separate. Direct
drawCalls also includes these passes, so compare scoped native counters rather
than assuming every backend's pass/clear counters are identical. Triangles retain
instance multiplication. No GPU-time speedup is inferred from draw-count reduction.

## Compressed texture uploads

Use the existing immutable `Texture::compressed` representation and
`RenderQuality::compressedTextures` (default true). Direct D3D12 queries BC7 linear
and sRGB format support, reports `compressionBC`, and chooses the first supported
BC7 variant matching the requested `TextureSemantic`. Color uses BC7 sRGB; data and
normals use linear BC7. ETC2 and ASTC variants retain RGBA fallback on D3D12.

All supplied variants validate before scene GPU publication, even when unsupported
or disabled. Existing contracts require a 4-aligned base up to 4096, exact complete
logical mips through 1x1, and 16-byte 4x4 blocks. D3D12 copy footprints supply block
row counts and row pitches, including non-square textures and sub-4x4 tail mips.
Each mip stages into its aligned footprint and copies to a typeless BC7 resource
with the matching typed view. No runtime encoder/transcoder is added.

RGBA fallback remains mandatory for disabled/unsupported compression, archives and
other backends. Semantic mip generation remains the uncompressed fallback. GUI
textures force the existing RGBA path and use separate cache identities, even when
the same CPU texture also has compressed scene variants. Toggling compression
fences outstanding work before invalidating semantic texture/material bindings.
Recovery rebuilds from retained CPU source variants and rechecks new device support.
See [compression contracts](rendering-quality.md#optional-compressed-textures) and
[mobile delivery bundles](mobile-assets.md).

`compressedTextureCount` and `gpuTextureBytes` describe cached semantic mip payloads;
`textureUploadBytes` counts newly uploaded payloads in the current frame. These
exclude staging row padding, GPU heap alignment and environment/GUI aggregate costs
unless those images are in this semantic texture cache; they are not total VRAM.

## Build and verification

```powershell
./scripts/build-windows.ps1
./scripts/windows-performance-smoke.ps1 -EvidenceDir build/evidence/windows-performance-new
ctest --test-dir build/windows -C Release -R '^(desktop_recovery|desktop_fallback|desktop_api|desktop_api_d3d12|instancing|render_quality|material|render_frame)$' --output-on-failure
```

The opt-in smoke compares instanced/individual color, shadows, complete materials
and HDR/MSAA; BC7/retained RGBA pixels and mip byte counts; real selected-device
removal; pause/running clocks; cancelled direct captures; removed cameras; resize;
GUI/font, water/contact, skin/morph/FX reuploads; and shutdown during loss. A separate
fake-factory unit suite covers backoff, failed acquisition, unstable-device limits,
explicit retry, exception classification and no resurrection after unbinding.
Run a separate Dawn-free build as well. Browser checks are
`render-quality-smoke.cjs`, `instancing-smoke.cjs` and `render-fallback-smoke.cjs`
against fresh pinned local Emscripten output, with explicit WebGPU/WebGL2 coverage.
See [milestone 8 evidence](windows-d3d12-milestone8-validation.md).
