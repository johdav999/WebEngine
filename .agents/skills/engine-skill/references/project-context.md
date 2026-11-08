# WebEngine source repository

This installation lives in WebEngine itself. Its root `engine.md` is the canonical integration guide; the bundled `engine.md` beside this file is a synchronized portable copy.

Read root `AGENTS.md`, `architecture.md`, `README.md`, public headers and feature contracts before engine changes. Inspect working-tree changes and preserve work from other tasks. Some source files may implement unfinished milestones that are absent from the documented or committed release.

For a consumer question, locate that consumer's configured engine source/pin before selecting APIs. The adjacent RPGGame and CafeGame projects have different hosts and older dependency pins; the current engine checkout is not automatically their runtime dependency.

No engine feature was implemented by installing this skill. The guide does not change dependency pins, build boundaries or the platform support contract.
