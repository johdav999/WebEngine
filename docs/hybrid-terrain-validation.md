# Hybrid terrain milestone 1 validation — 2026-10-22

## Native Windows

Built Release with Visual Studio 18 2026 and the repository's installed SDL/Dawn/
direct D3D12 dependencies. CTest passed all six selected suites: `hybrid_terrain`,
`terrain`, `terrain_paging`, `terrain_page_edit`, `scene_archive`, `render_frame`.
The hybrid suite was subsequently extended and passed again for per-cell physical
surface remapping and authored mesh LOD/source collision independence.

Coverage includes a hole crossing four chunks, source/world queries, matching
physics, solid neighbours, invalid masks, all-hole chunks, no empty render draws,
source-only streaming, prepared patch validation, sculpt preserving masks, intact
terrain LOD, source sharing/legacy archives in existing suites, cave floor/roof/
ceiling/entrance and overhang queries, actual character-motor entry/exit, and hybrid
scene archive roundtrips. Rejected mesh indices and invalid LOD thresholds are tested.

`webengine_hybrid_terrain_windows --backend dawn --smoke ...` and the matching
`--backend d3d12` run both passed with real GPU presentation. Each traversed into
and out of the cave, checked floor contact, changed camera/detail, validated an
in-memory scene archive, paused/resumed and shut down. Four captures per backend
(overview, inside, coarse, returned) were produced at 1100×800. Pixel inspection
and `tools/verify_hybrid_terrain.py` passed: each overview/interior comparison
changed 687,993 pixels. This is visibility evidence, not a performance measurement.

Evidence: `build/evidence/hybrid-terrain/dawn/`, `d3d12/` and `native-pixels.json`.
Native smoke invokes the same actions as the retained buttons; browser acceptance
below additionally clicks the actual DOM controls. Physical input devices and
native device-loss recovery were not tested for this milestone.

## Browser

Built with repository-local Emscripten **6.0.11**. Chrome **155.0.8059.85** on this
Windows desktop passed `scripts/hybrid-terrain-smoke.cjs` with both explicitly
selected WebGPU and WebGL2. Actual buttons drive the motor through the entrance
and back out. Both runs checked floor elevation, rendered camera differences,
coarse detail, archive decode/query validation, resize and simulated device/context
loss recovery. Each reported no page/console errors. Captures were visually
inspected for the overview, cave interior and their native equivalents.

Overview/interior changed pixels: WebGPU 643,698; WebGL2 645,154. Evidence is in
`build/evidence/hybrid-terrain/browser/report.json` and adjacent screenshots.
The demo's in-memory archive probe verifies the restored scene's physics; it does
not replace the displayed scene or exercise a browser file picker.

The existing `scripts/terrain-smoke.cjs` also passed on both forced backends using
the newly rebuilt main host and matching worker staged in
`build/hybrid-regression-site`. Its version assertion was updated from stale v23
to v35. It verified textured hills, ground/collision agreement, rigid-body contact,
orbit/marker input, actual file download/open and fresh-page restore, malformed
archive preservation, heightmap import, reset and return/restart. Results and
captures are in `build/evidence/hybrid-terrain/regression/`. The optional native
archive interchange fixture was not supplied (`nativeArchiveChecked=false`).

Initial browser testing found and fixed an inline-handler collision with the
browser's `HTMLButtonElement.command` property; final acceptance uses the visible
buttons and the renamed `window.hybridCommand` handler.

## Artifact identity and limits

SHA-256 of tested artifacts:

| Artifact | SHA-256 |
| --- | --- |
| `build/web-release/hybrid-site/hybrid_terrain_demo.wasm` | `2350886C6635F5FADB457E003FF3A1E8FE973CBE71C9463A06BDD5EB38D4F435` |
| `build/web-release/site/webengine.wasm` | `D0D994B3D540B664E1CF2D45A6E2067E646455F03F656ED45B02949B739A75C0` |
| `build/web-release/site/asset_decode.wasm` | `01B6263A26D563AA7511A6B725FF18F1888B2F78E1009BD90293EFC447033E1E` |
| `build/windows/Release/webengine_hybrid_terrain_windows.exe` | `630FB77BB3B6FAEFB94D6BDECF11178EC983D7FC24F6F745731BC27C3CE5A20B` |

This establishes the desktop browser/native milestone on the current machine.
It does not establish physical Android/iOS, other GPUs/browsers, large-world memory
or sustained frame-rate acceptance. The demo uses authored static sections and
creation-time cell holes. Hole painting, runtime cutting, mesh simplification,
automatic seam welding and remote hole paging remain unimplemented. See the
[feature contract](hybrid-terrain.md) for build and reproduction commands.
