# Game implementation (P05-07)

Decision: option A: hold implementation and product export; keep it out of every SDK profile
Decided by: scubamount (maintainer)
Date: 2026-10-12
Applies to: `src/game/**`, `include/sco/game/**`, `tests/fixtures/datacore/**` (88 files, issue `game-pack`)
Source reviewed: `779f5753bc438fafe3da14bda1d7e2f62a9ca831` (sdk-v2.0.0 preparation, PR #118)
Counsel review: not recorded. This record states the maintainer's decision only; it is not a legal opinion.

## Result

- The provenance ledger records these files as `reviewed` (accepted into the repository as interoperability-derived, GPL-3.0-only).
- None of them ships in the core, game-sc, legacy or core-source zips. That stays true for sdk-v2.0.0: adding one to a profile needs a new decision.
- Export of the game implementation as a separate artifact or as part of a product build is on hold under this decision. sc-offline's product releases are assessed separately.

## Open

Whether distributing the implementation alongside the game is a combined work under the GPL, and the basis for the referenced game names, are not decided here. No linking exception is granted ([plugin licence](2026-10-11-plugin-licence.md)).
