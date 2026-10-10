# Asset loading validation — 2026-10-10

Validated in this shared checkout using isolated `build/assets-native` (GCC 16.1.0,
Debug) and `build/web-assets-release` (repository-local Emscripten 6.0.11, Release).
Other work was actively changing the checkout; this record describes actual runs,
not an immutable commit-wide release certification.

| Check | Result |
| --- | --- |
| Final native `assets` and `application` suites | 2/2 passed |
| Browser asset runner and normal `webengine` executable | Built successfully |
| HTTP/Wasm asset smoke, Chrome 154.0.8037.99 | Passed; six checks, no page errors |
| Existing WebGPU application lifecycle smoke, same Chrome | Passed; seven checks, no console/page errors |
| Documentation local links and patch whitespace | Checked |

Native tests cover typed registration, wrong/null result rejection, deferred owner
publication including worker completion, cache and in-flight identity, consumer
cancellation, retries, stale callbacks, resource release, abort reentry, closure
destruction outside completion locks, saturated aggregate progress, heterogeneous
batches, preparation ordering/failure/cancellation, pending initial scenes,
scene-unload/shutdown cancellation and transition redirects from preparation.

The HTTP runner imported the real Knarr FBX into Wasm. Three consumers shared one
HTTP request; cancelling one preserved the others and scene activation waited for
preparation. Missing/corrupt/oversized/timed-out loads published no resource. Server
observations confirmed cancellation, oversized-response and timeout HTTP aborts.
A failed source retried with exactly two requests. Evidence is in
[asset results](../build/evidence/assets/results.json).

The existing application smoke checked actual WebGPU canvas pixels for paused
animation and camera movement, separate clocks, host suspension/resume, scene
archive replacement/return and shutdown. It used the isolated site's own server
without replacing shared `dist/`. Evidence is in
[application results](../build/evidence/assets-application/results.json), with PNGs
and a physically saved scene archive in the same directory.

An earlier full native run passed 36/37 suites. The separate persistence suite failed
with `persistence: Aggregate scene node budget` while concurrent persistence changes
were being developed. This task did not modify that implementation or its tests.
The focused suites above were rebuilt and rerun after the final asset changes;
the earlier full-suite result is not reported as a final all-suite pass.

Browser HTTP is asynchronous; importing/decoding still runs on the browser thread.
Native hosts supply their IO/executors. Scene preparation provides a host callback
before activation; GPU prewarming and asynchronous GPU validation are not added.
No physical Android/iOS acceptance, background decode latency or sustained mobile
performance was measured.
