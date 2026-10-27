---
name: sco-report-sdk-gap
description: "Prepare a minimal, privacy-safe report of a missing or broken public SCO SDK contract, documentation or tool encountered by a consumer. Use for SDK gaps, unsupported consumer features or upstream handoffs."
metadata:
  version: "0.2.2"
---

# Report a public SDK gap

Start with [roadmap and SDK planning](../sco-plan-sdk-work/SKILL.md), retaining its goal, public-contract boundary and acceptance record through this task.

Apply the version and availability resolution in [input/evidence rules](../sco-build-check-package/references/inputs.md) to the selected kit and target. Reduce the desired behavior to the smallest public consumer example. Inspect the selected kit's headers, available contract records/catalogue, language mappings, capability status, docs and examples before declaring an API missing. Inspect negotiated MCP `tools/list` and schemas: use available discovery tools for contract and status evidence, never assume they inspect a running host or search issues. Search current issues/PRs only through an authorized issue-tracker route; retain links and actual acceptance state, or mark duplicate search `not_run` with the reason.

Classify the first supported finding, recording uncertainty and multiple contributing causes when needed:

| Observed boundary | Classification / next step |
| --- | --- |
| Public contract or requested language binding absent | SDK contract/binding gap; identify the requested operation and supported alternative. |
| Contract present; provider absent or operation unavailable | Provider gap or unavailable capability; record actual provider/default/evidence and a fallback. A synthetic fixture does not create a native adapter. |
| Wrong service version, short whole-member extent or NULL member | Compatibility/member gap or consumer guard error; record requested versus observed values and test a safe fallback. |
| Host admission denied, unknown or not tested | Admission/refusal or missing evidence; retain reason and prerequisites; never bypass a guard or assert why without evidence. |
| Needed contract guidance or example absent from selected archive | Packaged documentation/contract gap; record exact profile, missing path and archive identity. |
| Contract exists but negotiated MCP discovery cannot find or expose it | MCP discovery/schema gap; retain query, schemas, response and direct contract evidence. No result alone does not prove no contract. |
| Public contract works and consumer violates it | Consumer bug; fix and retest the consumer; report upstream only if independently justified. |

Tell the developer the observed state, the specific impact, evidence boundary, usable fallback or exact next step, and whether an upstream gap is established, possible or unconfirmed. Continue independent supported work. Do not promise an implementation or treat an enabled flag, search hit, API header or synthetic test as native/game proof.

Prepare a sanitized local Markdown draft using [the versioned report template](references/report-template.md). Its schema version is independent of the selected SDK and applies across releases; record actual identities or `unknown`, never infer them from this skill bundle. Include:

- behavior requested, whether and how existing public APIs express it, and the observed limiting boundary;
- plugin/language/platform/toolchain and independently pinned SDK source, archive, catalogue and workflow index hashes, host and product coordinates;
- minimal author-owned public source and manifest, exact commands/exit codes, expected/actual reply and sanitized log excerpts;
- used contract IDs/declarations, required size/version checks and provider/capability observations;
- evidence labels: static, synthetic, Windows build, launcher discovery, host load and game observation, including `not_run`;
- a bounded proposal or question, alternatives/fallbacks, duplicate links and acceptance criteria.

Keep game binaries/dumps, keys, offsets copied from private investigation, user paths, player/account identifiers and proprietary mod source out of public evidence. Use synthetic identifiers and a minimal reproduction rather than attaching full logs/projects. Security problems follow SECURITY.md and private disclosure, not a public gap issue.

This consumer report establishes no engine facts. When the task explicitly includes contributing a new core service or interoperability-derived finding, follow the repository AGENTS.md and its community-extractor workflow in a clean upstream worktree. That separate process owns provenance, anchors and verification tags. Do not invent private analysis or mark a public mock `VERIFIED_IN_GAME`.

Return the draft and evidence paths, classification, links and remaining verification. Give the developer the project's [issue intake](https://github.com/scuba-sc/sco-core/issues/new/choose). The known SDK-gap form requests native-handoff evidence and is not a reason to collect game data for a consumer report. If the intake offers a blank issue, the developer may use it after review; if it does not, report that there is no verified ordinary consumer-gap form and ask the maintainer for the appropriate intake route. Do not invent required native evidence. The developer reviews the sanitized draft and chooses whether to submit it; do not automatically send, file, or upload the report. Recheck duplicates before any separately authorized submission. An issue is not release approval, and a merged header is not proof of a working native backend.
