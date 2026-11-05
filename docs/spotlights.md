# Spotlights in the RacingGame engine branch

This extension targets the consumer's `619ce5e` engine baseline. It is a small
spotlight implementation for that baseline, not the later integrated development
engine's shadow/HDR API. Do not transplant its archive version into newer branches.

`LightType::Spot` uses the ordinary `LightComponent` hierarchy and local emitted
`direction`. Configure `innerCone` and `outerCone` as half angles in radians;
defaults are 0.20 and 0.35. Require finite `0 <= innerCone < outerCone < pi/2`,
positive range, nonnegative finite intensity/color and a nonzero finite direction.
Cone width and range remain unscaled world quantities. The existing point-light
distance falloff is multiplied by
`smoothstep(cos(outerCone), cos(innerCone), dot(direction, -toLight))`.

WebGPU and WebGL2 apply this to ordinary materials and water direct lighting.
Ambient and unlit surfaces are unaffected. All light types share the existing
eight-light limit, in traversal order. Applications own prioritization. This
branch has no spotlight shadow maps, photometric IES profiles, volumetric beams,
or automatic exposure. `shadow.enabled` remains directional-only.

The inspector exposes Spot and degree-valued cone controls. Prefabs, duplication
and archive v28 retain both angles; readers still accept v1-v27. The CPU/WGSL
light payload is 64 bytes and the retained WebGPU frame cache is 2592 bytes.
Publish matching Wasm/JavaScript resources together.

Validated on Windows: native shadow tests; scene archive roundtrip and 300
structural mutations; editor property source compilation. The RacingGame
`vehicle-lights-smoke.cjs` consumer test exercises both renderers, four vehicle
meshes, road spill, braking, reversing and the shared light budget. This is desktop
browser evidence, not physical mobile acceptance.
