# sco-core SDK 2.4.0 roadmap

Historical record: this is the plan behind SDK 2.4.0, kept as it was when 2.4.0 shipped. For the current release and what comes next, read the [roadmap](roadmap.md).

SDK **2.4.0**, optional Star Citizen game pack **0.8.0**, frozen plugin ABI **1.1**. It was published on 2026-11-01 from source commit `6106fa3639cbcc28f78a1ded8de2707de019f4d3`. Its five archive identities are in the [release notes](releases/sdk-v2.4.0.md#stable-sdk-240-archive-identities). Two release candidates came first: RC1 on 2026-10-21 and RC2 on 2026-10-28 (source `844e136ccb280ad8cb43f73bc43c9447e8f6799d`). Both are historical releases with unchanged bytes.

Contract presence, implementation, default availability, synthetic validation and in-game acceptance are separate. This page certifies no game behavior.

## Scope

The roadmap covers the SDK and its developer tooling. Engine, studio and product releases are separate and were not criteria for SDK 2.4.0. Consumers adopt published, checksum-verified archives for their selected release, not integration branches.

## Distribution profiles

| Profile | Stable SDK 2.4.0 archive | Purpose |
|---|---|---|
| core | `sco-sdk-core-2.4.0.zip` | Game-neutral plugin headers, bindings, examples and tools |
| game-sc | `sco-sdk-game-sc-0.8.0.zip` | Optional overlay over the exact compatible core archive |
| legacy | `sco-sdk-2.4.0.zip` | Combined core and game developer kit |
| core-source | `sco-sdk-core-source-2.4.0.zip` | Standalone game-neutral host-source kit |
| host-source-sc | `sco-sdk-host-source-sc-2.4.0.zip` | Standalone host-source kit with optional game implementation |

`sdk/components.json` defines the actual membership. Compatibility metadata binds an overlay to the exact bytes of its base archive. Local packaging does not publish a release.

## Contract inventory

What 2.4.0 added or grew. The release notes and [contracts](contracts.md) hold the per-operation limits, and [stability](stability.md) says which tables are Stable or Experimental.

| Surface | Contract and limits |
|---|---|
| `sco.widgets` 1.3 | 1.2 added stable ASCII tags: a 128-byte node with `const char* tag` at offset 104, copied by the host. 1.3 added numeric slider presentation. Native C and C++ submission and raw C# interop are distinct from typed C# and Lua support. |
| `sco.storage` 1.1 | 144-byte table with bounded table, view and column enumeration. |
| `sco.diagnostics.publisher` 1.0 | Numeric and enum observations through `sco_diagnostics_observation_v1`; no free-form identifiers. |
| `game.loadout` 1.2 | Optional asynchronous submit, status and cancel with state, completed-prefix count and a nested report. Respawn event availability is a separate gate. |
| `game.inventory` 1.2 | The 1.1 prefix supplies copied port snapshots and an item-category vocabulary. 1.2 adds copied logical-layout observations. Native reads are OFF by default. The production host has no logical-layout producer, and `game.inventory.layout` is OFF. |
| `game.inventory.contents` 1.0 | Copied, bounded stored-item snapshots; no grounded native adapter is supplied. |
| `game.audio` 1.1 | Owner-bound trigger and playback tokens with unload cleanup. `play_once` adds fire-and-forget submission under its own default-OFF capability. The native backend is absent, and acceptance does not prove audible playback. |
| `game.entities` 1.2 | Copied, epoch-pinned class registry, debug-name lookup and finite typed component presence. No pointer export and no native lookup adapter. New C# and Lua calls are unsupported; managed layout parity only. |
| `game.world.control` 1.1 | Bounded multi-hit with provider-owned acquire, query and release, validated copied records and owner skips. The native adapter is OFF. |
| `game.actor_stats` 1.1 | 29 raw read ids; stat units and ranges are unverified. The separate write service keeps its original allowlist and default-off admission. |
| `game.input` 1.0 | Named action metadata: 1,297 ids re-checked statically on 4.10.197.2878. It is not input dispatch. |
| `game.gamerules` 1.1 | Static parameter definitions through `list_params`; a live read needs a backend. |
| `game.mining` 1.0 | Public activation and state service whose game-pack provider owns the single native detour. Default OFF and unavailable pending native acceptance; resolving the legacy `mining.cell` row does not enable it. |
| `game.relocation` 1.1 | The 1.0 FullPose route keeps conditional offline admission. The 1.1 position-only request has its own default-OFF capability, and the default native adapter supplies no position callbacks. |
| `game.datacore_live` 1.0 | Explicit host-backed pool access. No native pool adapter is supplied, and writes are a separate opt-in. |
| DataCore packs | Atomic is the default. BestEffort receipts and `clone = { record = "...", mode = "deep" }` are available. `sco-dcb lint <pack>...` checks syntax and `sco-dcb check <file.dcb> <pack>...` validates against a database. |

## MCP developer workflow

The stdio discovery server in 2.4.0 supports protocol versions `2026-07-28` and `2025-11-25`. Its default is fifteen read-only discovery tools, including `list_workflows`, `get_workflow` and `get_service_status`, with the matching schema-2 catalogue and workflow index. `--minimal-tools` selects the original six catalogue tools.

Discovery exposes recorded contracts, examples, compatibility, native metadata, manifest and widget validation, pure scaffolding and guide-section search. It does not invoke game services. Local build and check tools need a separately configured `--project-root`. Runtime inspection is a separate default-off launcher with its own six-read allowlist.

RC2 had a fourteen-tool default. RC1 had six tools, or twelve with `--extended-tools`, and no workflow bundle. Read the selected kit's manifest for its bundle version. The matching guides are [consumer workflows](agent-workflows.md), [agent integration](agent-integration.md) and the [SDK and MCP tutorial](sdk-mcp-tutorial.md).
