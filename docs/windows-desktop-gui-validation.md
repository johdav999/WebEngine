# Interactive desktop GUI acceptance — 2026-10-21

## Product profile and scope

Shared C++ retained GUI, Windows x64 SDL3/Dawn D3D12 vehicle example. Role: local
driver; no account or external service. Seed: existing procedural vehicle course,
also exercised with `assets/formula1-red-driving.glb`. Build/run:

```powershell
./scripts/build-windows.ps1 -BuildDir build/windows-desktop
./build/windows-desktop/Release/webengine_windows.exe
ctest --test-dir build/windows-desktop -C Release -R '^(desktop_text|desktop_api|application|input|gui)$' --output-on-failure
./scripts/windows-smoke.ps1 -BuildDir build/windows-desktop -EvidenceDir build/evidence/windows-desktop-gui
./scripts/windows-smoke.ps1 -BuildDir build/windows-desktop -EvidenceDir build/evidence/windows-desktop-gui-glb -Model assets/formula1-red-driving.glb
```

The native smoke uses the same application and public host, hidden real SDL/Win32
windows, deterministic Application timestamps, SDL events, an SDL virtual gamepad
and actual Dawn framebuffer readback. Test injection stays outside the application.
The focused text suite uses the OS clipboard, temporarily replacing/restoring its
text, and requires desktop/clipboard access. Test windows enable background events
for their virtual gamepad; normal host focus/suspension policy is unchanged.

Flows: mouse pause/settings; keyboard/controller focus and activation; Unicode
text/selection/clipboard; IME commit/cancellation; settings at 960x540 and 640x360;
focus loss and return to driving; browser GUI/vehicle regression.

## Issue ledger

| ID | Priority / flow | Baseline and acceptance | Owner / regression | Status |
| --- | --- | --- | --- | --- |
| GUI-001 | P1 text editing | Desktop ignored SDL text events. Existing TextInput must accept Unicode commits, selection, deletion, clipboard shortcuts and bounded valid input; preedit must replace once and cancel safely. | Private SDL text adapter; `desktop_text`, native `menu-text.png`. | Verified |
| GUI-002 | P1 input ownership | P toggled the menu before checking GUI ownership. Typing P/R must stay in the textbox; acquiring text focus must cancel held driving sources, including programmatic focus while running. Blur/Resume must require fresh input. | Host ownership + example shortcut policy; vehicle smoke. | Verified |
| GUI-003 | P2 controller Back | B blurred GUI focus but did not close the menu. Start must open, D-pad traverse/adjust, A activate and B return. | Application menu policy; SDL virtual-gamepad smoke. | Verified |
| GUI-004 | P1 controller handoff | The neutral-input gate also blocked modal navigation while a driving trigger stayed held. Menu navigation must remain usable; driving must remain cancelled until neutral. | Host gamepad dispatcher; held-trigger smoke. | Verified |

## Verified results

- MSVC x64 Release builds the public host, vehicle application, public-header
  consumer, text suite and smoke driver. No SDL/Windows/GPU types were added to
  portable public interfaces. Application remains the sole frame scheduler.
- **5/5 focused CTest passed:** application, input, gui, desktop_api, desktop_text.
  Text cases cover UTF-8 byte selection/codepoint deletion, word movement,
  Ctrl+A/C/X/V using the Windows clipboard, single-line line-ending normalization,
  repeated preedit, IME Enter/Escape, character-to-byte offsets, commit,
  widget identity, queued old-focus text, application edits, malformed/oversized
  input, hiding and explicit cancellation.
- **Both native vehicle/menu scenarios passed:** course alone and existing GLB.
  The same tests cover mouse pause/checks/slider, keyboard slider/Resume/Quit,
  wheel scroll and fixed actions at 640x360, native name entry through keyboard
  and mouse focus, submit-to-Resume, programmatic unpaused text ownership, drag
  cancellation on blur, suspension, explicit pause retention, virtual controller
  navigation/activation/slider and held-trigger cancellation. Driving, braking,
  contact, reset, model failure retention and safe shutdown remain checked.
- Actual `menu.png`, `menu-small.png` and `menu-text.png` readbacks were visually
  inspected alongside image-dimension/scene-variation checks. Resolution scaling
  affects capture dimensions; GUI input/layout remains in window logical pixels.
- **Browser GUI: 10 checks passed per backend**, WebGPU and forced WebGL2.
  **Browser vehicle suite passed both backends.** Chrome 155.0.8059.85; no captured
  page/console errors. Local Emscripten 6.0.11 built current source; generated
  outputs and current web adapters were staged in `build/windows-gui-browser-site`
  and served on localhost:8186. Browser suites ran serially.

Ignored evidence and logs:

```text
build/windows-desktop/{gui-final-build.log,gui-tests.log}
build/evidence/windows-desktop-gui/{verified.json,binary-hashes.json,*.ppm,*.png}
build/evidence/windows-desktop-gui-glb/{verified.json,binary-hashes.json,*.ppm,*.png}
build/evidence/windows-desktop-gui-browser/results.json
build/evidence/windows-desktop-gui-browser-gl2/results.json
build/evidence/windows-desktop-gui-vehicle-browser/results.json
build/windows-desktop-gui-{browser,browser-gl2,vehicle-browser}.log
```

Browser staged module SHA256:

```text
webengine.wasm    242256a8ba1c9cc29ff22950b46cba9c6a51404ad2535031fae98b95308e578d
asset_decode.wasm c29f77dac3ce3a21ecdea8eccb0a516ca84f7a6013465bdb3cf403418f554543
```

## Acceptance boundary and next review

Synthetic SDL IME events verify adapter ownership, not installed Windows IME
candidate windows or physical keyboard layouts. The controller is virtual;
physical device/reconnect behavior, monitor/fullscreen/DPI transitions and broad
Windows drivers need separate acceptance. Native Windows accessibility, undo,
grapheme editing, bidi/shaping and multiline input are not delivered. Text uses
the existing single-line/codepoint GUI contract. Driver names are session-local.
No P0/P1 defects remain in the exercised flows. The next review should exercise
a physical controller and an installed Windows IME on a visible window.
