# Consumer workflows for agents

Current source contains six focused skills for developers building extensions, targeting the **SDK 2.4.0** baseline. Check the selected kit’s workflow manifest and index for the actual bundle version and source identity; source changes do not modify the already published RC1 archives or authorize a release. API truth stays in public headers, contracts and the matching catalogue.

| Task | Entrypoint |
| --- | --- |
| Plan against roadmap goals and public SDK boundaries | [sco-plan-sdk-work](../sdk/skills/sco-plan-sdk-work/SKILL.md) |
| Create/adapt, scaffold and check | [sco-build-check-package](../sdk/skills/sco-build-check-package/SKILL.md) |
| Reproduce and fix a consumer failure | [sco-debug-plugin](../sdk/skills/sco-debug-plugin/SKILL.md) |
| Stage, inventory and round-trip a distribution | [sco-package-plugin](../sdk/skills/sco-package-plugin/SKILL.md) |
| Move between SDK/host versions | [sco-upgrade-plugin](../sdk/skills/sco-upgrade-plugin/SKILL.md) |
| Prepare a minimal public gap report | [sco-report-sdk-gap](../sdk/skills/sco-report-sdk-gap/SKILL.md) |

Read an entrypoint only when its task applies, then its relevant references. These are portable Markdown skills; open the files directly or follow your agent client's supported skill installation process. The SDK never writes agent configuration or automatically installs/activates them. Keep the complete bundle together because workflows share references.

For direct-file use, give an agent with access to the extracted kit the path
`<SDK>/skills/sco-plan-sdk-work/SKILL.md` and ask it to read that file and the
relevant linked references for the task. The other five entrypoints are siblings
under `<SDK>/skills/`; preserve the entire `skills/` directory, including the
manifest and reference files, so cross-workflow links resolve. In a source kit the
corresponding path is `<source-root>/sdk/skills/sco-plan-sdk-work/SKILL.md`.
Reading these files provides instructions to the agent; it does not install or
activate a skill in a client. Client-specific installation, if desired, follows
that client's own instructions and requires access to the complete bundle.

## Archive and source discovery

`core` and `legacy` kits place the bundle under `skills/`; source kits preserve `sdk/skills/`. Each base kit also has `sdk-workflows.json`. The `game-sc` overlay uses its exact matching core kit's workflow bundle and overwrites none of it. The index embeds entrypoint/reference text, descriptions, bundle/baseline/SDK/source coordinates and SHA-256 hashes of the **actual packaged Markdown after link rewriting**. SHA256SUMS, ATTRIBUTIONS and the SPDX SBOM also cover these files. Hashes identify bytes; human review/release approval remains separate.

The [discovery server](../tools/sco-core-mcp/README.md) exposes all fourteen read-only discovery tools by default, including `list_workflows` and `get_workflow`. The following launcher requires a combined `legacy` plugin kit or the `game-sc` overlay composed over its exact matching `core` kit, with both `tools/sco-core-mcp/server.py` and `sdk-catalogue.json` present under `$SDK`. A standalone `core` or `core-source` kit lacks those files; use the direct-file path above. A `host-source-sc` kit includes the server and catalogue but is a host-source input, not a plugin kit:

```sh
python3 "$SDK/tools/sco-core-mcp/server.py" --catalogue "$SDK/sdk-catalogue.json" --repo-root "$SDK"
```

The catalogue and workflow index must have exactly matching SDK/source coordinates; a missing, oversized, malformed, hash-inconsistent or mismatched index refuses startup when an index is supplied or present. If the default index is missing, the workflow tools remain listed and return a precise unavailable error. The operator can select an index with `--workflows <file>`. Tool calls cannot select arbitrary filesystem files: `get_workflow` serves only embedded indexed text. `list_workflows {}` returns IDs/descriptions/entry paths; `get_workflow {"id":"sco-build-check-package"}` reads the entrypoint and lists resource paths/hashes. Pass a returned `path` with that ID to read one reference. Cross-workflow links are resolved by requesting the linked workflow's ID.

Use `--minimal-tools` for the original six catalogue tools. The old `--extended-tools` and `--workflow-tools` flags remain compatibility aliases. Local build/check tools are offered when an operator configures `--project-root`; fixed commands, confinement and resource limits still apply. Runtime inspection retains its independent admission requirements. Check the selected archive inventory for its catalogue, server and workflow bundle; direct skill-file reading is independent of MCP availability. Combined or matching core+game-sc kits support MCP discovery. Core-source/host-source are distinct source build inputs.

For a source checkout, build a matching catalogue using its existing instructions, then:

```sh
python3 tools/sdk_workflows.py --root . --out /path/beside/catalogue/sdk-workflows.json
```

The builder takes inputs only from the explicit `sdk/skills/manifest.json` allowlist, validates frontmatter/version/links and rejects symlink inputs. Run without `--out` to validate. `sdk/package.py` generates the index from the selected transformed payload. Do not use dirty-source output as evidence that published bytes match a commit.

## RC2 consumer reconciliation

This is the bounded consumer-workflow portion of [SDK 2.4.0 preparation](roadmap-2.4.0.md).
RC2 was published on 2026-10-28 and is the latest GitHub release. Its tag points
to release source commit `844e136ccb280ad8cb43f73bc43c9447e8f6799d`; current
`main` may be newer, and later source edits do not change the published archive
bytes. The prepublication RC2 state remains in the
[historical notes](releases/sdk-v2.4.0.md#historical-rc2-candidate-status-before-publication).
Published archives, the product's host revision and any final release approval
have separate identities. The following
issues explain demand for contracts already present in source. Their proposals
are not API declarations, and this reconciliation does not close them or certify
their full acceptance.

| Request | Current public contract and remaining boundary | Source checks to retain |
| --- | --- | --- |
| [#189](https://github.com/scuba-sc/sco-core/issues/189), GameRules definitions | `game.gamerules` 1.1 appends `list_params`: 15 static definitions, with names/groups verified on 4.10.197.2878 and types unverified. Check the table extent and member before calling. Metadata does not require the default-OFF live-read backend and supplies no server values. [Contract](game/gamerules.md#parameter-registry-list_params-11) | `tests/abi_game_gamerules.c`, `tests/test_gamerules_params.cpp` |
| [#190](https://github.com/scuba-sc/sco-core/issues/190), inventory categories | `game.inventory` 1.1 exposes 192 build-scoped categories and display helpers; `item_class_id` remains a category ordinal. C# `Sco.Sdk.Game.Inventory` and Lua `sco.game.inventory` provide guarded copied snapshots and static vocabulary, preferring 1.1 with guarded 1.0 prefix fallback and explicit tail absence. Unknown categories keep the exact raw uint64 value separately from UNKNOWN (C# `ulong`; Lua integer or upper-half decimal string). Names do not establish live availability; native reads remain OFF by default. [Snapshot and language contracts](game/inventory.md#c-and-lua-consumers) | `tests/abi_game_inventory.c`, `tests/test_inventory.cpp`, `tests/test_inventory_engine.cpp`, `sdk/csharp/Sco.Sdk.Tests/GameFakeHost.cs`, `tests/game_contracts/lua_cases.inc`, `tools/test_sdk_catalogue.py` |
| [#192](https://github.com/scuba-sc/sco-core/issues/192), partial pack application | `PackApplyPolicy::BestEffort` is explicit opt-in with `PackApplyReceipt`; Atomic remains default. `sco-dcb lint <pack>...` checks syntax; `sco-dcb check <file.dcb> <pack>...` also needs the database. The issue's one-input example is not the implemented CLI. [Pack contract](datacore.md#packs-lint-check-show-diff) | `tests/test_datacore_pack.cpp`, `tests/dcb_pack.cmake` |
| [#194](https://github.com/scuba-sc/sco-core/issues/194), action vocabulary | `game.input` 1.0 has **1,297** actions, ids 1..1297, re-derived on 4.10.197.2878. The older 1,267-member proposal is superseded by source evidence. Lookup is metadata only; no input dispatch or binding API is implied. [Contract](game/input.md) | `tests/abi_game_input.c`, `tests/test_input.cpp` |
| [#195](https://github.com/scuba-sc/sco-core/issues/195), respawn completion | `game.loadout.respawn_completed` deliberately carries only the size-prefixed 8-byte occurrence marker (`size`, padding), with occurrences within a tick coalesced. It has no proposed entity/result fields and is not per-slot success. The producer seam is tested; the native hook remains unresolved and the capability OFF. [Lifecycle](game/loadout.md#respawn-completed-event-gameloadoutrespawn_completed) | `tests/abi_game_events.c`, `tests/game_contracts/loadout/test_loadout.cpp` |
| [#140](https://github.com/scuba-sc/sco-core/issues/140), host-source distribution | The standalone `host-source-sc` profile exists in the five-profile manifest and published RC2 inventory. Use the release archive and its independently recorded identity. A host-source build proves host composition; plugins separately consume the plugin SDK. [Kit guide](host-source-sc.md) | `sdk/test_host_source.py` and the selected archive's checksums/compatibility metadata |

Additional current-source contracts are not contained in published RC1:

| Surface | Consumer checks and remaining boundary |
| --- | --- |
| [Inventory layout 1.2](game/inventory.md#logical-layout-observations-12) | Query 1.2, check the complete `snapshot_layout` member and `game.inventory.layout`, then validate copied observations. The production host has no logical-layout producer; the capability is OFF. |
| [ASOP selection 1.0](game/asop-selection.md) | Query the service and each complete member, and require `game.asop.selection.file` and a ready host coordinator. Editing the host-owned document does not establish native terminal publication or observation. C# and Lua are unsupported. |
| [Position-only relocation 1.1](game/relocation-position.md) | Query 1.1, check the complete `request_position` member and `game.relocation.position`, and preserve offline/owner and observation guards. The default native adapter supplies no position callbacks; the capability is OFF. |
| [Widgets 1.3](../sdk/docs/widgets.md#numeric-presentation-13) | Query 1.3, check the full member extent and numeric-presentation feature before submitting the extended node. Host and language support are independent of the SDK used to compile. |

These boundaries are source descriptions; a built candidate does not establish
that the installed product host supplies the capabilities. Synthetic
fixtures are not gameplay acceptance.

For example, the [quantum contract](quantum.md#availability-and-evidence)
describes a conditional Windows game-pack adapter in this source. Its existence
does not establish that a particular product host has configured it, passed
admission, or verified it in game. Check the product's exact host source pin and
observed capability separately from the plugin kit version; an unavailable or
untested provider does not by itself mean that an adapter is absent from source.

For an RC2 consumer evaluation:

1. Select a bounded author-owned plugin task with `sco-plan-sdk-work`. Record the
   roadmap revision/hash, public contracts, supported language route and expected
   unavailable-provider fallback. Do not add another native hook or scanner to
   bypass a missing SDK backend; the mining service owns its detour.
2. Select the published RC2 archive and verify its release `SHA256SUMS` and
   inventory before extracting it into an empty directory. Compose `game-sc` only over its exact matching core
   archive; keep host-source kits separate. Read the six skills directly or use
   the matching catalogue/workflow index. Confirm fourteen discovery tools by
   default and six with `--minimal-tools`; runtime tools require their independent
   admission path.
3. Carry the task through the matching build/debug/package/upgrade workflow. Keep
   actual MCP requests/replies, compiler dependency evidence showing extracted
   public plugin-SDK inputs, `sco-plugin-check` results, payload inventory and
   round-trip archive checks. A source-tree or host-source build alone does not
   prove this consumer path.
4. Record the target product/host revision independently. Check service version,
   complete member extent, capability and provider readiness on that host. Retain
   old/short/missing-provider refusals where the task depends on a newer tail.
   A newer kit cannot upgrade the installed host, and a successful synthetic
   provider is not game acceptance.

Use the [planning receipt](../sdk/skills/sco-plan-sdk-work/references/roadmap.md#acceptance-record)
and [input/evidence receipt](../sdk/skills/sco-build-check-package/references/inputs.md#receipt-template).
Mark unexecuted Windows, launcher, host-load and game checks separately; **not run
in game** remains the boundary here. Final candidate checks on the exact builder
commit and publication remain distinct. The [RC2 metadata decision](legal/release-decisions/2026-10-27-rc2-discovery-metadata.md)
records the native-index and registry scope for those RC2 bytes.

## Verification and maintenance

Run `python3 tools/test_sdk_workflows.py`, the MCP test suite, and packaging tests when changing workflows/discovery. The workflow tests check deterministic indexes, malformed inputs, transformed archive byte hashes, composition, checksums and actual stdio retrieval/refusals. Existing archive example proofs exercise language/toolchain paths. Start each task with `sco-plan-sdk-work`: bind its roadmap source/hash and goal to measurable acceptance, record public contract IDs, prove dependencies use the extracted plugin SDK, then route to implementation. The canonical roadmap ships as `docs/roadmap.md`; no hand-copied status snapshot is maintained in the skills. A fresh-agent evaluation should use only an extracted published kit and a bounded author-owned task, record commands and exact replies, and mark Windows/launcher/game checks separately.

Change bundle version when instructions/resources change; the registry and each entrypoint must agree. Do not copy API declarations into skills. Preserve the published RC/main/final-release distinction and explicit owner deferrals during upgrades. The generic host API also defaults external plugins on when the host supplies a
nonempty plugin root. Explicit disable markers/host policy still apply; an empty
root never discovers the current directory. [Backend readiness](defaults-readiness.md)
reviews all 37 recorded OFF capabilities and each required safeguard. APIs remain
truthful about missing providers and game evidence.

See [agent integration](agent-integration.md) and [contributor rules](../CONTRIBUTING.md).
