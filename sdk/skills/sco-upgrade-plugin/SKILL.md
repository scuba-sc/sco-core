---
name: sco-upgrade-plugin
description: "Migrate an existing SCO SDK consumer between SDK or host versions, checking archive composition, ABI/service prefixes, language mappings, behavior and rollback. Use for plugin dependency upgrades or compatibility regressions."
metadata:
  version: "0.2.4"
---

# Upgrade an SCO consumer

Start with [roadmap and SDK planning](../sco-plan-sdk-work/SKILL.md), retaining its goal, public-contract boundary and acceptance record through this task.

Apply the version and availability resolution in [input/evidence rules](../sco-build-check-package/references/inputs.md) separately to each kit and target. Preserve the last known plugin, input kit hashes, host/product revision and behavior evidence. Pin old and new SDK assets/source separately; never label moving main as published release bytes. Host-source, plugin SDK, service versions, ABI, game pack and product are independent inputs.

Build a small comparison table for the methods actually used: old/new public declarations and contract IDs, minimum version/member extent, language support, ownership/thread/cleanup rules, stability and available runtime capability. Use each kit's matching catalogue/headers, not one server pointed at mismatched roots. Compare exact core/overlay digests before configuring. Consult each selected release's actual contract rather than asserting compatibility from its version number.

Prefer additive service prefixes and optional feature fallback. The frozen root ABI only grows; guard optional/appended root fields and queried table extent/pointers. Keep the manifest consistent with exported built-against ABI info. Retain an appropriate older build when older-host admission is required; size guards alone do not lower the loader minimum. Experimental contracts need explicit review of their documented changes/deprecation window. `check_compatibility` compares metadata, not installed provider behavior or release approval. Respect explicit owner deferrals; missing native backend work is not part of an ordinary SDK bump.

Create a fresh build tree and stage directory for the new kit, retaining the old package for rollback. Use [build/check](../sco-build-check-package/SKILL.md) and the actual language route; regenerate only into a comparison directory, then adapt intentionally rather than overwriting custom code. Recheck old-host fallback, new-host feature behavior, missing/old/short/NULL service cases as relevant, cleanup and exact command replies. C# layout/RID and Lua binding availability need their own checks.

Recheck final staged and freshly extracted bytes using [packaging](../sco-package-plugin/SKILL.md). On a supported owner-run host, compare the named old/new scenarios; otherwise mark host/game gates `not_run`. Prepare manual update/rollback instructions preserving settings and disable markers. A successful SDK bump does not authorize a product host-pin change or release.

Deliver the version/source/hash comparison, changed requirements, test matrix, package receipts and unresolved gates. If a regression is in the public SDK or its docs, retain the minimal evidence for a [gap report](../sco-report-sdk-gap/SKILL.md).
