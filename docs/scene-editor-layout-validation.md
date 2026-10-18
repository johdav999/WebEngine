# Editor workspace validation

Recorded 2026-10-16. Scope: browser authoring UI, docking/resizing, Quick Add and
preservation of existing editor workflows. No renderer or portable editor ownership
model was changed. The primitive preset export composes existing mesh factories
and the existing undoable placement command.

## Build and environment

- Local pinned Emscripten **6.0.11**, CMake Release `build/web-editor-release`.
- Chrome **155.0.8059.80**, real WebGPU and forced WebGL 2.
- Desktop workspace: 1680×1000; existing regression suites: 1440×1040.
- DPR 1 and 2, plus 390-pixel compact workspace and existing narrow-screen checks.
- Initial isolated site: `build/editor-layout-site`, localhost port 8166.
- Final published site: `build/editor-layout-final-site`, localhost port 8167.
  Publishing also rebuilt concurrent terrain work. Its binaries are pinned for
  final regression independently of further source changes.
- Evidence: `build/evidence/editor-layout/`. The original build identity and
  final build identity are recorded separately. Source/host hashes accompany
  the final verification. The normal CMake `web_assets` target published to `dist`.

## Acceptance flows and issue ledger

Final results under `final/dpr1/` and `final/dpr2/` cover **180 checks**: for each
renderer and pixel density, 8 workspace checks, 14 transform checks, 14 content
checks and 9 structure/Play checks. All completed with empty recorded error lists.
The last cancellation-status text correction was rechecked in the workspace suite
at both densities; it does not change scene commands or the tested Wasm binaries.

| ID | Flow and acceptance | Result |
| --- | --- | --- |
| LAYOUT-01 | Reference arrangement, Scene Outline, Quick Add, Scene, Details and Assets; existing controls remain reachable | Verified in browser |
| LAYOUT-02 | Pointer/keyboard dock dividers, stacked heights, floating corner size, title drag/drop, Scene floating with identical canvas, Escape cancellation | Verified on both renderers at DPR 1/2 |
| LAYOUT-03 | Hide/Window restore, persisted reload, reset, malformed storage and compact overflow | Verified on both renderers at DPR 1/2 |
| LAYOUT-04 | Cube/Sphere creation by click/drop and undo/redo; selected-actor component shortcuts; Play gates | Verified with real Wasm commands |
| LAYOUT-05 | Existing transform, content, files, autosave/recovery, materials, hierarchy and Play/Stop workflows | Existing three suites passed at DPR 1/2 on both renderers |
| FIX-01 | Escape must cancel a layout drag before the editor shortcut handler consumes it | Fixed and covered by workspace smoke |
| FIX-02 | Moving a captured dock title must retain its drag until release | Reacquire capture after DOM reparenting; Scene float regression passes |
| FIX-03 | Narrower viewport makes rotation-ring hit strokes overlap | Pick nearest projected centerline instead of SVG paint order; real snapped Y rotation regression passes |
| FIX-04 | Snapping popover must not remain over the viewport after another action | Outside clicks dismiss menus; reload/recovery pixel comparisons pass |
| FIX-05 | Selecting low hierarchy rows keeps search visible; component shortcuts reflect the latest selection | Tree scroll is contained below search; shortcut gating refreshes after structural snapshots |

Early simultaneous browser-suite attempts encountered refused HTTP requests and
missed host scripts. Serial runs resolved those loading errors without product
changes. Early failures and diagnostic captures remain
separate from passing suite results.

## Reproduce

Build `webengine` / `web_assets` with the repository's local SDK. Stage matching
`web/`, the build's `site/`, fixtures and fonts. With Playwright/pngjs resolvable,
set `DEMO_URL`, `BROWSER_EXECUTABLE` and an isolated `EVIDENCE_DIR`, then run:

```text
node scripts/editor-layout-smoke.cjs
node scripts/editor-smoke.cjs
node scripts/editor-content-smoke.cjs
node scripts/editor-structure-smoke.cjs
```

Repeat with `DEVICE_SCALE_FACTOR=2`. Content/structure suites use the existing
native-generated fixture under `build/evidence/editor-content/`. The checks use
real renderer output and editor commands, not mocks. Both engine-skill copies
were updated and passed `quick_validate.py`; JS syntax checks passed.

## Boundaries

Panels float inside the browser, not separate OS windows. Docking uses four
stacking zones rather than an arbitrary nested/tabbed docking tree. Below 801 CSS
pixels the same tools stack with Scene first, and docking/resize handles are hidden.
Quick Add supplies Cube/Sphere/Plane plus existing component shortcuts; it does
not introduce additional primitive mesh types or arbitrary actor factories.
Existing exit/re-entry creates a fresh editor history; Play/Stop retains history
and selection as before. Browser layout storage is independent of scene saves.
Desktop Chrome and compact emulation do not establish physical Android/iOS
acceptance, other browsers, or sustained mobile performance. Native portable
tests were not rerun because the portable editor implementation is unchanged.
