# sco-core SDK 2.3.0 (release notes)

Tag `sdk-v2.3.0`, combined zip `sco-sdk-2.3.0.zip`. **Published on 2026-10-15 at 07:11:58 UTC:** [SDK 2.3.0 release and assets](https://github.com/scubamount/sco-core/releases/tag/sdk-v2.3.0). The tag peels to approval commit `1de4a97877f8af3742d0f1a645857db8c98bc76a`, which adds only the signed approval record (`docs/legal/release-decisions/sdk-v2.3.0.json` and `.sig`); the published archives were built from source commit `b039700dcd867da472095510bd68c74845f025f4` by candidate run `38696462512` (attempt 1, artifact `11954255975`). The two identities are distinct. The release publishes all five profiles (combined/legacy, core, core-source, the Star Citizen game-sc 0.6.0 overlay and the Star Citizen host-source kit), their SBOMs, `SHA256SUMS` and the signed `release.json`. The scope is the maintainer's [SDK 2.3.0 scope record](../legal/release-decisions/2026-10-15-sdk-v2.3.0-scope.md). The release workflow publishes the CHANGELOG section [`## sdk-v2.3.0`](../../CHANGELOG.md) as the release's notes; this page is the longer reference behind it.

<!-- release-scope:start -->
```json
{"tag":"sdk-v2.3.0","sdk_version":"2.3.0","game_pack_version":"0.6.0","plugin_abi":"1.1","profiles":["core","game-sc","legacy","core-source","host-source-sc"]}
```
<!-- release-scope:end -->

## Versions

| What | Version | Where |
|---|---|---|
| Kernel (the SDK) | 2.3.0 (was 2.2.0) | `sdk/VERSION`, tag `sdk-v2.3.0` |
| Plugin ABI | 1.1, unchanged | `SCO_API_MAJOR` / `SCO_API_MINOR` in `sco_api.h` (frozen; `include/sco_api.h` and `tests/abi_v1.c` byte-identical to 2.2.0) |
| `sco.ui.surfaces` service table | 1.0 (new) | `SCO_UI_SURFACES_VERSION_1_0` in `sco_ui_surfaces.h` |
| `game.loadout` service table | 1.1 (was 1.0) | `SC_LOADOUT_VERSION_1_1` in `sc_loadout.h`; additive, 1.0 consumers unchanged |
| `game.asop` service table | 1.0 (new) | `sc_asop.h` |
| `game.contracts` service table | 1.0 (new) | `sc_contracts.h` |
| Star Citizen game pack | 0.6.0 (was 0.5.0) | `SCO_GAME_PACK_VERSION` in `sc_game_pack.h` |
| Verified game build | unchanged from 2.2.0 | no new sigcheck or in-game verification in this release |

`VERSIONS.txt` in the zip states the same lines, generated from the headers by `sdk/package.py`.

## Coordinates and distribution

- Core SDK, combined/legacy SDK, the neutral core-source kit and the Star Citizen
  host-source kit (`host-source-sc`): 2.3.0.
- Optional game-sc public overlay: 0.6.0 (was 0.5.0). The release adds `game.asop` 1.0 and
  `game.contracts` 1.0 and grows `game.loadout` to 1.1, and the
  [game-pack policy](../game-pack.md) raises the minor for a release that adds or grows a game
  service. The overlay ships as `sco-sdk-game-sc-0.6.0.zip`, so its compatibility descriptor,
  which binds the exact 2.3.0 core archive SHA-256, never shares a file name with the published
  0.5.0 overlay.
- Frozen plugin ABI: 1.1, unchanged. Individual service versions remain independent.

The declared profiles repeat the five profiles published with 2.2.0; `sdk/components.json` and the
candidate workflow's packaging loop list the same five. Which new headers and sources join which
profile is listed in the [scope record](../legal/release-decisions/2026-10-15-sdk-v2.3.0-scope.md),
as approved for the release. These records supply no licence grant or counsel opinion.

## Changes since 2.2.0

The [CHANGELOG `## sdk-v2.3.0` section](../../CHANGELOG.md) holds the complete entries and their
verification limits. In short:

| Change | Where | Profiles (as packaged today) |
|---|---|---|
| `sco.ui.surfaces` 1.0: renderer-neutral placement of a `sco.widgets` panel as a 2D HUD rectangle, camera-facing billboard or world-anchored quad | `include/sco_ui_surfaces.h`, `include/scosdk/ui_surfaces.hpp`, [UI guide](../ui.md) | headers in core and core-source; host implementation in core-source |
| `sco::game::pak` `Scope::AllFiles`: persistent swaps of the four pinned `ICryPak` slots, refused unless the product opts in; capability `game.pak.all_files` default OFF | `include/sco/game/pak.h`, `src/game/pak_hooks.cpp` | host-source-sc |
| `files/` replacement mounts: `vfs::ReplaceMount` and `vfs::FileMounts` | `include/sco/vfs.h`, `src/vfs/files.cpp` | core-source |
| `game.asop` 1.0 (insurance claim timeout and shard-persisted reads); capabilities default OFF | `include/sc_asop.h`, [ASOP service](../game/asop-service.md) | header in game-sc and host-source-sc; implementation and doc in host-source-sc |
| `game.contracts` 1.0 (mission stream radius read); capability default OFF | `include/sc_contracts.h`, [contracts service](../game/contracts-service.md) | header in game-sc and host-source-sc; implementation and doc in host-source-sc |
| `game.loadout` 1.1: weapon and multitool slot names 8-21 (vocabulary only); `game.loadout.weapons` default OFF | `include/sc_loadout.h`, `include/scosdk/game/loadout.hpp`, [loadout](../game/loadout.md) | headers and doc in game-sc and host-source-sc; implementation in host-source-sc |
| Game status table and engine-native index regenerated for the new capabilities | [evidence status](../game/status.md), `sdk/native_index.json` | repository; the native index stays on the docs site and static MCP only |

Profiles are the `sdk/components.json` classification; `legacy` includes `core` and `game-sc`, and
`host-source-sc` includes `core-source`.

## Not in this release

- C++, C# and Lua routes for `game.asop` and `game.contracts`. C and C++ call the tables through
  their headers.
- A plugin content index listing for `files/` mounts.
- Any service built on the `game.asop` vehicle list, deliver, retrieve and claim rows, or on the
  other contracts rows: the repository documents no plugin-callable meaning for them.

The engine-native index follows the
[2026-10-14 native-index decision](../legal/release-decisions/2026-10-14-native-index.md): the
documentation site and the static MCP server only, never an SDK zip.

## Engineering limits

Nothing in this release has run in the game. Evidence is macOS local builds and synthetic
fixtures: the host test suite, `test_surfaces`, `test_pak_files` over a fake `ICryPak` vtable,
`test_asop`, `test_contracts`, the loadout contract tests and the ABI pins as C11, C++20 and
`-fshort-enums`. Linux and native Windows builds of the merged branch, the Windows game-pack
adapters for the new game tables, GPU rendering of surfaces, and real-game, anti-cheat,
official-network and private-LAN acceptance remain unproven. New game capabilities stay
default-off: no production adapter turns on the five this release adds.

## Checking the zip

The same checks as every release apply:

```sh
sha256sum -c SHA256SUMS
unzip -p sco-sdk-2.3.0.zip sco-sdk-2.3.0/VERSIONS.txt
```

Publication followed the [maintainer release procedure](../legal/release-checklist.md#releasing).
