# Star Citizen overlay package (P05-06)

Decision: approve the complete reviewed overlay
Decided by: scubamount (maintainer)
Date: 2026-10-12
Applies to: every file the `game-sc` profile selects, and the same files in `legacy` ([`sdk/components.json`](../../../sdk/components.json))
Source reviewed: `779f5753bc438fafe3da14bda1d7e2f62a9ca831` (sdk-v2.0.0 preparation, PR #118)
Counsel review: not recorded. This record states the maintainer's decision only; it is not a legal opinion.

## What was reviewed

The game-sc profile's public interfaces (C, C++, C#), the synthetic example packs (see [example data](2026-10-11-example-data.md)), the game-pack documentation and the ABI/contract test fragments it ships. At the reviewed commit the profile selects no `needs-review` file.

## Result

game-sc and legacy may be published at the exact bytes the sdk-v2.0.0 approval manifest lists. The approval binds to those hashes; a changed overlay needs a new review.

## Not covered

The game implementation, internal game headers and DataCore fixtures (see [game implementation](2026-10-12-game-implementation.md)); the documentation site has its own approval.
