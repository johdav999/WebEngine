# Workflow tasks and staff scheduling (stage 9)

Include engine/simulator/workflow.hpp. TaskBoard is an optional C++20 owner-thread allocation layer above production jobs. It owns bounded task records, dependency edges, role-matched staff, shift windows and remaining timed work. It owns no actors, callbacks, production inventory, paths or application clock. Construct with the Runtime epoch and validate registered WorkerIds against the selected Runtime in the host. Do not route task IDs between unrelated boards.

## Scheduling and effects

addStaff supplies role keys and an absolute [start,end) simulation-time shift. enqueue copies a TaskSpec (key, role, priority -100..100, duration and dependencies). Dependencies must already exist, so cycles and forward references are rejected. Default record limit is 128 (maximum 4096), 16 staff, 8 prerequisites per task and 16 roles per staff. Timed work is bounded to one day; zero duration denotes an externally completed task.

assign(now) reconciles supplied monotonic simulation time, then assigns eligible tasks by descending priority and ascending TaskId. Staff are visited in WorkerId order. It does not preempt active work; reprioritization affects subsequent allocation. Each staff member owns at most one task. start(id,worker,token) reports actual arrival; positive-duration work advances only after start. complete reports an external effect only for a running task with no remaining timed work. Host effects must be applied idempotently before completing externally controlled tasks.

advance(now) consumes only running timed work within the staff shift window. Completion at exactly shift end wins. Other work is requeued at shift end with remaining progress preserved. available(false) explicitly interrupts and releases staff; later assignment issues a new token. Shifts can be updated with shift. Assignment remains an explicit owner command. Hosts must schedule at meaningful boundaries; a large advance does not invent intermediate navigation arrivals or assignments.

cancel propagates to dependents and releases assigned staff. Terminal work cannot be started again. forget removes only terminal tasks with no dependents; retire completed chains from the leaves. IDs never repeat. Validation/allocation occurs in a private candidate before publication; queries return copies or references valid only until mutation. Snapshot restore rejects invalid references, dependency order, role/shift ownership and state combinations, then issues fresh active tokens. The host owns byte encoding, epoch rebinding and joint game-state validation.

## CafeGame integration

CafeGame assigns a collect-supplies task (500 ms), preparation task delegated to the existing production job, and hand-over task completed only after the player serves. The same barista has barista/cleaner roles. Service work has priority 20 and table cleaning priority 5; current work finishes unless explicitly interrupted. Staff use the existing WaypointGraph and PathFollower around the authored counter/aisles.

A served guest's seat stays reserved until the departing guest is clear and the barista completes a one-second cleaning task. An abandoned cleaning task releases its spatial claim as explicit failure cleanup. Used cups and a cleaning cloth expose the task in the scene. The staff-break control pauses staff allocation without stopping customer patience or an already-running coffee machine. Returning to a preparation task never commits ingredients twice.

Cafe schema 3 combines core bytes, wallet, spatial state, workflows, shifts and dirty-seat ownership. It validates cross-system references before publishing. The host rebuilds navigation with fresh epoch/task tokens. Transient tokens are excluded from durable encoding. Formats 1 and 2 are explicitly migrated; Runtime core snapshots remain schema 1. Native simple hosts can retain immediate arrival/seating; browser input additionally enforces the hand-over task gate.

This is a bounded task graph for simple simulators, not a behavior-tree/scripting system. There is no global task thread, renderer integration in the helper, staff wage model, recurring shift calendar or dynamic crowd obstacle avoidance.

Validation on 2026-10-25: engine standalone CTest 4/4 and 870 checks passed under Node; browser contract and CafeGame fallback smoke passed. Cafe tests cover interrupted work and format-2 migration. Native execution and hardware WebGPU/mobile remain unverified. Available installed/WebEngine/CafeGame skill copies were updated; the legacy Johan/CatGame paths remain unavailable.
