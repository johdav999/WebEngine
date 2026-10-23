# Native Windows scene workbench

Build with `./scripts/build-windows.ps1`, then run
`./build/windows/Release/webengine_editor.exe [scene.wescene]`.
This is a native SDL/Dawn window using the shared C++ editor commands and retained
GUI; it does not require a browser or web server. Keep the staged assets and
SDL/DXC DLLs beside the executable. The player ZIP remains a player distribution;
the editor executable is a separate developer build target.

## Workflow

With `WEBENGINE_BUILD_MULTIPLAYER_DEMO=ON`, the server endpoint field and
**Multiplayer test** launch the isolated [multiplayer arena](multiplayer.md) in
a separate native client process. The authoring scene and history stay in this
editor. The launcher is unavailable without a host callback, and rejects normal
Play or pending document transitions. This does not upload or replicate the
current scene. The host registers `DesktopEditor::setMultiplayerLauncher`; its
captures must remain valid until cleared or the editor is destroyed.

**Host selected actor** and **Join authored scene** add the separate
[M2 scene session](multiplayer-scenes.md). Select a root mesh actor to publish the
supported static scene; Join receives the server's existing scene without exporting
the local source. `setMultiplayerSceneLauncher` receives synchronous source, views,
assets and selected-ID borrows; an empty ID means Join. The sample host serializes
the bounded package and launches `multiplayer_scene_windows`. Temporary packages
are consumed by that process. Source bytes, selection, dirty state and history
remain in the editor. Both Dawn and direct D3D12 use the same callback and protocol.

- Scene Outline selects actors/components, including disabled nodes. It shows up
  to 2048 nodes; deeper indentation is capped for readability.
- Click a mesh in the viewport to select its bounds. Drag with the left mouse to
  orbit; Frame selection and Zoom buttons navigate the authoring camera.
- Details edits local position, Euler rotation in degrees and scale as one atomic
  transform command. Enter or Apply commits all nine fields. Invalid values keep
  the previous scene. Registered component properties use their existing types,
  ranges and validation; choice fields show numbered choices and booleans accept
  `true`/`false`. Finish IME composition before committing.
- Cube/Sphere/Plane create an object through EditorSession; Duplicate, Add light/
  camera/collider/body and Remove component reuse existing history commands.
  Remove component deliberately does not delete actors. Undo/Redo retain up to
  128 commands. Structural/physics restrictions are the portable editor's rules.
- Import GLB loads a bounded 32 MiB embedded mesh file and places it through the
  existing editor command. Companion-image IO and a new import worker are not
  implied by this workbench action.
- New/Open and file drop validate a complete candidate. Dirty documents retain
  their source/history until Save, explicit Discard or Cancel. Saving writes an
  atomic `.wescene` archive; current assets and opaque application metadata remain
  in it. The browser editor reads/writes the same archive. The workspace viewport
  is temporary: serialization restores the authored viewport, then returns to
  the native layout. Camera navigation changes the saved camera pose.
- Play clones the document using SceneArchive and the configured component codecs,
  activates it in the host's existing Application, and disables authoring. Stop
  rebinds the retained source before unloading the runtime. There is one scheduler;
  Play never advances the source. Runtime motion cannot become an authoring edit.
- Window close protects dirty documents. Explicit `DesktopHost::requestClose()` is
  a programmatic force-close; pending editor transactions are cancelled before
  host shutdown releases scenes. F11 toggles fullscreen outside text input.

Use a desktop workspace of at least 1100 x 700 logical units. Outline and Details
scroll vertically. Native authoring is not the complete browser interface:
the new [shared authoring tools](editor-authoring.md) provide terrain stamps and
AI/animation/prefab/FX/cinematic forms and overviews. Advanced graph/curve/timeline
dragging, material graphs, viewport terrain strokes, docking, transform gizmos,
asset library browsing, autosave and arbitrary actor deletion remain extensions. Existing authored assets/components survive compatible
scene archives and supported properties remain editable. These are explicit
remaining parity gaps, not new implementations of the browser panels.

## Embedding and ownership

Link optional `WebEngine::DesktopEditor` (`engine_editor_desktop`) and include
`engine/editor_desktop.hpp`. Construct `DesktopEditor(host, document, codecs)` with
an unused DesktopHost, then call `editor.step(timestamp)` or `editor.run()` instead
of advancing the host separately. Default codecs include built-in AI controllers;
applications can supply their own existing ComponentCodecs. Custom callbacks and
registries must outlive their use. Failure to clone unsupported components retains
the source; no fallback silently drops application behaviour.

The host owns the source and Play scenes; the workbench owns their EditorSession,
assets, metadata, camera controller and bounded (32-command) UI queue. Camera views
are scene-scoped handles; raw RenderViews exist only during synchronous archive
operations. Removed authoring cameras are replaced with a new editor camera at
an idle boundary. GUI callbacks enqueue work; commands publish outside frame/GUI
traversal. Destroy the workbench before the host. It cancels input, clears handlers
and widget callbacks, and hides its retained GUI subtree, which is released with
the host. Do not shut down, replace or independently advance its Application while
the workbench is alive; requestClose is the supported programmatic exit.

See [parity validation](windows-parity-validation.md), [desktop host](windows-desktop.md)
and [portable scene editor](scene-editor.md).


Multiplayer [milestone 3](multiplayer-prediction.md) adds automatic local movement prediction and
server reconciliation to authored-scene Host/Join sessions on both runtimes.


Multiplayer [milestone 4](multiplayer-presentation.md) now interpolates remote players and smooths
local reconciliation corrections automatically in authored-scene Host/Join clients.


Multiplayer [milestone 5](multiplayer-replication.md) adds per-peer delta snapshots, periodic
keyframes, baseline recovery and distance interest filtering to authored-scene
Host/Join on browser and Windows. Rebuild scene server/clients together for MP05;
saved scenes and source editor documents remain compatible.


Multiplayer [milestone 6](multiplayer-authoring.md) adds saved ownership/relevance settings,
server-owned mesh actors and isolated-session diagnostics to both editors.
Scene clients/server now use MP06; saved archive and package envelopes are unchanged.

Multiplayer [milestone 7](multiplayer-components.md) adds registered custom
component factories, typed configuration and State replication (Off/Everyone/Owner
only) through shared editor history, duplication and save/load. Pass
EditorComponentSupport to EditorSession; DesktopEditor accepts optional property
registry and factory values after its codecs argument. Browser application setup
configures equivalent support before binding. Host/Join uses MP07 with WES3 component
manifests. Native authoring evidence uses EditorSession APIs and file round trips;
browser evidence uses actual fields. See [contracts](multiplayer-components.md) and
[validation](multiplayer-components-validation.md).


## Multiplayer runtime authoring and guest launch (M8–M10)

Both editor frontends expose Runtime prefab ownership and registered RPC/event
permissions, using shared properties/history/save/load. Existing body/collider
controls author the supported server-physics profile. Optional transient lobby
settings configure guest identity, room and invitation without changing the source
document. DesktopEditor stores settings; the example host supplies publication and
restricted inherited-pipe process launch. Client diagnostics include public roster,
recovery status, actual component state and body/event counters; capabilities are
excluded. See [prefabs/RPCs](multiplayer-runtime.md), [physics](multiplayer-physics.md),
[lobbies](multiplayer-lobbies.md) and [validation boundaries](multiplayer-runtime-validation.md).
