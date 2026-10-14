# sco-core SDK 2.2.0 (release notes, unpublished candidate)

Tag `sdk-v2.2.0`, combined zip `sco-sdk-2.2.0.zip`. **Status: unpublished candidate.** This page
is a draft prepared on 2026-10-14 for the maintainer. No tag, signature, approval record, candidate
run or release exists for 2.2.0, and nothing on this page authorizes one. The candidate scope still
needs the maintainer's approval: see the
[SDK 2.2.0 scope draft](../legal/release-decisions/2026-10-14-sdk-v2.2.0-scope.md). Until the
maintainer publishes 2.2.0, the newest published SDK is 2.1.0
([latest release](https://github.com/scubamount/sco-core/releases/latest)). The release workflow
publishes the CHANGELOG section [`## sdk-v2.2.0`](../../CHANGELOG.md) as the release's notes;
this page is the longer reference behind it.

<!-- release-scope:start -->
```json
{"tag":"sdk-v2.2.0","sdk_version":"2.2.0","game_pack_version":"0.4.0","plugin_abi":"1.1","profiles":["core","game-sc","legacy","core-source","host-source-sc"]}
```
<!-- release-scope:end -->

## Versions

| What | Version | Where |
|---|---|---|
| Kernel (the SDK) | 2.2.0 (was 2.1.0) | `sdk/VERSION`, tag `sdk-v2.2.0` |
| Plugin ABI | 1.1, unchanged | `SCO_API_MAJOR` / `SCO_API_MINOR` in `sco_api.h` (frozen; `include/sco_api.h` and `tests/abi_v1.c` byte-identical to 2.1.0) |
| `sco.widgets` service table | 1.1 (was 1.0) | `SCO_WIDGETS_VERSION_1_1` in `sco_widgets.h`; additive, 1.0 consumers unchanged |
| Star Citizen game pack | 0.4.0, unchanged | `SCO_GAME_PACK_VERSION` in `sc_game_pack.h` |
| Verified game build | unchanged from 2.1.0 | no new sigcheck or in-game verification in this candidate |

`VERSIONS.txt` in the zip states the same lines, generated from the headers by `sdk/package.py`.

## Coordinates and distribution

- Core SDK, combined/legacy SDK, the neutral core-source kit and the Star Citizen
  host-source kit (`host-source-sc`): 2.2.0.
- Optional game-sc public overlay: 0.4.0. No game service changed, so the
  [game-pack policy](../game-pack.md) gives no reason to raise it. The overlay's
  compatibility descriptor binds the exact core archive SHA-256, so an overlay rebuilt over the
  2.2.0 core has different bytes from the published 0.4.0 overlay under the same version. How to
  name that overlay is an open maintainer decision in the scope draft.
- Frozen plugin ABI: 1.1, unchanged. Individual service versions remain independent.

The declared profiles repeat the five profiles published with 2.1.0. Which new headers and tools
join which profile is listed in the [scope draft](../legal/release-decisions/2026-10-14-sdk-v2.2.0-scope.md),
which records no maintainer approval yet. These records supply no licence grant or counsel opinion.

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

Profiles are the `sdk/components.json` classification; `legacy` includes `core`, and
`host-source-sc` includes `core-source`. All three panel builders target `sco.widgets` 1.0. 1.1 nodes are written against the C header;
no language wrapper exposes 1.1 yet.

## Not in this candidate

These were planned for 2.2.0 and have no code on the candidate branch:

- The engine-native index (`sdk/native_index.json`) and its documentation-site page and static MCP
  tools. The [2026-10-14 native-index decision](../legal/release-decisions/2026-10-14-native-index.md)
  sets where it may appear when it lands: the documentation site and the static MCP server, never
  an SDK zip.
- `game.hangar`, `game.reputation` and `game.world` 1.1 game tables. Without them the game pack
  stays 0.4.0.
- The plugin scaffolder and its MCP tool.

## Engineering limits

Nothing in this candidate has run in the game. Evidence is macOS local builds and synthetic
fixtures: the host test suite, the panel-builder tests and the diagnostics dead-owner fixture. The
C# builder has not been compiled locally (no .NET SDK on the preparing host); the hosted
`sdk/csharp-host-sim` job is its first build. Linux and native Windows builds of the merged
branch, GPU rendering of the 1.1 adapter, and real-game, anti-cheat, official-network and
private-LAN acceptance remain unproven. New game capabilities stay default-off; this candidate adds
none.

## Checking the zip

After publication, the same checks as every release apply:

```sh
sha256sum -c SHA256SUMS
unzip -p sco-sdk-2.2.0.zip sco-sdk-2.2.0/VERSIONS.txt
```

Publication follows the [maintainer release procedure](../legal/release-checklist.md#releasing);
these notes do not authorize a candidate, a tag or a release.
