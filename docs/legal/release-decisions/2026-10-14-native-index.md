# Engine-native index: publication channel

- Decision: publish the engine-native index on the documentation site and through the static MCP
  server, limited to row names, subsystem, tier and readiness; ship it in no SDK zip
- Decided by: the maintainer
- Date: 2026-10-14
- Applies to: the engine-native index (planned `sdk/native_index.json` and its renderings) from
  SDK 2.2.0 preparation onward
- Source basis: none yet; no index exists on the candidate branch (see "Open")
- Counsel review: not recorded

## Decision

The maintainer decided on 2026-10-14 that the engine-native index may be published through two
channels only:

1. the project documentation site, and
2. the static, read-only MCP server (`tools/sco-core-mcp`).

Each published row carries exactly four fields: the row name, its subsystem, its tier and its
readiness. Byte patterns, offsets, addresses and any other value that locates code or data in a
game binary are not part of the published index, in either channel.

The index ships in no SDK archive. No profile in `sdk/components.json` (`core`, `game-sc`,
`legacy`, `core-source`, `host-source-sc`) includes it, and no profile may be changed to include
it without a new maintainer decision.

## What this record does not do

- It supplies no licence grant and no legal opinion. Counsel review is not recorded.
- It does not change the [game implementation export hold](2026-10-12-game-implementation.md) or
  the signature-row sources under `src/game/`; publishing a row's name does not publish the row.
- It approves no SDK scope, candidate, tag or release. SDK 2.2.0 scope is a separate, pending
  [draft](2026-10-14-sdk-v2.2.0-scope.md).
- It states no in-game verification. Readiness values describe engineering evidence as recorded
  in the repository, not gameplay acceptance.

## Open

- The index itself, its generator and check, the site page and the MCP tools have not landed on
  the 2.2.0 candidate branch. When they land, the review should confirm that the generated output
  contains only the four fields above, that the site and MCP renderings read the same file, and
  that `python3 sdk/test_package.py` still shows the file in no profile.
