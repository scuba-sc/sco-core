---
name: sco-package-plugin
description: "Stage and verify a distributable SCO native, Lua or data extension, including archive round trips, payload inventories, licenses and manual SC Offline installation/update instructions. Use after a plugin builds or when reviewing a plugin package."
metadata:
  version: "0.2.4"
---

# Package an SCO extension

Start with [roadmap and SDK planning](../sco-plan-sdk-work/SKILL.md), retaining its goal, public-contract boundary and acceptance record through this task.

Apply the version and availability resolution in [input/evidence rules](../sco-build-check-package/references/inputs.md) to the built artifact and intended host; record unknown target compatibility rather than inferring it from the SDK version. Require the final plugin ID/version, platform, SDK/host target and check results. Packaging existing binaries must not be presented as a fresh build. Read [SC Offline boundaries](references/sc-offline.md) only when that product is the target.

1. Stage into a new empty directory. Include exactly one intended plugin subtree, its manifest, required module/scripts/content and applicable license/notices. Keep build trees, debug logs, local databases, secrets and game material out. Check the native binary's actual format and architecture; a generated `.dll` suffix also occurs on macOS/Linux. Label local native bundles `development-<platform>` unless matching target checks exist.
2. Validate final manifest/kind/entry, settings and mandatory plugin IDs. Native/C# requires matching checker/lifecycle/command assertions. Lua requires entry/modules, Lua 5.4 syntax and stand-in execution; data has no native entry/settings and needs its actual consuming feature's format checks. Indexing data does not prove import. Use the matching kit's docs for DataCore lint/schema checks.
3. Inventory every payload file with relative path, size and SHA-256. Reject symlinks, absolute/traversal paths, duplicate or case-colliding paths and undeclared payloads. Review text for private data. Keep receipt/inventory outside the payload or explicitly exclude their own hashes to avoid cycles. Include licenses/policies before public distribution.
4. Write the ZIP with the documented install subtree. Enumerate members before extraction; refuse unsafe paths, links and collisions. Extract into a second empty directory and compare exact file set, sizes and hashes. Run applicable validation and checker command assertions on the extracted folder, not the original staging directory. Retain archive SHA-256 and test outputs.
5. Supply INSTALL.md with target host/platform, dependency IDs/versions and order, restart requirements, backups and rollback. Preserve the owner's enabled/disabled choice and settings on update; do not ship or delete local storage. No verified generic plugin ZIP importer/updater exists in SC Offline. Describe manual copying only unless a pinned target proves another route.

Use the receipt template in the evidence reference. Distinguish `package_prepared`, `windows_package_checked`, `launcher_discovered`, `host_loaded` and `in_game`; each later state requires its own observation. Report missing checks as `not_run` with the reason. Deliver the package, inventory, instructions and receipt. Installing, launching or publishing requires authorization in the task.
