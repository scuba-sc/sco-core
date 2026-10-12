# Distribution scope for sdk-v2.0.0 (P05-10)

Decision: approve the complete reviewed candidate
Decided by: scubamount (maintainer)
Date: 2026-10-12
Applies to: the core, game-sc, legacy and core-source zips of sdk-v2.0.0, at the hashes in `sdk-v2.0.0.json`
Source reviewed: `779f5753bc438fafe3da14bda1d7e2f62a9ca831` (sdk-v2.0.0 preparation, PR #118)
Counsel review: not recorded. This record states the maintainer's decision only; it is not a legal opinion.

## Families

| Family | Decision |
|---|---|
| core | Approved at the manifest's hashes |
| game-sc | Approved at the manifest's hashes ([overlay](2026-10-12-game-overlay.md)) |
| legacy | Approved at the manifest's hashes |
| core-source | Approved at the manifest's hashes |
| Documentation site | Covered by its existing Pages approval, not by this record |
| Game implementation / product | On hold ([game implementation](2026-10-12-game-implementation.md)) |

The three conformance files under `tests/game_contracts/conformance/**` are recorded `reviewed`; they are repository-only and ship in no profile.

## Conditions

Publication still needs the signed `sdk-v2.0.0.json` approval and its `.sig`, verified by [`release.yml`](../../../.github/workflows/release.yml). This record does not replace that signature.
