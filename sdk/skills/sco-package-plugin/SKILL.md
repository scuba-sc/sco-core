---
name: sco-package-plugin
description: "Stage and statically verify a distributable SCO native, Lua or data extension, including single-folder ZIP checks, archive round trips, payload inventories, licenses and manual SC Offline installation/update instructions. Use after a plugin builds or when reviewing a plugin package."
metadata:
  version: "0.2.7"
---

# Package an SCO extension

Start with [roadmap and SDK planning](../sco-plan-sdk-work/SKILL.md), retaining its goal, public-contract boundary and acceptance record through this task.

Apply the version and availability resolution in [input/evidence rules](../sco-build-check-package/references/inputs.md) to the built artifact and intended host; record unknown target compatibility rather than inferring it from the SDK version. Require the final plugin ID/version, platform, SDK/host target and check results. Packaging existing binaries must not be presented as a fresh build. Read [SC Offline boundaries](references/sc-offline.md) only when that product is the target.

1. Stage the plugin ID folder (for example, `pilot_probe/`) in a new empty directory. Include its manifest, required module/scripts/content and applicable license/notices. Keep build trees, debug logs, local databases, secrets and game material out. Check the native binary's actual format and architecture; a generated `.dll` suffix also occurs on macOS/Linux. Label local native bundles `development-<platform>` unless matching target checks exist.
2. Validate final manifest/kind/entry, settings and mandatory plugin IDs. When you need a non-executing folder preflight, run the matching checker as `<checker> <plugin-folder> --no-load`; it checks a non-symlink plugin folder and manifest, manifest rules, folder identity, regular non-symlink entry-file presence, and recognized data-pack inventory without loading or executing plugin code. Data scans skip symlinks/non-regular files, cap at 4,096 indexed files and 16,384 filesystem entries, and stop at 16 script-directory levels; content syntax is not checked. This does not establish code safety, binary ABI compatibility, provider availability or runtime compatibility. `--no-load` cannot be combined with `--cap`, `--short-tables` or `--invoke`. Native/C# still requires matching checker/lifecycle/command assertions; the normal checker mode loads native code. Lua requires entry/modules, Lua 5.4 syntax and stand-in execution; data has no native entry/settings and needs its actual consuming feature's format checks. Indexing data does not prove import. Use the matching kit's docs for DataCore lint/schema checks.
3. Inventory every payload file with relative path, size and SHA-256. Reject symlinks, absolute/traversal paths, duplicate or case-colliding paths and undeclared payloads. Review text for private data. Keep receipt/inventory outside the payload or explicitly exclude their own hashes to avoid cycles. Include licenses/policies before public distribution.
4. Write a single-plugin ZIP with the plugin ID folder at its root, such as `pilot_probe/plugin.ini`; do not wrap it in `data/plugins/`. Before any extraction, run the SDK's static archive verifier from the unpacked SDK root, using its trusted checker (build it with the SDK's documented CMake instructions if needed):

   ```sh
   # Human-readable report
   python3 tools/sco-plugin-verify.py /path/to/pilot_probe.zip --checker out/bin/sco-plugin-check

   # Or save a machine-readable receipt
   python3 tools/sco-plugin-verify.py /path/to/pilot_probe.zip --checker out/bin/sco-plugin-check --json > /path/to/pilot_probe-verification.json
   ```

   On Windows use `python` or `py -3` and `out/bin/sco-plugin-check.exe`. The checker must come from the matching SDK, not the ZIP. Exit code 0 reports `archive-structure-and-manifest-verified`; the human report or JSON receipt records archive and artifact SHA-256 hashes, parsed manifest metadata, and stated dependencies. The verifier checks bounded archive structure and manifest consistency without installing or executing archive entries. It does not authenticate the publisher or source, resolve dependencies, establish trust, inspect native-code safety, or guarantee host/runtime compatibility; hashes identify bytes but are not signatures. A failure means fix and rebuild the archive before continuing. Then independently enumerate ZIP members, refuse unsafe paths, links and collisions, extract into a second empty directory, and compare exact file set, sizes and hashes. Run applicable validation and checker command assertions on the extracted folder, not the original staging directory. The separate `sco-plugin-check` lifecycle path can load native code. Retain the verifier receipt, archive SHA-256 and test outputs outside the plugin payload.
5. Supply INSTALL.md with target host/platform, dependency IDs/versions and order, restart requirements, backups and rollback. Preserve the owner's enabled/disabled choice and settings on update; do not ship or delete local storage. No verified generic plugin ZIP importer/updater exists in SC Offline. Describe manual copying only unless a pinned target proves another route.

Use the receipt template in the evidence reference. Distinguish `package_prepared`, `windows_package_checked`, `launcher_discovered`, `host_loaded` and `in_game`; each later state requires its own observation. Report missing checks as `not_run` with the reason. Deliver the package, inventory, instructions and receipt. Installing, launching or publishing requires authorization in the task.
