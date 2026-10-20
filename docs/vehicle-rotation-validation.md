# Vehicle rotation stability

A near-quarter-turn vehicle pose could fail with `Transform cannot be represented
as local TRS` during physics writeback. Float `asin` reconstruction lost the small
cosine when the normalized axis sine rounded to one. Decomposition now uses
`atan2` with the cosine magnitude from the other axis components. Existing shear,
singular-matrix and reconstruction validation remains in place; no API changes.

The core regression checks 402 poses around both positive and negative 90-degree
headings, with pitch, roll and nonuniform scale. On Windows, Release native tests
`engine_core`, `physics`, and `vehicle` passed on 2026-10-20. The downstream
RacingGame browser validation is recorded in that repository.
