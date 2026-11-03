# Cloth milestone four

M5 adds optional edge/edge self-contact: see [cloth-edges.md](cloth-edges.md).

Verify the actual consumer revision before using `surfaceTriangles`,
`sweptCollision` or `ClothEnvironment::triangles`. Read engine-root
`docs/cloth-surfaces.md` and `docs/cloth-surfaces-validation.md` for contracts and
executed platform checks. Skill installation does not upgrade game dependencies.

- Author up to 2048 unique nondegenerate indexed garment faces; a positive
  selfCollisionDistance is required. ClothingItem faces must match mesh triangles.
  Shared vertices/seams are an authoring concern, not automatically welded.
- Vertex/triangle self-contact distributes response with barycentric inverse-mass
  weights. Pins win; incident/linked/rest-near neighbors are excluded. Collapsed
  simulated faces use edge fallbacks. Particle self-contact still works separately.
- Supply up to 256 borrowed, two-sided world-space environment triangles alongside
  planes/boxes/capsules. They are thin surfaces, not solid containment tests.
  Apps own transformed collision snapshots; renderer LOD never controls collision.
- sweptCollision defaults false. Enabled linear substep sweeps cover vertex/face
  self-contact and static-snapshot external triangles/capsules/boxes. Conservative
  searches are bounded to 64 queries; exhaustion can cause early false contacts.
- No edge/edge, inter-garment, swept particle-pair or moving-proxy CCD guarantee.
  Finite iterations, conflicting contacts and nonlinear motion can still intersect.
  Keep small meshes; published bounds are safety limits, not performance targets.
- Preserve afterSimulation/simulation delta, rigid driver transforms, atomic state
  publication and M2 reset/equip/persistence contracts. Surface/swept contact counters
  count projections, not unique impacts. No new clock, renderer or physics owner.

Run clothing_contracts, WebGPU/WebGL2 smoke and native Dawn/direct D3D12 smoke
separately. Record Windows compiler/runtime gaps explicitly; Wasm syntax checks
do not establish native support. Shared demos use the actual block mesh triangles.
