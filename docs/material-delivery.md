# Unreal material delivery

AssetManager's `webengine` export carries the evaluated portable surface and an
explicit description of additional material behavior. It supports StaticMesh and
SkeletalMesh assets. Animation sequence exports remain GLB/FBX. Mesh-asset export
uses assigned asset slots; level/component overrides need their own scene context.

## Import and ownership

Use `GlbImporter::loadMemory` or native `loadFile` for either ordinary GLB bytes or
the `.webengine.zip` bundle. There is no implicit IO or ZIP extraction. Browser
model import, browser editor import and Windows editor filters accept the bundle.
The existing `AssetDecodeKind::Glb` worker path also accepts it. A failed import
must retain the current scene or mesh.

The bundle has exactly `asset.glb`, `materials.webengine.json` and
`export-report.json`. Only ZIP_STORED is supported. All texture bytes are embedded
in the GLB; no relative source library paths are resolved. ZIP bounds, member names,
CRC, metadata and report checksums are validated. The sidecar manifest must match
the root GLB `extras.WEBENGINE_materials` value. The extracted GLB can therefore be
imported on its own and carries the same rendering settings.

Version 1 binds entries by GLB material index, not names. Unknown versions,
duplicate/out-of-range indices, duplicate JSON properties, nonfinite values and
invalid parameter ranges reject. Diagnostics record source models and extraction
limits, with mapped/baked/approximated/unsupported status. Never execute source
HLSL, source paths, material names or exporter diagnostics.

## Material parameters

`Material::advanced` is a `MaterialAdvanced` value. Instance overrides use
`MaterialInstance::advanced`, replacing the complete advanced value. Shared
published assets remain immutable. `MaterialAdvanced::validate()` checks the
complete candidate before publication; `materialAdvancedParameters()` returns a
36-float renderer payload. Texture presence and all advanced values participate in
binding identity. Scene archive v40 persists advanced data, texture references and
overrides; readers retain v1–v39 defaults. Worker decode/hydration uses the same
archive transport.

| Field | Default and bounds |
| --- | --- |
| clearcoat / clearcoatRoughness | 0 / .1, each 0..1 |
| transmission / ior | 0 / 1.5, 0..1 / 1..3 |
| thickness / attenuationDistance | 0 / 0, 0..100 / 0..1000000; zero distance disables absorption |
| attenuationColor | linear white, RGB 0..1 |
| subsurface / subsurfaceColor | 0 / linear white, 0..1 |
| sheen / sheenColor / sheenRoughness | 0 / linear white / .5, 0..1 |
| anisotropy / anisotropyRotation | 0 / 0, -1..1 / finite bounded radians |
| specular | .5, 0..1 |
| uvSpeed | (0,0), each -100..100 UV units/second |
| windDirection / windAmplitude | (1,0,0) / 0; nonzero direction with amplitude >0, amplitude 0..10 local units |
| windFrequency / windSpeed | 1 / 1, 0..100 / -100..100 |
| clearcoatNormalScale | 1, validated nonnegative bounded normal strength |

The material shader enum remains Pbr/Unlit/Water. Manifest names clearcoat, glass,
subsurface, foliage, cloth and hair select PBR with explicit advanced factors.
Water requires an explicit recognized recipe and uses the existing WaterSettings.
Standard glTF extensions supply advanced values and maps when present; explicit
manifest parameters override them. Optional alphaMode/cutoff/doubleSided settings
preserve additive as well as opaque/mask/blend rendering.

Packed map fields use otherwise unused nonlayered texture slots, retaining browser
sampler budgets. `surfaceTexture` is linear RGBA coat/coat roughness/transmission/
thickness. `coatNormalTexture` is an OpenGL +Y tangent normal. `colorTexture` has
two vertical bands: top sheen sRGB color plus linear roughness alpha, bottom
subsurface sRGB color plus linear strength alpha. `anisotropyTexture` contains
linear encoded direction RG, strength B and specular mask A. Advanced maps and
the independent terrain MaterialLayers cannot be enabled together; flatten Unreal
surface layers during export instead.

## Rendering and fidelity

Browser WebGPU, WebGL2, Windows Dawn and direct D3D12 share these surface/effect
contracts. Clear coat uses a secondary GGX lobe; anisotropy modifies direct GGX;
skin/foliage use wrapped/backlit subsurface; cloth uses sheen. These are explicit
approximations, not copies of Unreal's shaders or a strand-hair renderer.

Transmission retains its glTF alpha mode, which may be Opaque. It renders after
the current view's opaque scene snapshot, with IOR-dependent reflection and
screen-space refraction/absorption. Missing scene inputs and reflected/offscreen
views use the documented environment fallback. This is a single-surface screen-
space approximation, not volumetric multiple scattering or correct multilayer
transparent-object refraction. Lighting, exposure and tone mapping still affect
appearance independently of material conversion.

UV panning and sinusoidal wind use the existing host-controlled material/water
clock. Hosts set or freeze it with the existing renderer/desktop time API. Wind
changes rendered vertices and tangent bases, including shadows; it never modifies
skin poses, geometry assets, collision, picking or gameplay state. Rendering bounds
expand by amplitude, and clustered selection is bypassed for wind-deformed draws.

Independent transforms on a compatible UV set are baked into bounded rectangular
maps over the primitive's UV bounds, preserving mesh topology. Differing UV sets
or incompatible tangent orientations use bounded triangle atlases when necessary,
retaining skin and morph attributes and reorienting tangent normals.
The importer reports atlas resolution and approximation limits. Texture-only UV
motion on an atlas is frozen with a warning because scrolling the atlas would
cross unrelated triangles. Ordinary shared-UV material panning remains animated.
Repeated texture sampling, very dense meshes, tiny atlas triangles and baking
resolution can reduce fidelity; inspect actual output rather than assuming parity.

Unreal graphs with Time/Camera/Reflection expressions may bake a fixed state.
Only restricted recognized Panner patterns and explicit wind recipes transfer
dynamic behavior. Arbitrary WPO, Custom HLSL, complex Substrate, Modulate blending,
clothing simulation and unsupported shader logic require specific conversion work
and explicit diagnostics. A source archive preserves Unreal data but does not make
that behavior executable in WebEngine.

## Verification

Run `material_delivery` and `material_delivery_archive` CTests alongside
`material`, `glb_importer`, `scene_archive` and relevant render-frame regressions.
The browser build also supplies `material_delivery_wasm.js` and
`material_delivery_archive_wasm.js` for Node, covering worker transport.
`?materialDeliveryTest=1` gates an acceptance-only browser fixture, with
`Module._engine_material_delivery_fixture(enabled, seconds, maps)` selecting deterministic
advanced/neutral states. The Windows lighting sample accepts
`--material-delivery enabled|disabled --material-time N --capture FILE` for the same
eight surface examples on `--backend dawn|d3d12`.

Record each backend separately. Compare neutral/enabled material images, animated
time changes and exact restoration. Test packed masks and actual static/skeletal
bundles through ordinary file import, then save/reload. See
`docs/material-delivery-validation.md` for this task's actual evidence and gaps.
