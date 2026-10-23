# Accessibility and pointer input

DesktopHost automatically exposes its retained GUI through Windows UI Automation
on both Dawn and direct D3D12. Browser hosts retain the semantic DOM bridge on
WebGPU and WebGL2. Applications supply meaningful names/descriptions, mark
decorative subtrees accessibility-hidden, and opt changing text into live regions.

## Semantic controls

The native HWND owns a fragment root with a flat, ordered list of semantic
controls, matching the existing portable semantic snapshot. Stable widget IDs
become UIA runtime/automation IDs. Buttons expose Invoke, checkboxes Toggle,
single-line textboxes Value and sliders RangeValue. Labels, images, named groups
and scroll regions expose their roles/names. Focus, enabled state, descriptions,
clipped screen-space bounds and polite live regions come from GuiSystem.
Accessible focus reveals scrolled controls through the existing GUI path.

`GuiAccessibleNode::clippedBounds` and `keyboardFocusable` are copied portable
metadata; hooks cannot override geometry/focus eligibility. `bounds` remains the
full logical layout rectangle. An empty clipped rectangle means offscreen.
Neither field changes simulation, layout, rendering or persistence.

The private Windows provider owns synchronized snapshots and COM identities, never
Widget/Scene pointers. Queries may run on Windows worker threads. Actions enter a
bounded 128-command queue and execute on the DesktopHost owner at the next poll,
before Application advancement. Success means accepted for dispatch. Current
IDs, visibility, enabled state and slider ranges are checked again before applying
actions. Lifecycle cancellation discards queued work, including a pending batch
when an earlier callback changes host state. Text edits accept at most 32 KiB of
valid UTF-8 after UTF-16 conversion and normalize line breaks/tabs to spaces.

Focus, live text, structure, name, enabled, textbox value, checkbox and slider
changes raise UIA events. Removed providers disconnect; host teardown removes the
window hook and disconnects retained providers before GUI/window destruction.
The adapter links Windows UIAutomationCore/COM privately; public headers contain
no Windows or SDL types. Existing clipboard and SDL IME services remain the
native text owner. Browser DOM/text services retain their existing ownership.

Native scope is single-line Value editing, not UIA TextPattern/text ranges,
rich documents, ScrollPattern or an accessibility compliance certification.
Native actions are gated while the host is unfocused, minimized or suspended;
restore/focus the window before interacting. Render-only scene objects have no
automatic semantic representation; applications expose relevant actions in GUI.

## Mouse, pen and multiple contacts

Native SDL finger events map `(touch device, finger ID)` to distinct positive
engine pointer IDs; 64-bit SDL IDs are never truncated into the portable ABI.
Pen IDs retain identity from hover through contact and proximity loss. Mouse ID
1 is reserved. Up to 32 native non-mouse contacts are tracked. Pressure is retained
in `[0,1]`; touch coordinates convert from normalized window units to logical and
backing coordinates, and pen coordinates use the same DPI conversion as mouse.
Pen tilt/rotation and platform-specific pen axes are not part of InputPacket.

Portable button bits match browser PointerEvent: left/tip=1, right/barrel1=2,
middle/barrel2=4, mouse extra buttons=8/16, pen eraser=32. Pressure-axis events
update pen records, including hover. Pen proximity loss and SDL finger cancellation
emit PointerCancel. Synthetic SDL touch/pen mouse events and pen-as-touch events
are filtered so one physical action cannot click twice.

Each native contact retains its GUI, scene or ignored owner until release or
cancellation. GUI has one capture owner shared with mouse; a second contact cannot
replace a GUI drag. Other scene contacts retain independent records. Touch and pen
activate ordinary buttons, sliders, text selection and scrolling through the same
portable GUI pointer path. Blur, pause, input/context cancellation, scene handoff
and teardown clear contacts; stale finger moves/releases cannot reactivate them.
Reentrant GUI callbacks can cancel the contact being dispatched safely.

Applications consume ordered InputPacket/InputPointer records and choose native
gesture/action policy; there is no implicit native pinch/orbit or driving binding.
BrowserInput retains its existing camera gestures, multi-touch and virtual controls.
Browser lost-capture/disposal cancellation now uses retained identity and finite
coordinates even when the event has no position, preserving other active contacts.

## Validation

Build with `scripts/build-windows.ps1` and the pinned local Emscripten 6.0.11 via
`scripts/build.ps1 -Configuration Release`. Focused commands:

```powershell
ctest --test-dir build/windows -C Release -R '^(desktop_accessibility|desktop_pointer|desktop_pointer_d3d12|desktop_text|desktop_api|desktop_api_d3d12|gui|input|input_adapter)$' --output-on-failure
node --test tests/input-adapter-tests.cjs
```

`desktop_accessibility` drives an actual Windows UIA client from a separate COM
thread against an SDL HWND: discovery, geometry, patterns, Unicode, focus,
disabled/hidden targets and retained-provider teardown. `desktop_pointer` and its
D3D12 variant cover contact routing plus actual SDL-to-DesktopHost GUI activation,
synthetic mouse suppression and scene pointer delivery. These inject SDL events;
they do not establish physical touchscreen/tablet acceptance.

Browser acceptance uses `scripts/gui-widgets-smoke.cjs` on both backends/DPR1/2,
`scripts/ui-input-smoke.cjs` on each backend, and the input adapter cancellation
regression. Evidence from this task is recorded in
[validation results](accessibility-input-validation.md).
