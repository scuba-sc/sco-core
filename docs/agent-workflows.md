# Consumer workflows for agents

Current source contains six focused skills for developers building extensions, targeting the **SDK 2.4.0** baseline. Check the selected kit’s workflow manifest and index for the actual bundle version and source identity; source changes do not modify the already published RC1/RC2 archives or authorize a release. API truth stays in public headers, contracts and the matching catalogue.

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

The [discovery server](../tools/sco-core-mcp/README.md) exposes all fifteen read-only discovery tools by default, including `get_service_status`, `list_workflows` and `get_workflow`. The following launcher requires a combined `legacy` plugin kit or the `game-sc` overlay composed over its exact matching `core` kit, with both `tools/sco-core-mcp/server.py` and `sdk-catalogue.json` present under `$SDK`. A standalone `core` or `core-source` kit lacks those files; use the direct-file path above. A `host-source-sc` kit includes the server and catalogue but is a host-source input, not a plugin kit:

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

## Cross-repository capability integration inventory

This campaign separates the published baseline from current source. Published RC2 is
SDK 2.4.0 source `844e136ccb280ad8cb43f73bc43c9447e8f6799d`; sco-core `main` at the
campaign start is `0c977d3229012ab7bdf70e17da24dd2189fc4f40`, after the #289 receive-buffer
change. Current engine, offline and multiplayer pins still consume RC2. A current-source
catalogue, mock, signature match or synthetic test does not change those archive bytes or
prove a native provider ran in game.

Use these labels when checking each feature: **mapped** means a public header/contract
names the operation; **synthetic** means a fake provider or stand-in test covers selected
rules; **native** means source includes a game backend; **sigcheck** means rows were
reported resolved on the named executable; **tested** names the test suite that ran. These
states are separate. A mock is not a game provider, and a sigcheck is not gameplay proof.

The offline owner reports an 11-plugin release roster: Creative and Discord are
disabled; Mining is staged with `enabled=false`; `gear_presets` and `loadout` are
development-only; TitanLink and `voxel_bridge` are excluded optional bridges.
The owner also reports five GCC14 plugin build blockers fixed without warning
suppression and all 12 synthetic tests passing. Treat these as product
packaging/default and local test facts. They do not prove the matching core
provider is absent or available, and sc-mp remains a separate consumer.

| Consumer need | Existing sco-core contract and evidence | Integration gap and owner action |
|---|---|---|
| scuba-engine studio and plugin profiles | The frozen plugin root is `sco_api` 1.1 (128 bytes). Game calls use their own versioned `sc_*` tables. Engine #35/#36 added selected profile loading and bounded plugin snapshots; a later owner-reported local engine patch `30067f4` adds the four-plugin app-suite profile and stable `widgets` 1.2 tags through owned node snapshots. Its owner reports 75/76 full-suite tests passing, with the known RC2 eight-peer failure remaining. These local changes are not part of the pinned RC2 source or evidence of native gameplay. | Keep a mock per service separate from native status. Map each enabled profile to exact service versions and full member extents, and add engine-owned mock tables as new public contracts are adopted. Promote one common published kit and matching host pin together; the current engine pin remains RC2. |
| sc-offline build, world and entity writes | The named contracts exist: `game.entities` 1.2 includes spawn/despawn and transform methods; `spawn.entities` 1.2 provides product spawn helpers; `game.world.control` 1.1 includes filtered ray queries. In current core status, lookup rows and filtered-ray support are synthetic/OFF, while entity transform/spawn rows have static sigcheck records. Existing core issues [#223](https://github.com/scuba-sc/sco-core/issues/223), [#224](https://github.com/scuba-sc/sco-core/issues/224) and [#225](https://github.com/scuba-sc/sco-core/issues/225) cover adjacent entity, ray and coordinate surfaces. | Map each Build action to a callable method, capability, fallback and test. The main gaps are provider readiness and target evidence; do not treat methodless sigcheck rows or the synthetic ray provider as live support. Extend or close existing core issues from current product evidence instead of opening duplicate contracts. |
| sc-offline crew, spawn, NPC, seat and flight-ready behavior | The named contracts exist: `game.actors` 1.1, `game.entities` 1.2, `spawn.entities` 1.2 and `game.vehicles` 1.0 contain actor/entity spawn and seat/flight-ready methods. Selected actor/entity/vehicle rows have static sigcheck records; they do not prove each operation worked in game. | Map each Crew/Spawn action to its exact method/provider and an owner test. The main gaps are provider readiness and operation-level evidence. Retain owner-scoped cleanup and session-valid IDs. The existing vehicle contract already names seat/eject/power-on; do not propose a duplicate contract from feature wording alone. |
| sc-offline contracts, mining and creative modules | `game.missions` 1.0 has offer/accept/status/advance/abandon/ingest contracts and a callback/provider seam with synthetic tests; no production Windows provider is installed, and the registry remains OFF. The provider must satisfy admission and the operation-specific callbacks before calls can be ready. `game.mining` 1.0 exists but is OFF; its native adapter is unverified on Windows/game. `game.creative` 1.0 has mapped operations and static signature rows; the offline owner reports its product module disabled. | Keep provider readiness and product enablement distinct. Missions needs a grounded production adapter and readiness evidence; mining needs native verification and target evidence; creative needs an explicit product-policy decision. Do not read the core's static rows as offline plugin policy. |
| sc-offline teleport, quantum, inspector and gear presets | The named tables exist: `game.relocation` 1.1, `teleport.spatial` 1.0, `game.spatial` 1.0 and `game.quantum` 1.0. Position-only relocation is OFF/synthetic; quantum control has a conditional backend and synthetic tests. Inspector reads map across actor, entity, actor-stat, GameRules and inventory contracts. `game.inventory.layout` 1.2 (`snapshot_layout`) exists but is OFF/synthetic and has no production native producer. | Resolve each inspector field and travel action to a versioned method, ready provider and owner test. These are mainly provider/readiness gaps rather than absent contract names. Gear presets require the missing production inventory-layout producer; do not use inventory item-category vocabulary as a substitute. Quantum's conditional backend and synthetic tests are not independent game acceptance. |
| sc-mp multiplayer and hosting | `sco.net` 1.0 supplies peer/channel transport; `game.private_session` 1.0 requests Host/Join/Leave; `game.spatial` supplies copied zone observations with expiring epochs; entity/actor contracts can build product-owned objects. Current contracts do not provide durable world identity or authoritative shared simulation. The current sc-mp lane reports two-client loopback lifecycle tests passing 7/7; its backend remains co-presence/forwarding. | Keep listen host, relay-only transport and authoritative-world hosting as separate modes. Reuse the existing transport/session APIs, then get owner agreement on the minimum context/authority boundary before assigning a contract or ABI. |

For multiplayer review, the minimum context requirement is a read-only, copied, size-prefixed
v1 snapshot that distinguishes local, listen-host and client roles; carries an opaque session
context ID, monotonic epoch, provider-owned world/instance ID and authority peer only while
valid; and invalidates on leave, admission loss, provider stop, or world/authority replacement.
Relay is a transport role, not a world mode. Do not derive IDs from zone handles or endpoints.
The existing `game.private_session` response means a request was accepted, not connected; its
state has no session/world identity. `sco.net` peer IDs are scoped to one transport session,
and `game.spatial` zone IDs/epochs are volatile observations.

A separate authority-provider boundary is a proposal for owner review, not an approved API:
accept only bounded typed intents tied to the current session, epoch, world, entity owner,
schema and tick; validate on the owning game thread; let the authority provider assign
ownership and publish accepted state; and cancel queued work on provider unload. Missing
provider/admission, stale identities, wrong role/direction, malformed or oversized messages,
unknown schemas and unowned entities must fail closed. Agree on the service owner, lifecycle,
version and test evidence before a header is designed; no authority ABI is implemented here.

### Integration order

1. Use current-source `get_service_status` to reconcile a service's static rows with its
   exact schema-2 catalogue digest. It discovers methodless rows; it does not establish a
   configured provider or host runtime state.
2. For each product feature, record the selected published kit and host pin, service/table
   version, member extent, capability/default, provider kind (mock, synthetic, native),
   owner tests, fallback and target evidence still owed. Keep product defaults separate from
   SDK registry defaults.
3. Only after that inventory shows a shared contract is absent should core consider a new
   size-prefixed surface. Preserve the frozen C ABI, opaque/session-scoped IDs and explicit
   owner cleanup. Product owners add their mocks/adapters and tests against the same contract.
4. Move every cooperating repo to one latest **published** kit after a maintainer release
   decision. Until then, new source changes remain proposals and consumers keep their verified
   RC2 pins. In-game validation remains a separate deferred gate.

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
   the matching catalogue/workflow index. Confirm the published RC2 kit's fourteen discovery tools by
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
