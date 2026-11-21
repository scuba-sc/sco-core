# Skills for AI agents that build plugins

Seven Markdown skills tell an AI agent how to plan SDK work, build, check, debug, package and upgrade a plugin, report an SDK gap, and build a launcher. Start every task with `sco-plan-sdk-work`.

<!-- release-block:current-release -->
SDK **2.4.1** (release candidate 1, a pre-release for testing; the final release follows) uses Star Citizen game pack **0.8.1** (checked against Star Citizen
Alpha 4.10.3, build 4.10.200.58746; see [game builds](../docs/game/builds.md)) and plugin ABI **1.1**. Download it from the
[`sdk-v2.4.1-rc.1` release page](https://github.com/scuba-sc/sco-core/releases/tag/sdk-v2.4.1-rc.1) and verify each archive against that release's
`SHA256SUMS`; the [release notes](https://github.com/scuba-sc/sco-core/blob/sdk-v2.4.1-rc.1/docs/releases/sdk-v2.4.1.md) list what changed. The previous release is
[SDK 2.4.0](https://github.com/scuba-sc/sco-core/blob/sdk-v2.4.0/docs/releases/sdk-v2.4.0.md).
<!-- /release-block:current-release -->

Check each extracted kit's `COMPATIBILITY.json` and `VERSION` for its profile and versions.

## The seven skills

| Task | Entrypoint |
| --- | --- |
| Plan against roadmap goals and public SDK boundaries | [sco-plan-sdk-work](../sdk/skills/sco-plan-sdk-work/SKILL.md) |
| Create/adapt, scaffold and check | [sco-build-check-package](../sdk/skills/sco-build-check-package/SKILL.md) |
| Reproduce and fix a consumer failure | [sco-debug-plugin](../sdk/skills/sco-debug-plugin/SKILL.md) |
| Stage, statically verify, inventory and round-trip a distribution | [sco-package-plugin](../sdk/skills/sco-package-plugin/SKILL.md) |
| Move between SDK/host versions | [sco-upgrade-plugin](../sdk/skills/sco-upgrade-plugin/SKILL.md) |
| Prepare a minimal public gap report | [sco-report-sdk-gap](../sdk/skills/sco-report-sdk-gap/SKILL.md) |
| Build a launcher for a product on the SDK (contract, reference launcher, what a product brings itself) | [sco-build-launcher](../sdk/skills/sco-build-launcher/SKILL.md) |

An agent reads an entrypoint only when its task applies, then the references that entrypoint links. The SDK never writes agent configuration and never installs a skill.

## Give the skills to an agent

### Read the files

Give the agent `<SDK>/skills/sco-plan-sdk-work/SKILL.md` and ask it to read that file and the references it links (in a source checkout, `sdk/skills/sco-plan-sdk-work/SKILL.md`). The other six entrypoints are siblings. Keep the whole `skills/` folder, including `manifest.json` and the reference files, so the links resolve.

The `core` and `legacy` kits place the skills under `skills/`. The source kits keep `sdk/skills/`. Each base kit also has `sdk-workflows.json`, an index that embeds the skill text with SHA-256 hashes of the packaged Markdown. The `game-sc` overlay uses the index of its exact matching `core` kit and overwrites none of it.

### Read them over MCP

The [discovery server](../tools/sco-core-mcp/README.md) offers `list_workflows` and `get_workflow` in its default profile. Start it from a combined `legacy` kit, or a `game-sc` overlay composed over its matching `core` kit, with `tools/sco-core-mcp/server.py` and `sdk-catalogue.json` under `$SDK`:

```sh
python3 "$SDK/tools/sco-core-mcp/server.py" --catalogue "$SDK/sdk-catalogue.json" --repo-root "$SDK"
```

A standalone `core` or `core-source` kit has no server or catalogue: read its files directly. A `host-source-sc` kit has both, but it is a build input for a host, not a plugin kit. [Agent integration](agent-integration.md) covers client setup.

- `list_workflows {}` returns each workflow's ID, description and entry path.
- `get_workflow {"id":"sco-build-check-package"}` returns the entrypoint and lists its references with their hashes. Pass a returned `path` with the same ID to read one reference. To follow a link to another workflow, request that workflow's ID.
- `get_workflow` serves only the text embedded in the index. It cannot read other files.

The index must match the catalogue's SDK and source coordinates. Without a valid index the two tools are not listed. If you select an index with `--workflows <file>` and it is missing, malformed, altered or from another source, the server prints a warning on stderr and disables only those two tools. `--minimal-tools` offers only the six catalogue tools.

### Rust

The Rust route is manual. The `core` and combined kits of SDK 2.4.1 contain `rust/README.md` and `examples/rust_hello`; SDK 2.4.0, the source kits and the game overlay do not. The CLI scaffolder and the MCP scaffolder do not generate Rust, and MCP language filters accept only `c`, `cpp`, `csharp` and `lua`.

## Build the index in a checkout

Build a catalogue first, as the [server README](../tools/sco-core-mcp/README.md#run-from-a-source-checkout-maintainers) describes. Then:

```sh
python3 tools/sdk_workflows.py --root . --out /path/beside/catalogue/sdk-workflows.json
```

The builder reads only the files listed in `sdk/skills/manifest.json`, checks frontmatter, versions and links, and refuses symlinks. Run it without `--out` to validate. `sdk/package.py` builds the index for each kit from the packaged files.

## Read contract status

Use these labels when you check a feature:

- **mapped**: a public header or contract names the operation.
- **synthetic**: a fake provider or stand-in test covers selected rules.
- **native**: the source includes a game backend.
- **sigcheck**: signature rows were reported resolved on the named executable.

A mock is not a game provider and a sigcheck is not gameplay proof. Call `get_service_status` first: it returns the static capability rows of a service, including rows without methods, and its `source_binding.state` must be `matched` before you rely on them. Not run in game. See [game capability status](game/status.md).

## Contracts with their own checks

Query the service version and check the complete member extent before you call any of these.

| Surface | Check |
| --- | --- |
| [Inventory layout 1.2](game/inventory.md#logical-layout-observations-12) | Check the `snapshot_layout` member and the `game.inventory.layout` capability, then validate the copied observations. The production host has no layout producer and the capability is OFF. |
| [ASOP selection 1.0](game/asop-selection.md) | Check each member, the `game.asop.selection.file` capability and a ready host coordinator. C# and Lua are unsupported. |
| [Position-only relocation 1.1](game/relocation-position.md) | Check the `request_position` member and the `game.relocation.position` capability. The default native adapter supplies no position callbacks and the capability is OFF. |
| [Widgets 1.3](../sdk/docs/widgets.md#numeric-presentation-13) | Check the numeric-presentation feature before you submit the extended node. |
| [Widgets 1.4](../sdk/docs/widgets.md#canonical-callback-tags-14) | New in SDK 2.4.1. A 1.4 host passes the canonical node tag in a guarded `sco_widget_event_1_4` callback tail. Check `SCO_WIDGETS_FEATURE_EVENT_TAG` and `event->size` before you read the tag. Older hosts deliver numeric-only events. |
| [Launcher contract](launcher-contract.md) | What a launcher and the host kit must agree on. Used by `sco-build-launcher`. |
| [Host menu reader](widgets-view.md) | `sco.widgets.view` is a host-source seam. It is in the `core-source` profile and no plugin kit. |

## Maintain the skills

- Change `bundle_version` in `sdk/skills/manifest.json` and the `metadata.version` of every `SKILL.md` together whenever instructions or resources change. The builder refuses a mismatch.
- Do not copy API declarations into a skill. Link to the contract.
- Write MCP guidance that takes arguments as `tool_name(argument, ...)`. The builder checks backticked tool names and these calls against the input schemas of `tools/sco-core-mcp/server.py` and the schema hashes pinned in the manifest.
- After an intentional schema change, review the skill text, then run `python3 tools/sdk_workflows.py --generate` followed by `python3 tools/sdk_workflows.py --check`. Generation rewrites only the generated tool table in `sdk/skills/sco-build-check-package/references/inputs.md` and the pins in the manifest.
- Run `python3 tools/test_sdk_workflows.py` and the MCP tests after a change.

See [agent integration](agent-integration.md) and the [contributor rules](../CONTRIBUTING.md).
