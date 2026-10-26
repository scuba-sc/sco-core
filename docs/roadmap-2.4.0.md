# sco-core SDK 2.4.0 roadmap

SDK **2.4.0**, optional Star Citizen game pack **0.8.0**, frozen plugin ABI
**1.1**. Release candidate 1 is published as the repository's latest release,
[`sdk-v2.4.0-rc.1`](https://github.com/scuba-sc/sco-core/releases/tag/sdk-v2.4.0-rc.1); the final signed
`sdk-v2.4.0` release follows. This roadmap is not a release announcement.
Contract presence, implementation, default availability, synthetic validation and
in-game acceptance are separate. No game behavior is certified here.

RC2 preparation below describes current source and the consumer checks owed by a
candidate built from it. It does not announce an RC2 archive or change the
published RC1 bytes. See the [RC2 consumer reconciliation](agent-workflows.md#rc2-consumer-reconciliation)
for the bounded issue contracts and acceptance path.

## Scope

This roadmap covers the SDK and its developer tooling. Engine, studio and product
releases are separate; they are not SDK 2.4.0 completion criteria. Consumers adopt
published, checksum-verified archives (today the release candidate 1 archives) rather than
integration branches.

## Distribution profiles

| Profile | Release candidate 1 archive | Purpose |
|---|---|---|
| core | `sco-sdk-core-2.4.0.zip` | Game-neutral plugin headers, bindings, examples and tools |
| game-sc | `sco-sdk-game-sc-0.8.0.zip` | Optional overlay over the exact compatible core archive |
| legacy | `sco-sdk-2.4.0.zip` | Combined core and game developer kit |
| core-source | `sco-sdk-core-source-2.4.0.zip` | Standalone game-neutral host-source kit |
| host-source-sc | `sco-sdk-host-source-sc-2.4.0.zip` | Standalone host-source kit with optional game implementation |

The manifest in `sdk/components.json` defines actual membership. Compatibility
metadata binds overlays to exact base bytes. Local packaging does not authorize
distribution; each selected file still needs applicable provenance review.

## Current contract inventory

| Surface | Development contract and limits |
|---|---|
| `sco.widgets` 1.2 | Stable ASCII tags; 128-byte node with `const char* tag` at offset 104, copied by the host. Native C/C++ submission and raw C# interop are distinct from typed C#/Lua support. |
| `sco.storage` 1.1 | 144-byte table with bounded table/view and column enumeration. |
| `sco.diagnostics.publisher` 1.0 | Numeric and enum observations through `sco_diagnostics_observation_v1`; no free-form identifiers. |
| `game.loadout` 1.2 | Optional asynchronous submit/status/cancel with state, completed-prefix count and nested report. Respawn event availability is a separate gate. |
| `game.inventory` 1.1 | Copied port snapshot and item-category vocabulary; native read remains held off by default. |
| `game.inventory.contents` 1.0 | Copied bounded stored-item snapshots; no grounded native adapter is supplied. |
| `game.audio` 1.0 | Owner-bound trigger/playback tokens and unload cleanup; acceptance is not audible playback, native backend absent/OFF. |
| `game.entities` 1.2 | Copied epoch-pinned class registry, debug name lookup and finite typed component presence; no pointer export or native lookup adapter. New C#/Lua calls unsupported; managed layout parity only. |
| `game.world.control` 1.1 | Bounded multi-hit with provider-owned acquire/query/release, validated copied records and owner skips; native adapter remains OFF. |
| `game.actor_stats` 1.1 | 29 raw read IDs; stat units/ranges remain unverified. The separate write service retains its original allowlist and default-off admission. |
| `game.input` 1.0 | Named action metadata: 1,297 ids re-verified statically on 4.10.197.2878 (F-4.10.197.2878-050); not certified input dispatch. |
| `game.gamerules` 1.1 | Static parameter definitions through `list_params`; live read requires a backend. |
| `game.mining` 1.0 | Public activation/state service; its game-pack provider owns the sole native detour. Default OFF/unavailable pending native acceptance; resolving the legacy `mining.cell` row does not enable it. |
| `game.relocation` 1.0 | Existing public table with host backend composition and conditional offline admission. |
| `game.datacore_live` 1.0 | Explicit host-backed pool access; no native pool adapter supplied and writes separately opt-in. |
| DataCore packs | Atomic remains default; BestEffort receipts and `clone = { record = "...", mode = "deep" }`. Use `sco-dcb lint <pack>...` for syntax and `sco-dcb check <file.dcb> <pack>...` for database-backed validation. |

See [contracts](contracts.md), [game capability status](game/status.md) and
[release notes](releases/sdk-v2.4.0.md) for authoritative per-operation limits, and
[stability](stability.md) for which tables are Stable or Experimental across SDK 2.x.

## MCP developer workflow

The current-source stdio discovery server uses protocol `2025-11-25`: fourteen
read-only discovery tools by default, including `list_workflows` and `get_workflow`.
`--minimal-tools` selects the original six catalogue tools; `--extended-tools`
and `--workflow-tools` remain compatibility aliases. Discovery exposes recorded
contracts, examples, compatibility, native metadata, manifest/widget validation,
pure scaffolding and guide-section search; it does not invoke game services.
Local build/check tools require a separately configured `--project-root`.
Runtime inspection is a separate default-off launcher with its own six-read allowlist.

Published RC1 retains its six-tool default and twelve tools with `--extended-tools`;
it contains no workflow bundle/index. The six consumer skills (bundle 0.2.1), their
source-bound index and fourteen-tool default belong to updated source/candidate
kits. Use the [consumer workflows](agent-workflows.md), [agent integration](agent-integration.md)
and [SDK/MCP tutorial](sdk-mcp-tutorial.md) for the matching input path.

## Community plugin directory

Plugins stay in their authors' own repositories. The
[community plugin directory](plugin-directory.md) lists them from a validated index
with the SDK release, game-pack version and `sco-plugin-check` result each author
reported, plus listing rules and an issue form for requests. It starts with the
first-party sc-offline and sc-mp plugins and no community entries. A listing is
compatibility information, not an endorsement or in-game verification.

## Remaining release gates

- Reconcile each PR and issue against exact contract, implementation and evidence;
  a proposed API or static locator does not close a runtime gap.
- Validate the final frozen source: ABI/native/binding tests, catalogue, legal and
  documentation gates, five-profile archive checks and reproducibility.
- Require hosted checks on the exact builder commit and retain archive hashes.
- Resolve per-profile provenance and native-index distribution decisions; historical
  approvals do not authorize new bytes. The [native-index restriction](legal/release-decisions/2026-10-14-native-index.md)
  still requires a new applicable human decision for selected candidate metadata.
- For RC2, retain an extracted-kit consumer receipt bound to the exact candidate
  source, archive hashes, workflow index and target host; source-tree tests alone
  do not establish that path or upgrade an installed consumer.
- Release candidate 1 is published (`sdk-v2.4.0-rc.1`, every file attested). Remaining:
  the maintainer's signing, the final `sdk-v2.4.0` tag and publication, and the deployed
  documentation procedure. No approval or in-game result is implied by this plan.
