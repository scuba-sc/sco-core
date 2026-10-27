# SDK gap report draft

Copy this template to a new local Markdown file. Replace placeholders with verified, sanitized facts; use `unknown`, `not_run` or `not_applicable` rather than guessing. Review it before sharing. Do not include secrets, account/player data, local user paths, proprietary code, game files/dumps, private offsets or full unreviewed logs. This file is a draft, not an issue submission.

- Report schema: `sco-sdk-gap-report/v1`. This describes the report structure, not an SDK or release version. Preserve this identifier when filling the template; introduce a new schema version if its fields change incompatibly.

## Requested workflow and classification

- Requested behavior and impact: `<what the developer needs>`
- Classification: `<contract/binding | provider/capability | version/extent | admission | package documentation | MCP discovery/schema | consumer bug | unconfirmed>`
- Observed state, expected result and actual result: `<evidence and exact sanitized reply or not_run>`
- Supported fallback / actionable next step: `<what can be done now>`
- Is an SDK extension needed? `<established | possible | no | unknown; why>`

## Inputs and public contract

- SDK version, profile, source SHA, archive SHA-256: `<each independently pinned or unknown>`
- Catalogue SHA-256 and workflow-index SHA-256: `<each or not_present/unknown>`
- Host and product revisions, OS/architecture, compiler/runtime, language and plugin version: `<values or unknown>`
- Service, contract ID and public method, requested/provider version, required/observed whole-member extent, callback presence and capability: `<values or unknown>`
- Admission/default/provider observation: `<observed reason, documented snapshot with source, or not_run>`
- MCP server/source identity, negotiated tool/schema, query and sanitized response: `<when relevant; otherwise not_applicable>`

## Reproduction and evidence

1. `<minimal public inputs and sanitized command>`
2. `<exact step, expected/actual result and exit code>`
3. `<sanitized minimal log excerpt or not_run>`

- Public reproduction and manifest: `<local relative paths, no private source or not_available>`
- Checks: `static=<status>; synthetic=<status>; Windows_build=<status>; launcher_discovery=<status>; host_loaded=<status>; in_game=<status>`
- Limits / unperformed checks and reasons: `<details>`

## Triage and intake

- Duplicate search: `<authorized source, date and links/status; or not_run and reason>`
- Bounded request or question and acceptance criteria: `<no assumed implementation>`
- Attachments reviewed for private data: `<yes/no; list sanitized items>`

Review the draft and use [project issue intake](https://github.com/scuba-sc/sco-core/issues/new/choose) only when you decide to submit. If a blank issue is offered, it can carry an ordinary consumer gap; if not, ask a maintainer for the appropriate route. The native-handoff form requests separate interoperability evidence; do not invent or collect it merely to file this draft. Security vulnerabilities use the repository's private security reporting channel. Do not infer native or in-game behavior from this report.
