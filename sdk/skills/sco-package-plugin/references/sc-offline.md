# SC Offline distribution boundary

Product boundary reference, examined 2026-10-25 at `scuba-sc/sc-offline` commit `0092dd99a768b0af9084314bc655a8cb13cd2155`. Recheck the intended product release before installation claims. Sources: [launcher guide](https://github.com/scuba-sc/sc-offline/blob/0092dd99a768b0af9084314bc655a8cb13cd2155/docs/launcher.md), [launcher implementation](https://github.com/scuba-sc/sc-offline/blob/0092dd99a768b0af9084314bc655a8cb13cd2155/launcher/launcher.cpp), [SDK workflow](https://github.com/scuba-sc/sc-offline/blob/0092dd99a768b0af9084314bc655a8cb13cd2155/docs/sdk-workflows.md).

## Supported extension format

The product is the offline mod/launcher/host. Author extensions use SCO's `native`, `lua` or `data` plugin kinds. There is no separate generic mod manifest/import path established by these sources. Data-only mods can use `kind = data` when an existing feature consumes their content.

The Plugins page scans immediate `data/plugins/<id>/` directories with `plugin.ini`, approximately every three seconds. It displays metadata; this is not complete host manifest, dependency or ABI validation. Its switch toggles the `disabled` marker for the next game start. The host performs authoritative load/refusal checks. `plugins = on` is the current product default in code; explicitly check the owner's configuration rather than relying on contradictory SDK guide defaults. The launcher cannot execute in-game `sco.reload`.

For a Windows native example, prepare a distribution ZIP whose install subtree is:

```text
data/plugins/pilot_probe/plugin.ini
data/plugins/pilot_probe/pilot_probe.dll
data/plugins/pilot_probe/LICENSE
```

This ZIP layout is a proposed manual distribution convention around the existing folder contract. The launcher has no standalone ZIP import route verified here. Deliver `INSTALL.md`, a payload hash list and an evidence receipt beside the ZIP. The user extracts to a new staging directory, inspects it, then copies its `data/plugins/pilot_probe` folder into the product's own `data/plugins` with the game stopped. Do not install in StarCitizen/Bin64. Do not include or replace sc-offline.exe, dinput8.dll or sc-offline.ini.

Native payloads must be built for Windows x64 to label them installable in the supported game product. A local macOS/Linux build may retain `.dll` because the SDK CMake helper uses that suffix; label any such ZIP `development-<actual-platform>` and do not give it Windows installation status. Lua includes its declared entry and modules; data includes manifest/content only, without native entry. Include applicable licences/notices.

`plugin.ini` remains the existing authority for ID/version/API/kind/entry, `requires` and settings. `requires` accepts names, not dependency version ranges or repository/download URLs. Mandatory dependencies must already exist; package instructions must list their IDs/versions and installation order. Check cycles/provider failures in an actual host or suitable focused fixture; the single-folder checker cannot prove the entire installed graph. Optional services require fallback. SDK COMPATIBILITY.json describes SDK composition, not a launcher-consumed plugin compatibility descriptor.

## Install and update handoff

For an explicitly authorized later install: record product release/host revision, stop the game, back up the prior plugin folder, preserve its enable/disable decision, replace only this plugin folder, check the Plugins page and host mod.log, then invoke the named command through a supported host UI. The plugin settings database is `data/storage/<id>.db`; do not ship it or delete it during an update. A changed setting declaration may invalidate saved values. Record restart/reload requirements; pack/DataCore/manifest changes need restart. A faulted provider also needs restart.

Update discovery presently queries the canonical SC Offline repository for whole-product releases. The product ZIP has one `sc-offline-<tag>/` root containing launcher/host/data plus `manifest.json` format 1: version, tag, source commit and exact per-file path/SHA-256/size. The updater checks the GitHub asset digest and manifest, stages changes, preserves selected player/config files and rolls back failed updates. This is not an independent plugin update protocol. Do not manufacture that manifest around a plugin-only ZIP and claim the launcher accepts it.

The plugin directory is compatibility information and source discovery, not an installer, update channel or endorsement. Independent authors need manual plugin updates today. A future importer needs an explicit package descriptor/schema, safe staging/preview, conflict/dependency checks, product compatibility, immutable digest/source provenance, rollback and preservation rules; those are proposals, not implemented SDK/MCP behavior.

## Completion boundary

`package_prepared`: staged content, checks and archive round trip succeed on the actual platform. `windows_package_checked`: matching Windows x64 build/check results exist. `launcher_discovered`: observed the intended ID/version in the launcher, with evidence. `host_loaded`: mod.log records successful load on a named host. `in_game`: only an actual owner run with named game/product/plugin builds and behavior observed. Never infer later states from earlier ones.
