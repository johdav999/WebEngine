# RPG library L3 validation

L3 adds exact 3D target selection and complete area-target enforcement to API 1.9.0.
See [contracts](rpg-targeting.md). Platform validation is pending.

Required checks: standalone Windows/Linux/Node-Wasm suites, SDK omission matrix,
shared host controller tests, Windows Release D3D12 smoke, and browser WebGPU/WebGL2
input/pixel/storage smoke. Physical GPU evidence must be recorded separately from
software CI rendering. Consumer game adoption remains separate work.
