# Shared native and browser authoring tools

Build `webengine_editor` with `scripts/build-windows.ps1`, launch
`build/windows/Release/webengine_editor.exe`, and choose **Authoring tools**.
The six-tab frontend is drawn by the existing retained C++ GUI in the SDL/Dawn
window. The browser's `?editor=1` workspace also offers **Shared authoring tools**
using exactly the same panel and command implementations.

| Tab | Delivered workflow |
| --- | --- |
| Terrain | Create a 17×17 full-source terrain, enter local brush coordinates/radius/strength, stamp Raise/Flatten/Smooth/Paint/Hole/Fill (modes 0–5) and undo/redo through scene history. Color painting requires an existing compatible layered material; Hole/Fill are binary topology edits. |
| Animation | Open `.weanim` or capture a selected skeletal mesh, read/edit clip label/speed/loop, add clip states and exit-time transitions, assign to a compatible mesh, save and independently undo/redo. A state overview accompanies the forms. |
| AI | Read/edit/add states and named behaviors, add ordered conditional/delayed transitions, remove the last transition, assign through built-in binding validation, save/open `.weai` and independently undo/redo. Click an overview card to select a state. |
| Prefab | Open `.weprefab` or capture a built-in actor, inspect indexed definition nodes, rename/move nodes, capture another actor as a nested source, nest, place linked instances and explicitly publish revisions. Definition history is independent; publication uses scene history and retains supported instance overrides. |
| FX | Capture an effect or choose Campfire/Explosion, read/edit emitter rate/lifetime and size endpoints, place/assign immutable candidates, save/open `.wefx` and independently undo/redo. Assignment and placement use scene history. |
| Cinematic | Open/save `.wecin`, add all five existing track types, enter ranges/resources, record target poses/lenses at a time, select tracks in a timeline, remove tracks and undo/redo. Apply sampled track pose explicitly commits a transform to scene history. |

Indices start at zero. Enter numbers with a decimal point. Read buttons populate
forms from the chosen document item. Invalid candidates retain the last valid
resource/history; errors appear above the tab. The overview displays at most twelve
items; indexed fields address the complete bounded document. The cinematic timeline
displays at most twelve tracks, with vertical scrolling inside the page.

**Scene Undo/Redo** affects scene placement, assignments, terrain and sampled poses.
**Document undo/redo** affects the current tool document. Editing a document does
not alter existing scene assets until Assign/Place/Publish. Prefab publication
increments immutable revisions, including after a prior placement. Root placement
remains an instance override. Capturing prefabs retains existing built-in archive
restrictions; unsupported custom components are rejected, not silently removed.

Tool documents are saved separately from `.wescene`. **Open / replace tool
document** explicitly discards the current area's unsaved document after successful
parse/validation; other areas are retained. Native saves use atomic file writes
and mark only that area saved after success. Native New/Open/window-close protect
unsaved scene and tool documents. Cancel a pending close to save individual tool
documents, or explicitly Discard. Browser downloads retain the dirty flag because
a download request cannot establish that a file was persisted; before-unload warns
about unsaved tools. Browser uploads reject changed workspace generations, tab
changes and Play before adoption. File reads are bounded to 64 MiB and existing
archive resource limits still apply.

## Embedding

Link optional `engine_editor_gui`, include `engine/editor_authoring.hpp`, and
construct `EditorAuthoringPanel(gui, scene, editorSession)`. All three arguments
are borrowed and must outlive the panel. Use `setBounds`, `setVisible`, and call
`update(editable)` on the idle scene owner before rendering. Widget callbacks
enqueue at most 32 commands. `update(false)` disables the subtree and discards
queued commands, so commands cannot carry across Play or a file transition.
Do not update it from guarded scene/GUI traversal. Destruction clears callbacks
and hides its host-owned retained subtree before scene/session release.

`onFile(bool saving)` delegates platform IO to the host. `saveDocument()` returns
the current area's standard archive bytes; `extension()` returns the standard
extension. After successful external persistence, call `markSaved()`.
`openDocument(bytes, discard=false)` validates a complete candidate before replacing
the current area; explicit replacement is required when unsaved tools exist.
`dirty()` combines all independent document dirty states, in addition to the
separate scene session's dirty flag. Terrain is saved in the scene archive.

`DesktopEditor::authoring()` exposes the panel while idle. DesktopEditor owns and
updates it, recreates it when adopting another scene and disables it during Play
or pending document replacement. The existing Application alone owns source/Play
scenes. No browser, SDL, Win32 or renderer implementation types enter this module.

## Scope

This delivers a native frontend for each authoring area; it does not port every
HTML/SVG editor control. Browser graph dragging/edge wiring, AI task/blackboard
forms, retargeting/blend-tree/layer/IK forms, FX curve and trigger editors, cinematic
clip/key dragging and full isolated cinematic playback/scrubbing remain in the
specialized browser panels. Existing asset data is retained when these new forms
edit a supported field. Native scene Play previews assigned animation/AI/FX using
the normal runtime. It does not automatically play an open cinematic document.
Docking, transform gizmos, freehand viewport terrain strokes and editor autosave
remain native frontend extensions. The reusable core/asset contracts stay shared.

See [native workbench](windows-editor.md), [browser scene tools](scene-editor.md)
and [validation](editor-authoring-validation.md).
