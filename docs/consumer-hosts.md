# Shared application hosts

`WebEngine::Browser` is the Emscripten renderer target. It owns the renderer
source list, shader embedding, WebGPU port and WebGL2 link requirements. Consumers
link it with their game libraries and the browser GUI/audio/asset adapters; they
do not compile engine renderer files themselves. Native applications continue
to link `WebEngine::Desktop` and use its single owned `Application`.

An application session may borrow a platform host's Application and GuiSystem.
Keep commands, scene assembly, progression, presentation and save policy in that
session. The host supplies asset IO, audio backends, durable storage and file
dialogs. Register desktop scene camera handles before starting or transitioning.
Release UI callbacks and rendering borrows before session/scene destruction.

`GuiSystem::clear()` is an idle-only rebuild boundary. It cancels pointer capture,
clears focus/hover/scroll state and draw resources, then destroys root children.
Font, theme and scale remain configured. Old widget IDs cannot be activated.
Never call it from a widget event callback or while drawing; mark the application
UI dirty and rebuild after event dispatch instead.

Desktop event handlers receive `PointerPreview` before retained GUI dispatch.
This lets an application widget composition reserve a custom drag. Preview reservation is suppressed while touch/pen owns retained GUI capture. Returning
true consumes the contact, cancels engine GUI capture on Down and prevents
gameplay input. The application owns continuation/cancellation of its drag.
`Pointer` follows GUI dispatch with `guiConsumed`, logical `position`, `pointer`
phase and `primary`. `Wheel` follows GUI scrolling with its logical delta and
`guiConsumed`. Honor GUI ownership and `InputCancelled`; callback captures must
outlive registration. Events are synchronous during host polling, before advance.

`DesktopHost::setWaterTime(optional<double>)` supplies a game-owned shader phase
in seconds (finite 0..1,000,000). Use simulation time for pause-sensitive water,
or zero for reduced motion. Null restores the existing simulation-clock default, preserving pause behavior.
The browser Renderer already has the matching explicit phase setter.

M4 adds native and Wasm GUI reset regressions. The module/quest suites and rendered
examples remain independent consumers; Paws consumes the same targets using its
own unchanged quest rules and version-12 save codec. Native acceptance for this
published revision uses SDL/Dawn. Paws M6 subsequently routes all 29 quests
through these public targets and writes version-14 saves, retaining versions
1–13 and exact pre-migration originals. See the [M7 SDK handoff](module-sdk.md)
for independent consumer and release checks. This integrated checkout also retains the local direct D3D12 backend through
the same DesktopHost interfaces; see [native renderer contracts](windows-d3d12.md).
These engine changes do not update any consumer dependency pin.
