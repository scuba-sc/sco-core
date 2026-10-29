---
name: sco-build-check-package
description: "Build or adapt an SCO SDK extension in C, C++, C# NativeAOT, Lua or a data pack. Use for consumers choosing contracts, scaffolding, building and checking a plugin before distribution."
metadata:
  version: "0.2.3"
---

# Build, check and package an SCO consumer

Start with [roadmap and SDK planning](../sco-plan-sdk-work/SKILL.md), retaining its goal, public-contract boundary and acceptance record through this task.

Produce an author-owned extension with reproducible checks and an explicit target. Keep offline scope and public SDK boundaries; do not introduce raw offsets, private host headers, game material or account data.

## Pin inputs and resolve contracts

Read [input and evidence rules](references/inputs.md) before running kit tools. Record behavior, unique plugin ID, language, target OS/architecture and product/host revision. When no feature is specified, a neutral command with an optional widgets panel is a useful bounded example. Data packs require an existing consumer for their format.

Use the selected kit's public headers, per-method contract records and contract rules. Plugin kits place these records in `contracts/`; source kits retain them in `sdk/contracts/`. References inside records to repository evidence and tests identify source context and may not be local to the kit. Use the matching `sdk-catalogue.json` when present; if a needed input is missing, report the gap. If MCP is available, inspect `tools/list` and its schemas before choosing tools: profiles and versions differ. Use available `list_services`, `get_service_status`, `search_api` (language tokens `c`, `cpp`, `csharp`, `lua`), `get_symbol`, `get_example`, `get_capability_status` and `check_compatibility`; resolve procedures with guide and workflow tools where offered. A service-status result is a static registry snapshot: require `source_binding.state == "matched"` for a schema-2 catalogue, preserve `unbound` for older schema-1 catalogues, and do not infer a live provider from it. Discovery grants no build or runtime access. Direct kit references and CLI tools remain valid when MCP is unavailable. Never mix a moving catalogue with a pinned kit.

For each used method retain record ID, version, whole-member extent, result/buffer semantics, ownership, thread/callback rules and cleanup. Apply documented mandatory root ABI preconditions; guard optional/appended root members and service tables for presence, version, size through the whole pointer and non-NULL callback. `unspecified` supplies no guarantee. Stability, capability availability, signature resolution and game verification are separate facts.

For an empty or partial search, try related feature words and search guides, inspect total matches and request a larger permitted limit if needed, then resolve each candidate by symbol and capability status. A workflow search hit is a discovery hint, not proof that all required operations, language bindings or native providers are available.

## Scaffold and implement

Use an unreserved ID of 1–31 lowercase letters/digits/underscores, consistent across folder, manifest, native info, module and command prefix. Generate into a missing/empty directory using this kit's generator. Review files returned by MCP before writing them. Services requested from the generator are optional lookups, not supplied implementations. Make `requires` mandatory only if the feature cannot work without the named plugin; it takes IDs, not version ranges or URLs.

Read [language and build routes](references/build.md) for exact commands and language-specific limits. Copy data examples only when their actual consuming feature supports the format. Re-query borrowed resources where required; quiesce callbacks/workers and release owned resources on unload. When a missing contract, provider, compatibility/admission condition, package guidance or MCP discovery blocks the requested behavior, notify the developer and prepare a [gap report](../sco-report-sdk-gap/SKILL.md); do not invent a private scanner or silently replace a refusal.

## Validate and exercise

Validate the final manifest and widget nodes using `validate_manifest_and_widgets` when offered; inspect `valid` and all findings. This validates supplied text, not runtime pointers/providers. Otherwise inspect contracts and use the native checker, disclosing missing static validation. Keep negative fixtures outside the shipping tree.

Build, run the generated check target and stage the extension as described in the language reference. Find the actual checker executable, including configuration subdirectories. Recheck the staged folder in full and `--short-tables` root modes and explicitly invoke the feature command. Assert its exact semantic reply (`pong` for the default generated ping), not merely exit 0. Native and C# generated plugins declare API 1.1 to match their exported built-against version. Both root-table modes should pass defensively; a short root table does not establish admission by an API 1.0 host. A deliberately required appended member must disclose any short-mode refusal.

Short-root mode exercises the no-query-service path. It does not simulate a present query returning absent, old, short or NULL-member providers: add focused public-provider fixtures when the feature uses services. Capability flags do not create providers. Use [debugging](../sco-debug-plugin/SKILL.md) for observed failures.

If development MCP is already operator-enabled, its build/check tools can perform the CMake loop within configured roots. Require `passed`, inspect each exit code and output. C# publishing is separate; CTest needs registered tests. Do not enable execution tools or edit operator roots merely to complete this workflow. Native checkers execute plugin code with the user's rights.

## Deliver

Follow [packaging](../sco-package-plugin/SKILL.md), including fresh extraction and rechecks. Return artifact paths and a receipt using [the evidence rules](references/inputs.md). Separate static/synthetic checks, Windows builds, launcher discovery, host loading and game behavior. Preparing a package does not itself authorize installation, publishing or upstream submission.
