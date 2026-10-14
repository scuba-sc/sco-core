# sco-core SDK 2.2.0 (release notes)

Tag `sdk-v2.2.0`, combined zip `sco-sdk-2.2.0.zip`. **Published on 2026-10-14 at 20:01:25 UTC:** [SDK 2.2.0 release and assets](https://github.com/scubamount/sco-core/releases/tag/sdk-v2.2.0). The tag peels to approval commit `686187ce936400450d448f1fe754b1e58263e4af`, which adds only the signed approval record (`docs/legal/release-decisions/sdk-v2.2.0.json` and `.sig`); the published archives were built from source commit `81437e1a12993a4484308bd76443a003ae57b7fa` by candidate run `38627571934`. The two identities are distinct. The release publishes all five profiles (combined/legacy, core, core-source, the Star Citizen game-sc 0.5.0 overlay and the Star Citizen host-source kit), their SBOMs and `SHA256SUMS`. The scope is the maintainer's [SDK 2.2.0 scope decision](../legal/release-decisions/2026-10-14-sdk-v2.2.0-scope.md). The release workflow publishes the CHANGELOG section [`## sdk-v2.2.0`](../../CHANGELOG.md) as the release's notes; this page is the longer reference behind it.

<!-- release-scope:start -->
```json
{"tag":"sdk-v2.2.0","sdk_version":"2.2.0","game_pack_version":"0.5.0","plugin_abi":"1.1","profiles":["core","game-sc","legacy","core-source","host-source-sc"]}
```
<!-- release-scope:end -->

## Versions

| What | Version | Where |
|---|---|---|
| Kernel (the SDK) | 2.2.0 (was 2.1.0) | `sdk/VERSION`, tag `sdk-v2.2.0` |
| Plugin ABI | 1.1, unchanged | `SCO_API_MAJOR` / `SCO_API_MINOR` in `sco_api.h` (frozen; `include/sco_api.h` and `tests/abi_v1.c` byte-identical to 2.1.0) |
| `sco.widgets` service table | 1.1 (was 1.0) | `SCO_WIDGETS_VERSION_1_1` in `sco_widgets.h`; additive, 1.0 consumers unchanged |
| Star Citizen game pack | 0.5.0 (was 0.4.0) | `SCO_GAME_PACK_VERSION` in `sc_game_pack.h` |
| Verified game build | unchanged from 2.1.0 | no new sigcheck or in-game verification in this release |

`VERSIONS.txt` in the zip states the same lines, generated from the headers by `sdk/package.py`.

## Coordinates and distribution

- Core SDK, combined/legacy SDK, the neutral core-source kit and the Star Citizen
  host-source kit (`host-source-sc`): 2.2.0.
- Optional game-sc public overlay: 0.5.0 (was 0.4.0). The maintainer kept `game.hangar` 1.0,
  `game.reputation` 1.0 and `game.world` 1.1 in this release, and the
  [game-pack policy](../game-pack.md) raises the minor for a release that adds or grows a game
  service. The overlay ships as `sco-sdk-game-sc-0.5.0.zip`, so its compatibility descriptor,
  which binds the exact 2.2.0 core archive SHA-256, never shares a file name with the published
  0.4.0 overlay.
- Frozen plugin ABI: 1.1, unchanged. Individual service versions remain independent.

The declared profiles repeat the five profiles published with 2.1.0. Which new headers and tools
join which profile is listed in the [scope decision](../legal/release-decisions/2026-10-14-sdk-v2.2.0-scope.md),
where the maintainer approved them as proposed. These records supply no licence grant or counsel opinion.

## Changes since 2.1.0

The [CHANGELOG `## sdk-v2.2.0` section](../../CHANGELOG.md) holds the complete entries and their
verification limits. In short:

| Change | Where | Profiles (as packaged today) |
|---|---|---|
| `sco.widgets` 1.1: atomic subtree update and removal, node and event extensions (severity, tooltip, paged LIST/TABLE windows, PAGE events with a row), runtime capabilities | `include/sco_widgets.h`, [UI guide](../ui.md) | header in core and core-source; host implementation in core-source |
| C++ `scosdk::PanelBuilder`: declarative nested panels over `sco.widgets` 1.0 | `include/scosdk/menu_builder.hpp`, [UI guide](../ui.md) | core, core-source |
| C# `Sco.Sdk.PanelBuilder` over `sco.widgets` 1.0 | `sdk/csharp/Sco.Sdk/PanelBuilder.cs`, [C# guide](../sdk-csharp.md) | core |
| Lua `sco.panel_builder` module in every plugin sandbox | `plugins/lua/sco_panel_builder.h`, [Lua guide](../../sdk/docs/lua.md) | core-source |
| POSIX diagnostics transport recovers from an owner that died without cleanup | `src/ipc/diagnostics_platform_posix.cpp` | core-source |
| Game status table generated from `sdk/status_registry.json` and checked in CI | [evidence status](../game/status.md) | repository only; the packaged `status.md` carries the rendered table |
| Documentation gate refuses source citations by line number; scheduled read-only check that the docs site is fresh | `tools/doc_release_gate.py`, `tools/check_live_site.py` | repository only |
| `game.hangar` 1.0 (lift open/close request, ATC request cancel), `game.reputation` 1.0 (read-only check-site state), `game.world` 1.1 (read-only `free_camera_flag` tail member); every new capability default OFF | `include/sc_hangar.h`, `include/sc_reputation.h`, `include/sc_world.h`, [hangar](../game/hangar.md), [reputation](../game/reputation.md), [world](../game/world.md) | headers in game-sc and host-source-sc; implementation in host-source-sc |
| `sco-new-plugin.py`: one-command native plugin scaffolder (C or C++20, optional panel and service lookups) | `sdk/tools/sco-new-plugin.py`, [SDK README](../../sdk/README.md) | core, core-source (as `tools/sco-new-plugin.py`) |
| Engine-native index of signature rows by symbolic hash and promotion tier; docs-site `/natives/` page with a download of the same JSON | `sdk/native_index.json`, `tools/native_index.py`, `site/` | repository, docs site and static MCP only; in no SDK zip |
| Discovery MCP server, four opt-in tools behind `--extended-tools` (default six unchanged): `list_engine_natives`, `get_engine_native`, `validate_manifest_and_widgets`, `generate_plugin_scaffold` (returns files as text, writes nothing) | `tools/sco-core-mcp/`, [agent integration](../agent-integration.md) | repository only |
| Docs site grouped into four library hubs; no route moved | `site/scripts/gen.py` | docs site only |

Profiles are the `sdk/components.json` classification; `legacy` includes `core`, and
`host-source-sc` includes `core-source`. The C++, C# and Lua panel builders target `sco.widgets` 1.0; the C++
`scosdk::PanelBuilder` emits 1.1 nodes when `.Tooltip()`, `.Severity()` or `.Paging()` is used on a 1.1 host.

## Language routes for the new game tables

- C++ (`include/scosdk/game/`) and C# (`Sco.Sdk.Game`) wrap `game.hangar`, `game.reputation` and the
  `game.world` 1.1 member. Lua wraps the `game.world` 1.1 free-camera flag; Lua routes for
  `game.hangar` and `game.reputation` are unsupported.
- C++, C# and Lua wrappers for the `sco.widgets` 1.1 members, and 1.1 emulation in
  `sco-plugin-check`, which stays a 1.0 stand-in.
- `sdk/test_new_plugin.py` in CI. It runs locally only.

The engine-native index follows the
[2026-10-14 native-index decision](../legal/release-decisions/2026-10-14-native-index.md): the
documentation site and the static MCP server only, never an SDK zip.

## Engineering limits

Nothing in this release has run in the game. Evidence is macOS local builds and synthetic
fixtures: the host test suite, the panel-builder tests and the diagnostics dead-owner fixture. The
C# SDK and its builder were first compiled by the hosted `sdk/csharp-host-sim` job; Linux and
native Windows (MSVC) builds of the release source passed hosted CI. GPU rendering of the 1.1
adapter, and real-game, anti-cheat, official-network and private-LAN acceptance remain unproven.
The Windows game-pack adapters for the new game tables have not run in game. The MCP `generate_plugin_scaffold` tool and the native-index page
were exercised by unit tests and a local site build only. New game capabilities stay default-off: no production adapter turns on
the four this release adds.

## Checking the zip

The same checks as every release apply:

```sh
sha256sum -c SHA256SUMS
unzip -p sco-sdk-2.2.0.zip sco-sdk-2.2.0/VERSIONS.txt
```

Publication followed the [maintainer release procedure](../legal/release-checklist.md#releasing).
