# Quest host integration (M3)

The optional quest sample now has a complete shared host controller with persistent
save/load, Application-driven time, pause/suspension, scene travel and a portable
game-save envelope. Browser and Windows adapters use the same controller, quest
catalogue, custom delivery module and receipt policy. The M2 kernel and wire
formats are unchanged. See [kernel contracts](quests.md) and
[M3 validation](quests-host-validation.md).

## Responsibilities

| Owner | Responsibility |
| --- | --- |
| `WebEngine::Modules` / `WebEngine::Quests` | Typed module composition, immutable catalogue/provider lifetime, independent runtime state, candidate effects and receipt/snapshot validation |
| `examples/quests/session.hpp` | Sample wallet policy, current location, explicit pause state, sub-millisecond remainder, complete game-save validation |
| `examples/quests/controller.hpp` | Shared idle command queue, generation checks, staged load/travel, game/quest publication and Application callbacks |
| `examples/quests/scene.hpp` | Rebuildable scene projection using scene-scoped component IDs; no quest state |
| `examples/quests/browser.cpp` / `host.js` | RAF timestamps, renderer binding, visibility, localStorage, file download/upload and DOM controls |
| `examples/quests/windows.cpp` | DesktopHost, retained controls, per-user atomic files and command-line import/export |

These are application examples, not new engine-owned game services. Future code
plugins still register explicitly through M1. Reuse the controller's ownership
pattern and replace the sample catalogue/economy/envelope policy for another game;
do not put that game's wallet, content or journal rules into the generic kernel.

## One simulation owner

`Controller::callbacks().simulate` receives `ApplicationFrame::simulation.delta`.
`Session::advance` rounds its incoming seconds to nanosecond precision, carries
the fractional millisecond remainder, and submits whole milliseconds to the quest
candidate transaction. It publishes the remainder only after successful progress
and wallet publication. The small catalogue stays inside the M2 default budget;
larger integrations must define their own bounded retry policy. There is no second
clock, timer thread, direct scene tick or wall-clock progress recovery.

Pause goes through Application/`DesktopHost::setPaused`; paused simulation stops
while presentation and controls continue. Visibility suspension uses
`Application::setSuspended`, preserving explicit pause. DesktopHost supplies native
focus/minimize/lifecycle handling. Application rebases its timestamp on resume,
load and travel; hidden/paused/graphics-outage time does not become catch-up. Browser
render unavailability also drops its Application timestamp baseline. Quest timers
are capped by the existing Application frame policy, not elapsed real-world time.

Gameplay actions reject while explicitly paused or suspended. Save, Load and
Travel remain available while paused. Browser controls disable paused gameplay;
desktop attempts report a message through the same controller. Friendly selects
the 500 ms timer branch; Quiet selects the fact-driven gate branch. Retry keeps
earned receipts and cannot award the 20-coin first-completion effect again.

## Scene travel and input lifetime

The controller owns the session outside Application's scenes. Each travel builds
a new Harbour/Orchard `QuestScene`, loads it, registers the camera view and commits
`requestTransition(..., true)` at idle. Application rebinds the renderer before
unloading the outgoing scene. The browser renderer uses `Renderer::setScene`;
the desktop host resolves `DesktopView` component IDs. Scene projection resolves
its own IDs each frame. No delayed action retains a scene/camera pointer.

A failed load/view registration/binding removes the candidate and keeps the old
active scene/session. Successful travel updates the location without replacing
quest progress, receipts or the timer remainder; repeated travel retains exactly
one loaded scene. Shutdown releases renderer borrows before scene destruction.
Native event handlers are cleared before their controller captures expire,
including exception paths.

The bounded 32-command queue stamps input with a transient host epoch. Pause,
travel, session replacement, retry and lifecycle cancellation invalidate old
commands. File upload captures the epoch before awaiting the bytes; its completion
must still match before any decode/publication. Runtime/quest-generation checks
continue to protect kernel events. The host epoch is not saved and need not match
across platforms. Keep generation checks when adapting this sample to worker or
network callbacks; never retain raw scene references there.

## Save format and publication

`Session::save` encodes one at-most-64-KiB envelope:

- 8-byte magic/version `WEQHOST1`;
- location byte (Harbour=0, Orchard=1), explicit pause byte;
- uint32 wallet, IEEE-754 binary64 sub-millisecond remainder;
- uint32 length and the canonical M2 quest snapshot;
- trailing uint64 FNV-1a checksum of the preceding bytes.

Integers and the double's bit representation are little-endian. The envelope is
shared by Windows and browser `.qhost` files. It contains no scene IDs, callbacks,
render resources, input queue or absolute host timestamps. This is the sample's
v1 format, not a new generic engine save archive and not a Paws save format.

Decode bounds the complete file and checks exact length/version/checksum, location,
pause and finite fractional remainder. M2 validates the embedded catalogue/state/
receipt identity. The sample then validates wallet=20 exactly when the delivery
quest has durable earned completion, otherwise wallet=0. This policy is deliberately
in application code. Checksums and structural validation are not authentication or
proof of legal gameplay; Paws will keep its authoritative puzzle journals.

Load builds a separate candidate session and presentation scene before switching.
Invalid input or failed scene binding leaves the old session, wallet, active scene
and pause intact. After successful binding, the session is swapped on the owner
thread, the restored pause state is applied, clocks rebase, and old queued actions
are cancelled. No effect is emitted on load. A replacement runtime has a new
private runtime identity; host epoch changes invalidate delayed imports even if
the file's saved generation is older than the previous live session.

The application's wallet and quest candidate are committed together in memory;
one envelope joins them in storage. Saves are explicit, not automatic. Startup
restores the last saved envelope when present. Unsaved progress is intentionally
not recovered after process/page close. Import replaces the live session; press
Save to make an imported session the next startup save.

## Platform storage and controls

Browser storage uses the origin-scoped `webengine.quest.host.v1` localStorage key,
with a bounded base64 representation. `setItem` replaces the complete value;
quota/access failures report an error and retain the previous saved value. A bad
startup save reports the failure and leaves it on disk without silently replacing
it. Local storage is appropriate to this small sample; it is not a cloud save,
cross-tab conflict coordinator or large-game persistence recommendation.

Export downloads a `.qhost`. Import reads a bounded File asynchronously, checks its
captured epoch, copies bytes using the explicitly exported Emscripten `HEAPU8` view,
and frees the temporary Wasm allocation on every path. Page hide shuts down the
Application; bfcache restoration reloads the entry point. Browser callbacks and
JavaScript remain outside the portable controller/kernel.

Windows defaults to `DesktopFiles` organization `WebEngine`, application
`QuestHostSample`, save `session.qhost`. It uses bounded `readUser`/atomic
`writeUser` semantics (the sample reads the resolved save path through the same
bounded desktop-file helper). Assets resolve against the executable's asset root.
`--user-directory` isolates test profiles; it does not change the game save format.
`--import file.qhost` loads an explicitly named file at startup and `--export
file.qhost` writes the final live state at exit. Interactive native Save/Load use
the per-user slot; file interchange is also available through those CLI flags.

Neither host touches Paws' saves or engine pin. Native Windows is the desktop
acceptance target; other native operating systems and physical mobile remain
outside this milestone.
