# RPGGame adoption mapping

R10 prepares this plan; it does not advance RPGGame's engine submodule at
`56080eca124c9a62e15b146799a8004bdad5a9b2`. The game remains a Windows 3D,
third-person, turn-based RPG. Browser samples validate the engine, not a new game
platform. Resolve the published candidate and its evidence in
[L1 validation](rpg-l1-validation.md) before a separately authorized upgrade.

| Composition | RPGGame owner |
| --- | --- |
| `WebEngine::RPG`, optionally `WebEngine::RPGQuests` | Portable game rules target, enabled at the pinned engine root |
| `quest_bridge::Coordinator` and retained immutable catalogues | `GameSession` in `src/app`; one owner thread and private kernels |
| Resource units, advancement, slots, initiative, abilities, reactions | `src/gameplay`: coherence, resonance, probability distance, risk, balance |
| Campaign, islands, factions, story and sailing | `src/world`, `src/narrative`; enclosing staged transaction for campaign effects |
| Spatial snapshots, route reservation, durable-to-scene bindings | `src/runtime`; world revisions invalidate queued commands and quotes |
| Third-person camera, HUD, animation, sound | `src/presentation`; DesktopHost retains Application/input/frame ownership |
| Combined save, migrations and storage | `src/persistence`; keep campaign + coordinator + award receipts together |

These paths and GameSession remain planned. Reuse the starter's public scene and
host composition. Register camera views before activation, honor GUI-consumed
events, and destroy callbacks before their sessions/scenes. On travel/load, discard
queued input, rebuild handles, then accept commands. Reserve routes before commit;
animation acknowledges committed actions without applying effects a second time.

First adoption slice: one Greywake character, equipment, a turn-based encounter,
one quest award and save/load. Test game-owned Lattice rules and failures including
full inventory, invalid spatial stamp, interrupted projection and no duplicate
reward after load. Keep paid ElevenLabs/Tripo generation in authoring tools only.

Portable game code stays in shared targets. Linux cloud runs
`bash scripts/setup-cloud.sh` when unprepared, then `bash scripts/check-cloud.sh`.
Extend those tests with GameSession/rules/save integration without Windows renderer
dependencies. Windows must run the game's Release build and desktop smoke;
record real D3D12/input checks separately. `RPG_BUILD_DESKTOP` and `RPG_BUILD_TESTS`
remain independent. Source-level engine adoption does not establish game GPU
acceptance, content quality, balance or campaign completeness.
