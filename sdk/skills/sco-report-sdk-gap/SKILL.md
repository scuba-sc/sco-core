---
name: sco-report-sdk-gap
description: "Prepare a minimal, privacy-safe report of a missing or broken public SCO SDK contract, documentation or tool encountered by a consumer. Use for SDK gaps, unsupported consumer features or upstream handoffs."
metadata:
  version: "0.2.0"
---

# Report a public SDK gap

Start with [roadmap and SDK planning](../sco-plan-sdk-work/SKILL.md), retaining its goal, public-contract boundary and acceptance record through this task.

Read [input/evidence rules](../sco-build-check-package/references/inputs.md). Reduce the desired behavior to the smallest public consumer example. Inspect the selected kit's contracts, language mappings, capability status and examples before declaring an API missing. Search current issues/PRs when connected; distinguish a missing contract, absent provider, unsupported language binding, documentation error and consumer bug. Link likely duplicates and their actual acceptance state.

Prepare a local Markdown draft containing:

- behavior requested and why existing public APIs cannot express it;
- plugin/language/platform/toolchain and independently pinned SDK, archive/catalogue, host and product coordinates;
- minimal author-owned public source and manifest, exact commands/exit codes, expected/actual reply and sanitized log excerpts;
- used contract IDs/declarations, required size/version checks and provider/capability observations;
- evidence labels: static, synthetic, Windows build, launcher discovery, host load and game observation, including `not_run`;
- a bounded proposal or question, alternatives/fallbacks, duplicate links and acceptance criteria.

Keep game binaries/dumps, keys, offsets copied from private investigation, user paths, player/account identifiers and proprietary mod source out of public evidence. Use synthetic identifiers and a minimal reproduction rather than attaching full logs/projects. Security problems follow SECURITY.md and private disclosure, not a public gap issue.

This consumer report establishes no engine facts. When the task explicitly includes contributing a new core service or interoperability-derived finding, follow the repository AGENTS.md and its community-extractor workflow in a clean upstream worktree. That separate process owns provenance, anchors and verification tags. Do not invent private analysis or mark a public mock `VERIFIED_IN_GAME`.

Return the draft and evidence paths, classification, links and remaining verification. Submit only when the user's task authorizes upstream submission; recheck duplicates immediately before doing so. An issue is not release approval, and a merged header is not proof of a working native backend.
