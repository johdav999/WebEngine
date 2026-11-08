# Cloth milestones six to eight

Read engine-root `docs/cloth-materials-motion.md` and its validation report before
using these APIs. Verify the consumer revision; installing this skill does not
advance RPGGame, CafeGame or CatGame dependency pins.

- M6: `ClothDefinition::inverseMass` defaults empty/unit mass. Authored values
  cover every vertex and are finite [0.001,1000]. Zero maxDistance is the sole pin
  rule. Internal constraints and self-contact use mass weights; gravity does not.
- M7: `aerodynamicDrag` defaults zero/off, accepts [0,4], and requires existing
  surfaceTriangles (including their positive self-separation contract). Set
  `ClothEnvironment::wind` velocity (components +/-100) and density [0,10].
  Two-sided triangle pressure uses relative air/fabric velocity and area, equally
  distributed as force before inverse-mass conversion. Still air resists moving
  cloth; density zero disables drag. Normal speed and acceleration are capped;
  this is not lift, turbulence or a fluid solver. Author masses for mesh density.
- M8: optional `capsuleTranslations` / `triangleTranslations` spans match the
  supplied collider arrays. Shapes are end snapshots; translation runs from the
  last simulated endpoint to that snapshot, components +/-100. Keep ordering
  stable, retain motion until steps advance, and clear on reset/teleport/equip.
  Shared garments need aligned step/reset histories or separate updates.
  Translations split over executed fixed steps, including compressed catch-up.
  Enable sweptCollision for relative-frame sweeps. Friction uses collider-relative
  velocity; pins, the speed cap and last-projected-contact policy still apply.

No rotation/deformation history, moving planes/boxes, inter-garment collision,
reaction on rigid bodies or universal CCD guarantee is added. Existing scene,
rigid-transform, atomic candidate publication and transient-save contracts remain.
`aerodynamicFaces` and `movingContacts` are cumulative diagnostics, frozen while
paused and retained across reset. Advance only with simulation delta.

Both hosts use the weighted cape, aerodynamic wind and translating block. Run
clothing_contracts, both browser smoke backends and the native D3D12 smoke. Use
VS2022 with WEBENGINE_WINDOWS_DAWN=OFF / WEBENGINE_WINDOWS_D3D12=ON; the pinned
Dawn library needs a newer compatible MSVC runtime. Check the validation report
for actual results and limits, rather than treating common code as platform proof.
