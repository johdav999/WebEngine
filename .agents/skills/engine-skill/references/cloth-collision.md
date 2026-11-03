# Cloth milestone three

For M4 vertex/triangle contact and bounded sweeps, read [cloth-surfaces.md](cloth-surfaces.md).

Resolve the actual consumer pin before using `ClothDefinition::selfCollisionDistance`
or the trailing `ClothEnvironment` argument to `ClothSimulation::advance` and
`ClothingInstance::advanceCloth`. Read engine-root `docs/cloth-collision.md` and
`docs/cloth-collision-validation.md`; installing guidance does not update a game pin.

- Self-contact is optional (distance zero disables, otherwise [.001,1]). It separates
  particles within one garment; authored links and bind-space-near pairs are excluded.
  Pins win. No triangle/edge, inter-garment or continuous collision guarantee.
- Supply borrowed world-space unit-normal half-space planes (max16) and solid AABB
  boxes (max32), alongside existing capsules. Positive allowed plane side is
  dot(normal,p)>=offset. Thickness expands solids; apps own proxies/discovery.
- Keep afterSimulation/simulation-delta ownership, rigid unit-scale transforms,
  atomic garment publication, stable dynamic streams and explicit reset from M2.
  Collider spans are not retained. There is no Jolt feedback or extra clock.
- Self-contact follows tethers; capsules, boxes and planes follow self-contact.
  Conflicting constraints can remain unresolved; discrete fast motion can tunnel.
  Use small shared-index garments; the 1024-particle bound is not a performance goal.
- Contact stats count projections, not unique contacts. Reset retains counters;
  equip/reload creates new solver state. Save item/color selection, not particles.

Run clothing_contracts plus the WebGPU/WebGL2 browser smoke script. The shared
Windows example supplies the same floor/block/cape and smoke contact check;
validate Dawn and direct D3D12 separately. Record unavailable native validation
explicitly, never infer it from Wasm syntax checks or portable source alone.
