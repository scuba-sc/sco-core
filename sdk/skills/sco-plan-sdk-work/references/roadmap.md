# Roadmap and acceptance decisions

Read this reference when selecting or revising a work item. The authoritative direction is [docs/roadmap.md](https://github.com/scuba-sc/sco-core/blob/main/docs/roadmap.md), together with the selected release notes, service status and actual task issue. Prefer the copy or guide sections bound to the selected kit/source. Repository URLs in packaged Markdown are pinned during packaging; recheck live task ownership and explicitly record newer direction separately from that snapshot.

## Resolve authority and scope

1. Preserve the user's requested outcome and any named issue, owner, handoff or explicit deferral.
2. Record the SDK release/archive/source and the product/host revision independently. Bundle 0.2.0 was authored against SDK 2.4.0; published RC bytes, current source and final approval are distinct.
3. Load the current roadmap at that source. Use `search_guides` and `get_guide_section` when available, retaining their returned provenance and completing truncated sections. Otherwise read the selected public files. If the roadmap is unavailable, continue independently specified work and mark roadmap mapping unresolved; do not invent the missing text.
4. Check relevant release scope, service status and contract records. An older planning record such as `docs/framework.md` is not live issue or product state. A task issue or newer human decision can change direction; record the decision/source and the difference without silently changing the pinned SDK truth.
5. Identify an existing roadmap milestone/section and its concrete objective. A general aspiration is not a completed feature. Preserve issue acceptance criteria and explicit owner deferrals; an SDK bump does not implicitly include deferred native implementation.

At authoring time the public roadmap describes a contributor platform, products/studio built on the SDK, and later engine coverage. These labels help find the relevant sections; they are not a cached assertion of current progress. Resolve the actual section, wording and acceptance from the chosen snapshot each time.

## Prove the SDK boundary

For every required behavior, identify a public contract record or explain the gap. Track supported language mapping, service version and complete member extent, ownership/thread rules, result semantics and cleanup. Distinguish an unavailable provider from a missing public contract. Respect per-method C#/Lua limits instead of assuming every C function is exposed in every language.

Consumer evidence should show an isolated project building from the verified extracted **plugin SDK**, with its own source and ordinary toolchain dependencies. Retain relevant configure/build commands and compiler dependency or include evidence. Private core/product include paths, copied scanners, raw engine layouts and source-tree-only success do not satisfy this boundary. Source-development kits are appropriate for host/core work and produce a different proof.

A plugin may use a documented fallback or a synthetic provider to make progress before native support exists. Record that limitation in behavior and packaging. A missing backend becomes a separate prerequisite or gap report, with the contributor process owning core contracts, implementation and provenance. Do not represent a consumer workaround as completion of the upstream roadmap item.

## Separate readiness axes

| Axis | Record |
| --- | --- |
| Contract | Versioned public declaration, reading rules and language support |
| Implementation | Actual provider: none, synthetic/mock, static-checked native or other explicitly evidenced state |
| Availability | Effective host admission/capability/default and prerequisites; an enabled flag does not create implementation |
| Evidence | Exact executed static, synthetic, target-build, host or in-game checks and their artifacts |

When a task changes defaults, inspect the relevant implementation, lifetime assumptions, fault containment and admission checks before asserting readiness. A human request to enable a capability changes the intended default; it does not supply missing technical evidence. Keep genuine fail-closed behavior and factual blockers visible. Never replace a refusal with success merely to satisfy a default setting.

## Acceptance record

Add these fields to the shared receipt. Use stable source paths and section anchors or issue URLs; retain file/hash identity when available. The acceptance list should contain the actual checks needed for this task, not boilerplate tests that mirror implementation.

```json
{
  "work_item": {"request": null, "issue": null, "owner": null},
  "roadmap": {
    "source_sha": null,
    "document": "docs/roadmap.md",
    "document_sha256": null,
    "goal_reference": null,
    "expected_progress": null,
    "newer_direction": null
  },
  "sdk_boundary": {
    "public_contract_ids": [],
    "consumer_archive_only": "not_run",
    "dependency_evidence": null
  },
  "acceptance": [
    {"criterion": null, "check": null, "expected": null, "status": "not_run", "evidence": null}
  ],
  "prerequisites": [],
  "explicit_deferrals": [],
  "remaining_gates": []
}
```

For each criterion compare the observed result with the predeclared expectation. Preserve failures and skipped target checks with reasons. Report what advanced and what remains; only the proven part counts toward the roadmap goal. Release signing, publication, deployment and game admission retain their own authorization and evidence requirements.
