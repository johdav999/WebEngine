# Simulator movement adapters (stage 7)

engine/simulator/travel.hpp provides TravelBinding and TravelTicket without scene or navigation dependencies. Keep one binding per moving worker. begin validates an Assigned job and returns its epoch-scoped job ID, assignment token and local request sequence. Bind that ticket to a scene-scoped actor and an authored interaction point in the host.

Poll engine navigation on the owner thread. Report complete(runtime,ticket,true) only after actual arrival within the interaction radius. Report false on missing/destroyed actors, blocked paths or movement cancellation. reconcile discards stale/restored/cancelled assignments; cancel the corresponding navigation command too. clear invalidates a host request without changing simulator work. Retain terminal navigation results on LimitReached and retry after draining events. No asynchronous callback retains the Runtime.

Travel bindings are transient. On restore or scene replacement clear them, stop movement, rebind scene handles and reconstruct assigned work with fresh runtime epochs. Running jobs already consumed inputs and do not repeat arrival. The host chooses safe reconstructed positions; this release does not persist scene poses.

CafeGame uses WebEngine PathFollower and CharacterMotorComponent on two authored, obstacle-free corridors: barista to machine behind the counter, guest from entrance to service point. Character motors use free mode; these routes are not dynamic obstacle avoidance. Arbitrary placement, queue slots and seating are later stages. Game service requires the guest to have reached the counter. The headless Simulation retains its explicit immediate-arrival option for tests/simple consumers.

Validation (2026-10-25): CafeGame CTest under Node passed including travel/restore/failure cases. Engine standalone CTest 4/4 and all 787 simulator checks passed under Node and Edge 156 with no browser errors. Native Windows/Linux execution remains unverified locally.


Stage 13 CafeGame builds an engine WaypointGraph from validated free grid cells after furniture edits and load. Existing PathFollower, CharacterMotorComponent, arrival tokens and restore rebinding remain the movement adapter. Layout changes require a closed/drained cafe. See [layout contracts](simulator-layout.md).
