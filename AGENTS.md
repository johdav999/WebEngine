# Repository instructions

## Architecture

- Read [architecture.md](architecture.md) before planning or changing engine code, public APIs, platform integration, build boundaries or application/demo behaviour.
- Check each proposed change against its architectural boundaries. Keep reusable mechanisms generic, optional and composable; keep game-specific rules in application/example code.
- Use the current architecture sections to locate existing responsibilities. Treat the intended extensions as design directions, not implemented features or a requirement to implement unrelated systems.
- Update `architecture.md` in the same change when module responsibilities, ownership/lifetimes, frame execution, dependencies, platform boundaries or extension status change. Distinguish current implementation from future work.
- Always update the installed `engine-skill` at `C:/Users/Johan/.codex/skills/engine-skill` in the same task whenever new engine functionality is added, existing functionality is extended, or the architecture changes. Update `SKILL.md` and affected supporting references to match the delivered APIs, usage, defaults, limitations, architectural contracts, build commands and validation guidance. Include this documentation update before reporting the task complete.
- Whenever `engine-skill` is updated, also update the CatGame repository copy at `C:/Users/Johan/source/repos/CatGame/.agents/skills/engine-skill` in the same task, including `SKILL.md` and affected supporting references. Merge the changes while preserving CatGame-specific guidance, and verify both copies before reporting the task complete.
- Read the relevant feature contracts in [README.md](README.md) and `docs/` before extending existing systems. Verify the current source rather than relying on older feature inventories.

## Implementation

- Preserve unrelated work in the shared checkout. Inspect its current state before editing; do not reset or clean changes belonging to other work.
- Reuse the scene hierarchy, actor/component handles, input actions and existing resource representations. Preserve deferred destruction, guarded traversal and cache invalidation contracts.
- Keep browser APIs and renderer implementation types out of new portable interfaces. Put game-specific examples outside reusable systems; existing demo placement is documented in `architecture.md`.
- Make ownership, callback/subscription lifetimes and cancellation explicit. Reject invalid data without publishing partial state or replacing valid state.
- Keep simulation state independent of interpolation, culling, LOD and other presentation choices.

## Validation and handoff

- Run checks appropriate to the changed contracts. Use focused native tests for core logic and relevant browser smoke scripts for input, adapters and rendering. Documentation-only changes need content/link checks rather than runtime builds.
- Follow the setup/build commands in `README.md` and the Emscripten version in `.emscripten-version`; use the repository-local SDK.
- Record what was actually verified and any remaining limitations. Desktop browser checks and Chromium mobile emulation do not establish physical Android/iOS acceptance or sustained mobile performance.
- Keep feature contracts and architecture documentation consistent with delivered code. Do not report planned capabilities as implemented or existing evidence as a new test run.
