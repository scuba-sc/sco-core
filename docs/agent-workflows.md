# Consumer workflows for agents

The SDK ships six focused skills for developers building extensions. Bundle **0.2.0** targets the **SDK 2.4.0** baseline; this source addition does not modify the already published RC1 archives or authorize a release. API truth stays in public headers, contracts and the matching catalogue.

| Task | Entrypoint |
| --- | --- |
| Plan against roadmap goals and public SDK boundaries | [sco-plan-sdk-work](../sdk/skills/sco-plan-sdk-work/SKILL.md) |
| Create/adapt, scaffold and check | [sco-build-check-package](../sdk/skills/sco-build-check-package/SKILL.md) |
| Reproduce and fix a consumer failure | [sco-debug-plugin](../sdk/skills/sco-debug-plugin/SKILL.md) |
| Stage, inventory and round-trip a distribution | [sco-package-plugin](../sdk/skills/sco-package-plugin/SKILL.md) |
| Move between SDK/host versions | [sco-upgrade-plugin](../sdk/skills/sco-upgrade-plugin/SKILL.md) |
| Prepare a minimal public gap report | [sco-report-sdk-gap](../sdk/skills/sco-report-sdk-gap/SKILL.md) |

Read an entrypoint only when its task applies, then its relevant references. These are portable Markdown skills; open the files directly or follow your agent client's supported skill installation process. The SDK never writes agent configuration or automatically installs/activates them. Keep the complete bundle together because workflows share references.

## Archive and source discovery

`core` and `legacy` kits place the bundle under `skills/`; source kits preserve `sdk/skills/`. Each base kit also has `sdk-workflows.json`. The `game-sc` overlay uses its exact matching core kit's workflow bundle and overwrites none of it. The index embeds entrypoint/reference text, descriptions, bundle/baseline/SDK/source coordinates and SHA-256 hashes of the **actual packaged Markdown after link rewriting**. SHA256SUMS, ATTRIBUTIONS and the SPDX SBOM also cover these files. Hashes identify bytes; human review/release approval remains separate.

The [discovery server](../tools/sco-core-mcp/README.md) exposes all fourteen read-only discovery tools by default, including `list_workflows` and `get_workflow`:

```sh
python3 "$SDK/tools/sco-core-mcp/server.py" --catalogue "$SDK/sdk-catalogue.json" --repo-root "$SDK"
```

The catalogue and workflow index must have exactly matching SDK/source coordinates; a missing, oversized, malformed, hash-inconsistent or mismatched index refuses startup when an index is supplied or present. If the default index is missing, the workflow tools remain listed and return a precise unavailable error. The operator can select an index with `--workflows <file>`. Tool calls cannot select arbitrary filesystem files: `get_workflow` serves only embedded indexed text. `list_workflows {}` returns IDs/descriptions/entry paths; `get_workflow {"id":"sco-build-check-package"}` reads the entrypoint and lists resource paths/hashes. Pass a returned `path` with that ID to read one reference. Cross-workflow links are resolved by requesting the linked workflow's ID.

Use `--minimal-tools` for the original six catalogue tools. The old `--extended-tools` and `--workflow-tools` flags remain compatibility aliases. Local build/check tools are offered when an operator configures `--project-root`; fixed commands, confinement and resource limits still apply. Runtime inspection retains its independent admission requirements. Core-only plugin kits have the skill files/index but no catalogue/server; direct file reading works. Combined or core+game-sc kits support MCP discovery. Core-source/host-source are distinct source build inputs.

For a source checkout, build a matching catalogue using its existing instructions, then:

```sh
python3 tools/sdk_workflows.py --root . --out /path/beside/catalogue/sdk-workflows.json
```

The builder takes inputs only from the explicit `sdk/skills/manifest.json` allowlist, validates frontmatter/version/links and rejects symlink inputs. Run without `--out` to validate. `sdk/package.py` generates the index from the selected transformed payload. Do not use dirty-source output as evidence that published bytes match a commit.

## Verification and maintenance

Run `python3 tools/test_sdk_workflows.py`, the MCP test suite, and packaging tests when changing workflows/discovery. The workflow tests check deterministic indexes, malformed inputs, transformed archive byte hashes, composition, checksums and actual stdio retrieval/refusals. Existing archive example proofs exercise language/toolchain paths. Start each task with `sco-plan-sdk-work`: bind its roadmap source/hash and goal to measurable acceptance, record public contract IDs, prove dependencies use the extracted plugin SDK, then route to implementation. The canonical roadmap ships as `docs/roadmap.md`; no hand-copied status snapshot is maintained in the skills. A fresh-agent evaluation should use only an extracted candidate kit and a bounded author-owned task, record commands and exact replies, and mark Windows/launcher/game checks separately.

Change bundle version when instructions/resources change; the registry and each entrypoint must agree. Do not copy API declarations into skills. Preserve the published RC/main/final-release distinction and explicit owner deferrals during upgrades. Generated metadata and new workflow sources remain `needs-review` until a human completes provenance review. The generic host API also defaults external plugins on when the host supplies a
nonempty plugin root. Explicit disable markers/host policy still apply; an empty
root never discovers the current directory. [Backend readiness](defaults-readiness.md)
reviews all 37 recorded OFF capabilities and each required safeguard. APIs remain
truthful about missing providers and game evidence.

See [agent integration](agent-integration.md) and [contributor rules](../CONTRIBUTING.md).
