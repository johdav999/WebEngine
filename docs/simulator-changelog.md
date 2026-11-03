# Simulator changelog

## v0.1 release candidate — 2026-10-25 (unreleased)

- Optional standalone C++20 WebEngine::Simulator target with no graphics dependencies.
- Atomic inventory and input/output capacity holds, bounded IDs and event outbox.
- Immutable recipes, FIFO assignments, explicit travel arrival, station/worker ownership and bounded deadline processing.
- FIFO customers, multi-line direct orders, patience, cancellation/finish-for-stock, claimed outputs and retained payment receipts.
- Canonical version-1 snapshots with candidate validation, exact catalog matching and fresh handle epochs.
- CafeGame browser integration, local saves, diagnostics and a portable console host.
- Standalone service example, API integration guide and exact-byte/event parity scenario for Linux, Windows and Wasm CI.

No tag/package has been published. Native execution and cross-platform parity must pass CI before release acceptance. There is no prior released simulator API requiring migration; unsupported snapshot versions fail without replacing live state.

Stage 7 source extension: portable TravelBinding/TravelTicket and CafeGame actor/navigation/interaction bindings; read simulator-movement.md for ownership and validation.

Stage 8 source extension: optional OccupancyPool<Owner>, FIFO spatial claims, stable seat/equipment slots, arrival tokens and validated typed restore. CafeGame integrates queue, service, seating and schema-2 spatial saves.

Stage 9 source extension: optional TaskBoard with dependency/role/shift allocation, priorities, guarded arrivals, interruption and typed restore. CafeGame schema 3 adds collect/prepare/hand-over workflows, staff breaks and dirty-table cleaning.
Stage 10 source extension: atomic Runtime::transfer, paired StockTransferred notifications, optional SupplyBoard with reserved incoming capacity, timed delivery polling, cancellation/retirement, reorder policy and typed snapshot validation. Core wire schema 1 and renderer pins remain unchanged.

Stage 11 source extension: optional Ledger with signed minor-unit postings, nonnegative cash, contiguous source watermarks, stale-safe prepare/commit, compact history and typed restore; InsufficientFunds and StaleTransaction errors. CafeGame adds prepaid/refunded supplies, staffed-time wages, arrears and schema-5 finance persistence. Core schema and renderer pins remain unchanged.

Stage 12 source extension: optional Freshness/Condition, exact integer expiry, monotonic use-source wear protection, independent clean/repair restoration, typed validation, atomic Runtime::remove and StockRemoved events. CafeGame adopts timed cup quality, waste, staff maintenance and schema-6 saves. Core schema and renderer pins are unchanged.


Stage 13 adds optional GridLayout and OccupancyPool::setEnabled; no core wire or renderer pin change. See simulator-layout.md.


Stage 14 adds optional demand.hpp: deterministic DemandRandom, ArrivalTimer and pure satisfaction/patience scoring. No core wire or renderer pin change. See simulator-demand.md.
