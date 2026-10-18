# Scene editor

Choose **Scene editor** in the playground or open `?editor=1`. The browser editor
uses the actual WebGPU/WebGL 2 viewport, with portable C++ editing commands and an
HTML/SVG tool interface. Edit mode pauses simulation while camera navigation and
rendering continue; exit restores the previous pause policy. It edits the displayed
scene directly. **Play** creates an isolated runtime copy; **Stop** returns to the retained authoring document.

## Camera navigation

Right-drag in Scene to look from a fixed camera position. While holding right mouse,
W/S fly forward/backward, A/D strafe, Q/E move down/up along the camera's up axis,
and Shift increases speed from 5 to 20 parent-space units/second. Diagonal movement
is normalized. Release right mouse, press Escape, focus a field, leave the window,
hide the page or enter a busy/file/Play transition to release navigation input.
W/E/R remain the transform-tool shortcuts outside navigation. No pointer lock is
required; captured drags work across panel boundaries, within browser screen limits.

Middle-drag pans, Alt-left-drag orbits, Shift+Alt-left-drag pans, and scroll zooms.
Double-click an outline node or press F outside text fields to frame the selection.
Framing includes active descendant mesh bounds and the current skeletal pose;
meshless or inactive nodes use their position. It retains the viewing direction,
fits the current viewport aspect and perspective/orthographic lens with a margin,
expands the far clip if needed, and sets the orbit pivot. Framing is disabled during
Play; mouse navigation then affects only the isolated runtime camera.

Navigation does not add object undo commands or dirty revisions. Saving a scene
still includes its current authoring camera and orbit state. Play/Stop preserves
that source view. Docking and resizing reuse the same input canvas.
Portable `OrbitController::look`, `translate` (positive local Z is forward), and
`focus` use its camera-parent coordinate system. Optional `orbit` elevation and
`zoom` distance limits retain the previous defaults for non-editor callers.
Run native `input`, `camera`, `camera_controller` and
`scripts/editor-camera-smoke.cjs` on both backends, including DPR2; see
[camera validation](scene-editor-camera-validation.md) for recorded evidence.

## Dockable workspace

The editor fills the browser with **Scene Outline** and **Quick Add** on the left,
the real **Scene** viewport in the center, **Details** on the right and **Assets**
below the scene. Existing component/material/structure forms live in Details;
asset import, placement and autosave/recovery live in Assets. File, history,
transform, snapping and Play/Stop controls remain in the top bars.

Drag a panel title to float it within the workspace or onto a highlighted Left,
Center, Right or Bottom target. The title's docking menu offers the same locations
without dragging. Panels stack within each dock. Drag the dock dividers to change
column widths and bottom height; drag a panel's bottom grip to change its share of
a stack, or its lower-right grip to resize a floating panel. Focus a resize grip
and use arrow keys for keyboard resizing. Escape, pointer cancellation, window blur
or page hiding rolls back an unfinished panel gesture.

Close a tool panel with **×** and restore it through **Window**. Scene stays
available. **Window → Reset layout** restores the default arrangement. Layouts
are validated and stored separately in origin-local `localStorage` under
`webengine.editor.layout.v1`; storage failures keep the current session usable.
Layout changes never edit scene bytes, selection, dirty revision or history.
Floating panels stay within this browser workspace, not separate OS windows.
At widths of 800 CSS pixels or less panels stack with Scene first; desktop docking
and resize grips are hidden while tool controls, scrolling and Window remain usable.

Quick Add provides Cube, Sphere and Plane: click then place on the viewport's Y=0
ground plane, or drag a tile into the viewport. Escape cancels placement. Creation
uses `EditorSession::placeMesh`, including snapping, dirty state, saved identities
and one-command undo/redo. Light, Camera, Mesh, Collider, Body and Interaction
shortcuts attach supported components to the selected actor through the existing
component command (Mesh uses the selected asset). Existing topology restrictions
still apply. Quick Add is disabled during Play, staging and transform gestures.

## Selection, transforms and navigation

Select a hierarchy row or click a visible World mesh in the viewport. Ctrl/Cmd-click toggles membership; Shift-click selects an ordered hierarchy range. The last selected node is the primary inspector target. Hierarchy
selection includes disabled nodes; its filter matches names/types, and Up/Down,
Home/End and Enter/Space navigate rows. Viewport selection uses CPU mesh bounds
and the first render view's actual backing-pixel camera viewport. It does not
evaluate texture alpha, triangle gaps, shader displacement or multi-view pixel ownership.

The transform inspector edits local position, Euler rotation in degrees (`Rz * Ry * Rx`),
and scale. **Move / W** draws world X/Y/Z arrows; **Rotate / E** draws world-axis
rings about the selection's mean root position. **Scale / R** draws axes in the first effective root's local orientation and a center square for uniform scale. Dragging changes positive scale exponentially from the initial pose; the uniform handle also changes spacing about the shared pivot. Axis scaling under differently oriented/nonuniform parents may require shear and is rejected; uniform scaling is the reliable option for such groups. Gizmos convert world edits back to local TRS. Shear
which cannot be represented under a nonuniform parent keeps the last valid pose.
Overlapping rotation-ring hit areas select the nearest projected ring centerline,
so changing the viewport aspect does not make SVG paint order choose the axis.
Dashed screen bounds cover the selected roots. When both ancestor and descendant are selected, the ancestor covers that subtree once. Move, rotate, scale, duplication and component removal commit one command for the whole effective selection. Numeric TRS fields are disabled for multiple selections; property/material inspectors edit the clearly identified primary node. No marquee selection is provided.

Alt+left drag orbits, middle drag or Shift+Alt+left drag pans, and wheel zooms.
The existing arrow-key camera controls work outside text fields. One gizmo drag
is one history command. Escape, blur, resize, hidden-page, pointer cancellation,
lost capture, selection changes and leaving the editor cancel uncommitted drags.

**Snapping** independently enables a world translation grid and a rotation angle.
Grid size defaults to 1 world unit and angle to 15 degrees; both are initially off.
Move handles snap the selected axis's absolute world coordinate. Rotate handles
snap the rotation delta from the initial pose. Placement snaps the ground anchor;
duplication snaps its offset world origin. Numeric transform fields remain exact
local edits. Invalid grid/angle settings preserve the last accepted configuration.

## Asset import and placement

**Assets → Import assets** accepts GLB, FBX, PNG/JPEG/BMP/TGA images, and `.weassets`
libraries. File reading is asynchronous; existing bounded importers decode on the
owner thread and may temporarily pause the interface for large inputs. Models and
images have a 32 MiB source limit; libraries retain the archive's 512 MiB/budget limits.
GLB/FBX preserve imported units and geometry. Embedded textures are decoded by the
importer; FBX companion image files are not implicitly fetched from disk or URLs.
Import such images separately and assign them through the material inspector.

The catalog exposes mesh and texture entries; other library resources are retained
without specialized placement controls. Each successful file is one undoable import;
unplaced resources are retained in the scene's explicit `AssetLibrary` and survive
save/recovery. A failed file changes neither the catalog nor the scene. Multi-file
imports publish each successful file independently and report individual failures.
Import completion checks the document generation before publishing source bytes.

Choose a mesh, enable **Place in viewport**, then click the authoring ground plane
at world Y=0. Placement retains unit asset scale and offsets by the source bounds'
minimum Y so the mesh rests on that plane. It does not raycast arbitrary surfaces
or add colliders automatically. Escape or toggling the tool cancels placement.
Dropping a model file onto the viewport imports it and places it at the drop's
ground-plane position. Imported images enter the texture catalog instead.

## Terrain editing

Choose **Quick Add → Terrain** for a flat 64 × 64 m terrain with 65 × 65 samples,
four colored surface layers and static collision. Creation selects and frames the
terrain. **Details → Terrain → Create terrain** changes width/depth (1–512 m),
resolution (33, 65 or 129 samples per side), collision, heightmap and height range.
Use **Assets → Import assets** to import a heightmap image, then choose it in the
creation options. Its decoded red channel maps from 0 to the chosen height range;
nearest source pixels are sampled into the selected resolution. Image rows follow
local +Z, with no flip or gamma conversion. Existing 8-bit image precision applies.

Select a terrain actor or one of its chunks in Scene Outline, then choose a brush
under **Details → Terrain**:

- **Raise / Lower** adds/subtracts the strength in local metres per stamp.
- **Flatten** blends toward the chosen local height.
- **Smooth** blends toward the neighboring source samples.
- **Paint surface** blends toward one of the four existing material layers.

Radius is measured in source-local terrain metres. Strength is 0.01–1 per stamp.
The projected ring follows the actual terrain surface. Drag left mouse to brush;
stamps follow movement at 20% of the radius or repeat after 100 ms while held.
Right mouse + WASD, middle pan and Alt orbit remain available. Select **Select /
navigate** or use W/E/R to return to object transforms.

One stroke is one shared undo command, including multiple chunk boundaries.
Preview updates geometry, ground queries and optional collision; mouse release
commits the stroke and dirty revision once. Escape, blur, hidden page, pointer
loss, busy/Play transitions or selection/document changes cancel the preview.
Painting changes weights without rebuilding geometry/collision. Chunk identities,
transforms, detail policies and ordinary scene history are preserved.

Complete eager terrain instances are editable; source-only streams, partial
instances and paged patches remain owned by their stream and report a read-only
reason. Chunk-local transforms must be identity and collision bodies static.
Painting requires an enabled layered material. The new-terrain presets use Grass,
Sand, Rock and Soil colors; existing terrain retains its authored layers.

**Paint layer textures** provides one texture selector for each layer. Import
images through **Assets → Import assets**, select the terrain actor or any of its
chunks, then assign the imported image to the desired layer. **None (layer color)**
clears its image. Assignments apply to every chunk of that terrain and are one
undo/redo command each. Reassigning the same texture adds no command. Other terrain
instances sharing the original source remain unchanged.

Textures multiply the layer's existing color and use its existing UV tiling;
assignment preserves roughness, paint weights and other layers. Painting later
retains the assigned textures. Layer textures use the engine's existing 4096-pixel
per-axis material limits and decoded image validation. These albedo textures work
on WebGPU and WebGL2; per-layer normal maps are not part of this feature. Controls
are disabled during a stroke, busy operations and Play. Save, autosave/recovery
and Play/Stop retain bindings through the existing terrain/material codecs.

Scene files, autosave/recovery and isolated Play use the existing terrain codecs.
History retains immutable source versions in the session's bounded command list;
editing/rebuilding is synchronous and has no worker-time budget. Imported large
terrains can cost more than the bounded creation presets. The terrain demo pauses
its own streaming/brush writer during editor mode; returning adopts completed
source edits and clears its separate stale stamp history.

Portable APIs: `EditorSession::createTerrain(TerrainData,Vec3 origin,bool collision)`,
`terrainActor`, `terrainReadOnlyReason`, `beginTerrainStroke`, `stampTerrain`,
`commitTerrainStroke`, and existing `cancel`/`undo`/`redo`. Brush coordinates and
creation origin use terrain-local / scene-local coordinates respectively. The
session borrows its idle scene as before. Run CTest `editor_terrain` and existing
editor/terrain tests, plus `scripts/editor-terrain-smoke.cjs` on both backends at
DPR1/DPR2. See [terrain editor validation](scene-editor-terrain-validation.md).
`EditorSession::setTerrainLayerTexture(layer,texture)` accepts layer indices 0–3
and a shared immutable texture (null clears). It uses
`TerrainAsset::withLayers(MaterialLayers)` to stage appearance-only edits without
rebuilding geometry or collision. Run `scripts/editor-terrain-textures-smoke.cjs`
for actual texture import/assignment, rendering, clearing, history and scene
roundtrip on both backends/DPRs; see
[layer-texture validation](scene-editor-terrain-textures-validation.md).

## Component and material inspectors

Select an actor and use **Inspect component** to inspect its owned components, or
select the component directly in the hierarchy. The inspector is generated from
typed C++ property descriptors with keys, labels, groups, ranges, choices, getters
and validated setters. Supported built-ins include:

| Type | Editable properties |
| --- | --- |
| Nodes, actors and components | Name, enabled state, actor/component variable ticks and component fixed ticks |
| Meshes | RGB tint, opacity, render layer, casting/receiving shadows and culling |
| Lights | Type, RGB color, intensity, range, local direction and shadows |
| Cameras | Perspective/orthographic mode, FOV, orthographic height and clipping planes |
| Rigid bodies | Motion type, mass, friction, restitution, damping, gravity factor, sensor and continuous collision |
| Character motors | Radius, half height, jump speed, gravity, step height and ground-snap distance |
| Interaction providers | Selectable and focusable flags |

Particle effects have a dedicated [FX authoring section](fx-authoring.md).
Other components expose common fields; asset definitions and advanced animation,
collider-shape and water-specific settings are not generated
inspectors in this increment. Component creation/removal uses the supported types below.

Scene roots, managed implementation subtrees and the browser navigation camera are
protected. Body/motor component **transforms** stay read-only while their supported
settings remain editable. Position physical objects through the owning actor.
Physics edits reconcile without simulation ticks; configuration changes can rebuild
solver resources and are not an exact solver-history rewind.

Select a mesh for **Materials**. Each slot exposes linear base/emissive RGB,
roughness, metallic, opacity, normal/occlusion strength, alpha settings, double-sided
state, shader and UV transforms. **Texture maps** assigns imported/catalog textures
to base color, metallic/roughness, normal, occlusion or emissive slots, or clears them.
Opacity blends with Blend/Additive alpha modes. **Reset selected slot** removes
the instance override, with undo restoring it. Edits publish immutable per-instance
material bindings, preserving shared source assets, existing maps and unedited
layer/water parameters. Existing WebGL 2 PBR/shadow capability reductions still apply.
The existing standalone material graph remains separate.

## Component creation, removal and hierarchy

**Scene structure → Add to actor** attaches one of Component, MeshComponent,
SkeletalMeshComponent, CameraComponent, LightComponent, ColliderComponent,
RigidBodyComponent, CharacterMotorComponent or InteractionComponent. Select one
actor or one of its components first. Mesh creation uses the mesh selected in
**Assets**; light defaults to a point light, and collider defaults to a half-unit
box. Physics types must be direct children of a top-level actor; duplicate physics
components and unsupported topology/configuration are rejected.

**Remove components / Delete** removes selected component roots and their owned
subtrees. Scene/actor removal is not exposed by this command. Managed skeletal
implementation children and every camera subtree borrowed by a render view are
protected. Removed nodes stay owned by bounded undo history, allowing exact saved
identity/configuration restoration with fresh runtime handles when reattached.

Drag selected hierarchy rows onto a destination node, or select **New parent →
Reparent selection**. The transaction preserves world poses, validates the whole
selection and rejects cycles, protected nodes, invalid physics topology and
unrepresentable TRS/shear. Undo restores local transforms, original parent and
sibling order. Sibling reordering within the same parent is not a separate UI tool.
`Node::setChildIndex` is the idle portable ordering primitive used by replay.

## Isolated Play and Stop

**Play** builds a separate bounded scene with independent mutable nodes, animation,
physics, particles and Application clocks. Immutable mesh/material/texture/rig assets
remain shared. Saved identities can match across the two documents, while runtime
handles are scene scoped. Runtime physics, animation and sky time advance normally;
the editor's authoring Application remains paused. Canvas camera navigation operates
on a separate copied camera/controller. Host suspension freezes preview clocks and
drops the hidden-time baseline.

**Stop / Escape** rebinds the original render views before destroying the preview.
The source scene, camera, selection, dirty checkpoint, imports and undo/redo history
remain retained. Runtime changes are discarded; there is no apply-back operation.
Authoring, imports, file/recovery capture and document replacement are gated during
Play. An outstanding editor autosave is aborted; the prior recovery remains intact.
Creating or stopping a preview changes no authoring revision or file checkpoint.

`EditorPlaySession` uses `SceneArchive::clone`'s in-memory reference-only resource
transport; it does not round-trip image/geometry bytes or change the archive format.
Unsupported custom node classes fail before preview publication. This built-in preview
does not reconstruct host/game callbacks, input bindings, physics joints, GUI trees,
Web Audio voices or demo-specific motion policies. Applications needing those
behaviors must explicitly construct them for their own isolated runtime. The editor
currently supplies camera navigation and built-in simulation, not a script debugger.

## Duplication and history

**Duplicate / Ctrl/Cmd+D** copies each selected supported root hierarchy beside its
source with a world-X offset (one unit, or the enabled grid size) and a unique copy
name. Every saved node identity is regenerated, including managed descendants.
Immutable mesh, material, texture, rig and other assets remain shared; transforms,
overrides and playback state are independent. Physics actors register separate
bodies. Scene roots, independently selected protected children and unregistered
custom node subclasses are rejected before publication. Runtime callbacks, joints
and custom executable behavior are not cloned. Duplicate the owning physics actor
instead of trying to add a second active body/collider component to the same actor.

Undo/redo also covers immutable effect-asset replacement and effect creation.
Undo/redo covers terrain creation/strokes, transforms, properties, material bindings, asset-library imports,
placement, duplication, component creation/removal and hierarchy reparenting. Ctrl/Cmd+Z undoes; Ctrl/Cmd+Shift+Z or Ctrl+Y redoes.
Text fields retain browser text-edit shortcuts. History defaults to 128 commands;
new edits discard the redo branch. Creation undo parks the owned subtree in history;
redo reattaches it with fresh runtime handles and the same new saved identities.
Pending removal, parent/transform changes and conflicting supported state invalidate
replay rather than overwriting a later writer. Old history can retain resources
until its commands expire or the editor session closes.

## Files, autosave and recovery

**Save scene / Ctrl/Cmd+S** downloads a self-contained `.wescene`; successful byte
download path marks the editor's file checkpoint clean and reports download started.
Browser download completion is a separate host action. **Open scene** stages and
validates archive/audio/host metadata before publication. Malformed files preserve
the current document. Opening or leaving/re-entering the editor starts fresh history.
Imported resources, authored node/component/material data and saved identities persist.
Selection, gizmos, history and runtime handles do not.

Asset source labels and snapping preferences are stored in the existing opaque
application metadata envelope. No archive format version was added for this editor
increment. Scene replacement disposes the old session before its borrowed scene,
camera or explicit library can be released; replacement sessions bind after their
document metadata has moved into its final owner.

**Autosave & recovery** enables automatic local recovery by default, with a
three-second debounce (configurable from 1 to 300 seconds) after committed edits,
undo and redo. It captures no unfinished drag. IndexedDB namespace `scene-editor`
contains one `latest` recovery envelope with document/name/timestamp and scene bytes.
The adapter acknowledges success only after the storage transaction commits.
Autosave does not download a file or clear the file's dirty state. **Save recovery
now** explicitly captures the current document even if automatic autosave is off.

**Recover autosave** explicitly opens the last committed snapshot through the same
staged loader, then marks it unsaved to a separate scene file. Reloading the page
offers recovery; it never silently replaces the default scene. Failed storage writes
leave both the current editable scene and previous committed checkpoint intact;
the status explains the failure and the explicit save button retries. Malformed
recovery data cannot replace the active scene. Turning autosave off retains recovery.
One slot is shared by editor pages on this origin: the last committed writer wins.
Browser storage quotas/clearing and interruption before a transaction commits limit
recovery; page shutdown does not guarantee a final asynchronous save.

## Portable APIs and boundaries

Link optional `engine_editor` for `EditorSession`, `EditorSnap` and
`EditorPropertyRegistry` (`editor.hpp`, `editor_properties.hpp`), and `EditorPlaySession` (`editor_play.hpp`). A session borrows an idle, non-ticking scene and optional
stable-address `AssetLibrary`; it must be destroyed before either owner. Hosts own
simulation pause policy. Property registries are immutable shared registrations;
captured callback dependencies must outlive their registry/session. Custom setters
are trusted application code, not a transaction boundary for external side effects.

```cpp
engine::EditorSession editor(scene, 128, &library);
editor.select(actor.savedId());
editor.setProperty("node.name", std::string("Warehouse"));
editor.setSnap({true, true, 0.5f, engine::pi / 12});
editor.duplicateSelected();
editor.undo();
editor.redo();
```

Use `setTransform`, or `beginTransform`/`previewTransform`/`commit`/`cancel`, for
transform transactions. `beginGizmo`/`dragGizmo` use the same path. `setProperty`,
`setMaterial`, `clearMaterial`, `importAssets`, `placeMesh` and `duplicateSelected`
publish typed commands. `selectMany`, `toggleSelection`, `selectionIds` and
`selectionRoots` maintain an ordered selection and ancestor reduction.
`addComponent`, `removeSelectedComponents` and `reparentSelected` publish validated
structural commands. `reparentSelected` defaults to KeepWorld; portable hosts can
explicitly choose KeepLocal. `EditorPlaySession::start` stages a copy, `advance`
uses ordinary Application scheduling, `setSuspended` preserves clock policy and
`stop` releases runtime ownership after the host releases borrowed presentation.
`revision()` changes on committed mutation/undo/redo;
`markSaved()` tracks the file checkpoint and `markRecovered()` preserves recovery's
unsaved-to-file status. `SceneArchive::duplicateSubtree` reuses built-in node encoding
with an in-memory table of shared resource references; it does not serialize mesh
pixels/geometry or define a new on-disk format.

`web/editor-layout.js` and `.css` own browser workspace docking/resizing and layout
storage. Existing controls and the canvas are moved, never recreated. The browser
primitive preset export composes existing portable mesh factories and placement.
`src/editor_browser.*` supplies browser exports and snapshots. `web/editor.js` owns
selection/transform gestures; `web/editor-content.js` owns content controls, local
file reading, metadata and recovery; `web/editor-structure.js` owns component/reparent and Play/Stop controls. DOM/SVG never owns authoritative scene data.
Document generations reject stale file-read completions, abort outdated storage
writes and suppress stale status completions. Native logic has no browser dependencies.

## Validation and continuation

Run `scripts/editor-layout-smoke.cjs` against the matching staged Release host and
Wasm, with `DEMO_URL`, `BROWSER_EXECUTABLE` and optional `DEVICE_SCALE_FACTOR=2`.
It covers both renderers, docking/resizing/cancellation, layout reload/reset,
Quick Add/history, Play and compact layout. Run the three existing editor suites
as regressions; see [workspace validation](scene-editor-layout-validation.md).

Build `editor_tests`, `editor_content_tests`, `editor_structure_tests`, `scene_archive_tests` and
`persistence_tests`, then run their CTest entries. `editor_content_tests <scene path>`
also writes a small native scene and `.weassets` fixture for browser interchange.
Run `scripts/editor-content-smoke.cjs` with Playwright/pngjs, `BROWSER_EXECUTABLE`
and `DEMO_URL` set to the exact staged build. `DEVICE_SCALE_FACTOR=2` repeats high-DPI
input checks; `EVIDENCE_DIR` changes the output directory. The first-increment
`scripts/editor-smoke.cjs` remains the transform/input baseline. See
[content validation](scene-editor-content-validation.md) and the historical
[first-increment validation](scene-editor-validation.md).

Run `scripts/editor-structure-smoke.cjs` for real component creation/removal,
modifier/range selection, drag/drop reparenting, grouped history, scale handles,
physics rejection and byte/pixel-identical Play/Stop restoration on both renderers.
Repeat at `DEVICE_SCALE_FACTOR=2`. See [third-increment validation](scene-editor-structure-validation.md).

Further increments can add arbitrary actor creation/removal, advanced component
factories/inspectors, marquee selection, explicit sibling ordering, apply-back Play
changes, custom runtime reconstruction and editable prefab definitions.
Extend the existing typed commands/property registry rather than adding mutations
which bypass history, dirty revision or staged loading. Preserve shared immutable
assets, saved-vs-runtime identities and document-generation cancellation.


## Vehicle foundation and archive v24

See [vehicle contracts](vehicles.md) for the optional four-wheel component,
COM/inertia and point-force body APIs, typed editor/preset/Play support and supported
checkpoint state. Current scene/application/effect envelope writer version is 24;
older formats remain readable. Vehicle configuration/checkpoints and visual saved
references persist; commands, solver listeners and contact history do not.

## Graphical AI state machines

Open **Details → AI state machines → Open AI graph editor** to author states,
connections, named behavior/condition bindings, priority and delays. Draggable
nodes have independent undo/redo and `.weai` files. The isolated preview runs the
actual C++ state machine with controllable conditions, leaving scene bytes and
history unchanged. **Use in AI demo** validates application bindings for a live
demonstration after leaving edit mode. See [AI state-machine contracts](ai-state-machines.md)
and [validation](ai-state-machines-validation.md). Graph files are saved separately
from scenes and are not automatically attached to arbitrary actors.

The browser vehicle example now explicitly reconstructs its application-owned
driving audio callbacks/proxy scene during isolated Play, with Stop/unbind
cleanup. Details also provides isolated rate preview and separate tuning history/
files; these do not add scene audio codecs. See [driving audio](driving-audio.md).

## Actor AI assignments and live Play debugging

**Details → Actor AI controller** assigns the open graph to a selected actor,
selects registered behavior/condition bindings and edits schema-generated typed
parameters. Missing bindings are listed. Assignment/configuration uses scene
Undo/Redo and persists in normal scene files through the optional AI component
codec. **Live debugger during Play** highlights the selected actor’s active state,
elapsed time and recent transitions in the isolated runtime; Stop retains source
bytes/history. See [controller workflow and contracts](ai-controllers.md) and
[validation](ai-controllers-validation.md).

Route source authoring, grouped rebuild history, point/tangent preview, physical
profile selection and explicit terrain point baking are now in Details. New route
actors use protected static collision children; generic manipulation of those
children remains read-only. See [splines/routes](splines-routes.md).
