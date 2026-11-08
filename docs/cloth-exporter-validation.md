# Cloth exporter validation — 2026-10-26

Delivered: Blender add-on/background exporter, WECG v1 physical garment assets,
portable `loadClothAsset`, integration guide and engine-skill updates. No game
dependency pins were changed. Tests below were run during this task.

| Target | Result | Evidence |
| --- | --- | --- |
| Blender 5.1.2 | PASS | `tests/blender_cloth_fixture.py`, background factory-startup, Python exit code 1; successful exit 0. |
| Native Windows, VS2022 Release | PASS | Built `clothing_tests` in `build/clothing-native-vs2022`; `clothing_contracts` passed in 5.50 s (CTest total 5.62 s). |
| WebAssembly, local Emscripten 6.0.11 | PASS | Built `clothing_tests` in `build/web-release`; Node CTest passed in 1.70 s (total 1.73 s). |
| Actual Edge browser 156.0.4314.42 | PASS | `scripts/cloth-export-browser-smoke.cjs`; all nine printed contract groups passed, including `Cloth asset PASS`; browser main exited 0 with no page errors. |
| Engine skill copies | PASS | All six validated; exporter reference SHA-256 `60252302AD6FAC8BCBD3A68D3843B2A10A9BC053040B41A974D6D476ABCCC047` matched. |

Blender coverage: two-bone weighted garment with nontrivial child bone roll,
continuous UVs, painted pins/masses, deterministic repeated garment bytes,
validate-only mode, add-on register/unregister, preservation of existing bytes on
failure, invalid Preserve Volume, non-identity world transforms, missing pins,
UV seams and mirrored UV orientation. A matching GLB character is exported by
Blender itself and embedded with the garment in the C++ fixture.

C++/browser coverage: exact shared skeleton identity; GLB geometry bind-space
agreement; bind-pose skinning; painted values and topology; actual
`ClothingInstance` application and fixed-step simulation; every truncation of the
fixture; bad rig rest transforms and inverse binds; oversized vertex counts;
unsupported version/trailing bytes; UV changes that would force tangent vertex
splitting; compatible reordered binding palettes; rejected imports leave the live
outfit intact. Existing clothing and cloth milestones 2–8 contracts also passed.

The expanded transformed-object fixture initially exposed an incompatible GLB
bind-space case. The delivered exporter now requires garment and armature world
transforms applied to identity, documented and tested explicitly. This task does
not change GLB importer transform semantics. A separate tangent-generation review
identified possible vertex duplication; exporter validation and a loader guard
now reject it before asset publication.

Skills updated: repository, EJOHDAV personal, RPGGame, CafeGame, Johan personal and
CatGame. Each retains its existing game-specific guidance and gains a focused
exporter reference plus a SKILL.md route. No consumer runtime integration is claimed.

Limits of this evidence: the new asset path was exercised through scene/cloth
simulation in native and browser hosts, without a new rendered garment screenshot
or D3D12/WebGL/WebGPU visual acceptance run. No renderer implementation changed.
Blender add-on registration and export functions were tested, not manual clicking
through its file browser. Blender versions other than 5.1.2, physical mobile
devices, production character assets and multi-material/seamed/proxy workflows
were not tested. These results do not establish sustained mobile performance.
