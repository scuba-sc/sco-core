---
name: sco-plan-sdk-work
description: "Plan and carry out SCO SDK development against the project's current roadmap. Use at the start of extension, product-adoption or SDK-platform work to choose a bounded goal, resolve public contracts, define acceptance and route implementation through the matching workflow."
metadata:
  version: "0.2.0"
---

# Plan and carry out SDK work

Use this as the starting workflow for SCO development. Connect the requested outcome to the current roadmap and public SDK, then continue through implementation and verification. A plan alone does not complete an implementation request.

## Establish the goal and source of truth

Read [roadmap and acceptance decisions](references/roadmap.md) and the shared [input/evidence rules](../sco-build-check-package/references/inputs.md). Identify the task, affected product or extension, intended host/platform and existing owner or issue. Preserve assigned work and explicit deferrals; recheck them before assuming an unclaimed implementation.

Load the canonical roadmap at the selected source revision, the matching release notes, relevant service contracts/status and current task issue when available. Use the matching guide index or public files; retain their source coordinates. An issue may contain newer task-specific acceptance than a released kit, so record that difference. Historical plans are context, not current status. Do not invent roadmap goals, dates, owners or completed capabilities.

Map the work to a specific existing milestone/goal and explain the concrete improvement. If the request is maintenance or lies outside a named roadmap goal, state that honestly and use the user's outcome as its acceptance target. A roadmap connection does not authorize unrelated work, release approval or a product deployment.

## Choose a bounded SDK path

Resolve the required public methods, language mappings, table versions/extents, capability state and lifecycle rules from the selected kit. Build a small matrix of **requested behavior → public contract → implementation/availability → evidence needed**. Include the minimum fallback when a provider is absent or incomplete.

Extensions and product features consume the public SDK. They do not add private core includes, copied engine layouts, raw offsets or duplicate scanners to complete a missing operation. Build the consumer against an extracted plugin SDK when possible; retain compiler dependency/build evidence that distinguishes its public SDK inputs from the source checkout and private product headers. A core/host-source build alone does not prove that a consumer builds from the plugin SDK.

When a public API or backend is missing, separate the consumer fallback/mock from the upstream gap and its prerequisites. Broader default availability does not establish a backend, valid object lifetime or game behavior. Review the requested default against actual provider readiness, fault containment, admission and evidence; report blockers explicitly. Actual core contract/backend work follows repository contributor instructions and its clean-room process. Consumer planning does not authorize private reverse engineering, proprietary evidence disclosure or guessed hooks.

## Define acceptance before implementation

Choose the smallest end-to-end deliverable that advances the selected goal. Record observable behavior, exact command/reply or data result, affected contract and test artifacts, supported platform/host range, failure/fallback behavior and cleanup. Use the roadmap's separate contract, implementation, availability and evidence axes; a successful mock changes only the evidence it actually supplies.

Assign each acceptance check a status and an evidence path. Include archive-only build/check proof for consumer work, focused missing/old/short/NULL provider cases where relevant, and target-host/game checks only when needed and authorized. Resolve prerequisites before dependent work. Finish independent supported work while an external prerequisite remains, preserving an exact blocker rather than claiming the full goal complete.

## Carry the work through

| Work to perform | Continue with |
| --- | --- |
| Create or adapt an extension | [Build, check and package](../sco-build-check-package/SKILL.md) |
| Reproduce a failing consumer | [Debug](../sco-debug-plugin/SKILL.md) |
| Stage or verify a distribution | [Package](../sco-package-plugin/SKILL.md) |
| Change SDK/host compatibility | [Upgrade](../sco-upgrade-plugin/SKILL.md) |
| Request a missing/broken public surface | [Report an SDK gap](../sco-report-sdk-gap/SKILL.md) |

Use existing SDK generators, contracts, helpers and test tools instead of creating parallel API truth. Reconcile the plan if implementation evidence changes the scope. After the final edit, rerun affected checks on the actual final payload and update the acceptance record.

Deliver artifacts and a receipt with the pinned roadmap/source, chosen goal, public SDK inputs, acceptance results and remaining prerequisites. Explain the verified progress toward that goal. A merged contract, default flag, successful build or synthetic provider is not proof of an in-game capability or final release readiness.
