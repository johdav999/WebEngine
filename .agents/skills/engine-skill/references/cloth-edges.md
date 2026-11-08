# Cloth milestone five

Verify the consumer revision before enabling `ClothDefinition::edgeSelfCollision`.
Read engine-root `docs/cloth-edges.md` and `docs/cloth-edges-validation.md`; installing
this skill does not update RPGGame, CafeGame or CatGame engine pins.

- The default is false. Enable with existing nonempty surfaceTriangles and positive
  selfCollisionDistance. The solver derives unique undirected edges; max 2048 when
  enabled. Actual ClothingItem faces must match mesh topology as required by M4.
- Existing shared/linked/triangle-adjacent/rest-near endpoint exclusions omit local
  pairs. Closest finite-segment parameters distribute correction over four endpoints
  by inverse mass; pins win. Parallel/collapsed geometry has deterministic fallbacks.
- Existing sweptCollision also enables linear four-endpoint sweeps, bounded to 64
  queries. Early false contacts can occur on search exhaustion. No global mesh CCD,
  inter-garment collision or moving external-proxy history is supplied.
- Edge response follows vertex/triangle self-contact and precedes external contact.
  Sweep-and-prune bounds rebuild each iteration; new contacts may wait until the
  next pass. Conflicts/finite iterations can still leave intersections.
- Preserve simulation delta, rigid driver transforms, atomic solver/render publication,
  reset/equip and transient persistence contracts. edgeContacts counts projections,
  contributes to selfContacts, and remains frozen during pause; reset retains counts.
  Keep collision meshes small: worst-case edge cost is quadratic, independent of LOD.

Run clothing_contracts and both WebGPU/WebGL2 smoke paths; validate native Dawn and
direct D3D12 independently. Shared hosts enable edge contact on the cape and check
paused counters. Do not claim visible edge collisions from a capture with zero edge
projections; deterministic crossing fixtures provide separate solver evidence.

Native retry guidance: VS2022 Build Tools works with `-G "Visual Studio 17 2022"`
and `-A x64` in a fresh build directory. Install pinned dependencies with
`tools/setup_windows.py`, enable clothing/tests/demo and Windows, build Release
`clothing_tests clothing_demo_windows`, then run CTest with `-C Release`.
The native contracts passed on the retry; consult the engine validation report
for renderer evidence. Shared shader embedding splits raw literals for MSVC's
size limit without changing shader contents; preserve this for both targets.

The pinned Dawn binary failed to link with MSVC 19.44 (missing newer STL symbols,
including __std_rotate). Use a compatible newer toolchain for Dawn; on VS2022 use
WEBENGINE_WINDOWS_DAWN=OFF and WEBENGINE_WINDOWS_D3D12=ON for the native demo.
Successful core contracts do not establish Dawn renderer acceptance.
The VS2022 direct-D3D12 demo build and native clothing smoke passed on the retry.
The fixed-height wardrobe menu can extend below the viewport at display scaling;
the smoke invokes commands directly rather than testing access to every button.
