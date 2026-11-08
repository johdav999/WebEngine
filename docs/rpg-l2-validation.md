# RPG library L2 validation

L2 implements generic persistent parties and encounter teams in API 1.8.0.
Contracts and scope are documented in [rpg-parties.md](rpg-parties.md).

Validated implementation: `d2d9657` (the full revision is recorded by the linked
workflow runs). The initial test-only field-name typo at
`6f90925` was corrected before this accepted run.

| Gate | Result | Evidence |
| --- | --- | --- |
| Standalone Linux | 17/17 passed | [RPG library](https://github.com/johdav999/WebEngine/actions/runs/40232058401) |
| Standalone Windows Release | 17/17 passed | Same RPG library run |
| Emscripten/Node | 17/17 passed | Same RPG library run |
| SDK and omission matrix | All Windows/Linux/Wasm jobs passed | [Portable module SDK](https://github.com/johdav999/WebEngine/actions/runs/40232058364) |
| Windows Release host/controller | Build and controller test passed | [Rendered hosts](https://github.com/johdav999/WebEngine/actions/runs/40232058379) |
| Windows D3D12 rendered smoke | Passed with WARP, CPU and GPU validation enabled | Same rendered-host run, `rpg-windows-host` artifact |
| Browser controller and rendered WebGPU/WebGL2 | Passed; input, pixels, storage and recovery checks retained | Same rendered-host run, `rpg-browser-host` artifact |

`rpg_parties` checks active/reserve membership, leader/control validation,
duplicate/foreign membership rejection, limits, atomic rollback, stale commits,
team completeness/relations/control, encounter freeze and save round trips.
`rpg_snapshots` retains the old byte fixture and rejection coverage. The shared
host test verifies parties and teams survive its combined save/load lifecycle.

CI rendering uses Microsoft Basic Render Driver (WARP) on Windows and Chromium
SwiftShader on Linux. WebGPU runs under Xvfb; WebGL2 runs headlessly. These are
actual rendered acceptance checks, but do not establish fresh physical GPU or
mobile acceptance for L2. L1 hardware evidence remains historical, not a new L2 run.

The general engine build is separate from the passing RPG gates. Its pre-existing
vehicle/spotlight failures remain outside L2; see the
[general build](https://github.com/johdav999/WebEngine/actions/runs/40232058367).

RPGGame integration and advancing its pinned engine revision are separate work.
