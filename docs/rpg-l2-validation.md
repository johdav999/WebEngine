# RPG library L2 validation

L2 implements generic persistent parties and encounter teams in API 1.8.0.
Contracts and scope are documented in [rpg-parties.md](rpg-parties.md).

Validation is in progress. Required gates are the standalone native/Node-Wasm
suite, Windows Release host/controller build, and rendered Windows D3D12/browser
WebGPU/WebGL2 smoke workflows. GPU checks are not implied by portable tests.

RPGGame integration and advancing its pinned engine revision are separate work.
