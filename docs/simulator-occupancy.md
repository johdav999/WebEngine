# Occupancy and spatial queues (stage 8)

Include engine/simulator/occupancy.hpp and use OccupancyPool<Owner>. The default Owner is CustomerId; WorkerId also works for equipment access points. A pool owns bounded logical claims on host-authored point indices, and requires one owner thread. It knows no actors, coordinates, paths, wallets or wall clock.

Construct with point count, total request limit and compact flag. request(owner) appends FIFO; retries preserve priority/token. With compact=true (standing queue), removing the front shifts later claims forward. Changed destinations get a new token and lose occupied status. With compact=false (seats/counter/equipment), existing holders keep their points; the oldest waiting request gets the lowest free point. Waiting claims use Pool::waiting and cannot be occupied. There is one holder per point.

claim(owner) and claims() expose reservation state; claims() references last only until mutation. occupy(owner,point,token) confirms actual arrival, rejects stale responses and is idempotent for the current claim. release removes the request and promotes waiters; repeated release returns AlreadyTerminal without mutation. Cancellation, abandonment and departure require host cleanup. Hosts must validate owner lifetimes and invalidate claims when an actor or interaction point disappears. Spatial claims do not replace production station/worker reservations.

State-changing allocation is staged in a candidate; failed validation or token overflow preserves claims. Counts are bounded (1..128 points, total limit up to 4096); there are no event callbacks or silent eviction. Tokens are monotonic within one pool instance. They must not be routed between unrelated pools. Host bindings also carry the owner epoch/scene handle.

restore accepts typed claim records with validated/rebased owners, rejects duplicate owners/points, invalid waiting/occupied combinations and inconsistent queue layouts, and publishes only a valid candidate. It issues fresh tokens. Point layout/version compatibility, serialization and cross-system validation belong to the game envelope. The existing Runtime core snapshot remains schema 1; no spatial data is silently added to it.

CafeGame uses three standing positions, one service position and two seats. Its schema-2 envelope includes all guest phases, remaining seated time and occupancy state beside core bytes/wallet. It validates service-queue order and owner references before publication; schema-1 cafe saves are explicitly migrated. Scene handles and arrival tokens are reconstructed on load. The console uses the same rules with explicit immediate arrival/seating; browser movement reports real arrival. See CafeGame docs/simulator-occupancy.md for the playable flow and actual evidence.

Point reservations prevent duplicate destinations; collision avoidance, dynamic placement, changing point layouts and crowd path planning are not supplied by this helper. CafeGame continues to use authored engine PathFollower corridors.

Validation (2026-10-25): standalone engine CTest 4/4 and 814 contracts passed under Node and Edge 156. CafeGame lifecycle/restore tests and both forced WebGL2/automatic software fallback runs passed queue refill, seating and departure cleanup. Native execution and hardware WebGPU/mobile remain unverified. Available installed/WebEngine/CafeGame engine-skill copies were updated; legacy Johan/CatGame mirror paths remain unavailable.


Stage 13 adds setEnabled(span<const uint8_t>) for stable points. Disabling a claimed point fails atomically; enabling points promotes FIFO waiters. All points may be disabled in noncompact pools. Compact queues require all points enabled. Hosts persist and validate the layout/mask jointly with claims. See [layout contracts](simulator-layout.md).
