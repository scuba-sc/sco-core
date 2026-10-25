# sco-core

Consumer agents: start with the [SDK workflow skills](docs/agent-workflows.md) for building, debugging, packaging, upgrading and reporting public SDK gaps.

[![test](https://github.com/scuba-sc/sco-core/actions/workflows/test.yml/badge.svg)](https://github.com/scuba-sc/sco-core/actions/workflows/test.yml)
[![cmake](https://github.com/scuba-sc/sco-core/actions/workflows/cmake.yml/badge.svg)](https://github.com/scuba-sc/sco-core/actions/workflows/cmake.yml)
[![sdk](https://github.com/scuba-sc/sco-core/actions/workflows/sdk.yml/badge.svg)](https://github.com/scuba-sc/sco-core/actions/workflows/sdk.yml)

The mod framework and SDK under [sc-offline](https://github.com/scuba-sc/sc-offline), the Star Citizen offline mod.

sco-core puts the game's internals (addresses, file formats, engine calls) behind one versioned plain-C plugin interface. Mods target sco-core, not a game build, so when a patch breaks something it gets fixed once here instead of in every mod.

**SDK reference:** <https://scuba-sc.github.io/sco-core/>

## Who it is for

- **Mod authors:** write a plugin in C, C++20, C# (NativeAOT, so players need no .NET), sandboxed Lua, or as a data pack with no code.
- **Products** such as sc-offline: build sco-core in and ship features as built-in plugins on the same interface.
- **Patch-day maintainers:** check a new game build without starting the game.

## What you get

- **A stable ABI:** [`sco_api.h`](include/sco_api.h), version 1.1. Version 1 only grows, so a plugin built today keeps loading in later 1.x hosts.
- **Game-neutral host services:** storage (SQLite), retained widgets, UI and hotkeys, typed settings, local IPC and private-session messages.
- **Optional Star Citizen extension:** DataCore patches and game services for actors, actor stats, vehicles, world, entities, events and more. Contract presence, native availability, static signature checks and in-game verification are separate: [status of every game.* table](docs/game/status.md). No current game-service implementation is claimed verified in game.
- **Native fault handling and hot reload:** guarded host-entered calls on Windows support best-effort fault attribution and withdrawal, not safe recovery. Native plugins are trusted in-process code, not sandboxed; raw service calls and custom workers are not automatically guarded, and corruption can still end the process. Eligible plugins can reload while the game runs; restart after a native fault. [Guard limits and restart rules](docs/plugins.md#crash-containment).

## Quick start: write a plugin

The current SDK is **2.4.0**, Star Citizen game pack **0.8.0**, plugin ABI **1.1** (unchanged
since 2.3.0), published as [release candidate 1](https://github.com/scuba-sc/sco-core/releases/tag/sdk-v2.4.0-rc.1)
(tag `sdk-v2.4.0-rc.1`, the repository's latest release; the final signed `sdk-v2.4.0` follows). Download
`sco-sdk-2.4.0.zip` (or `sco-sdk-core-2.4.0.zip`) and `SHA256SUMS` from that release page and
check the digest as in [Start your first plugin](docs/getting-started.md), or build an archive
with `python3 sdk/package.py`. See the [2.4.0 release notes](docs/releases/sdk-v2.4.0.md); nothing
new in 2.4.0 has been run in game. Then:

```sh
cd sco-sdk-<version>
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
cmake --install build --config Release --prefix out
out/bin/sco-plugin-check out/data/plugins/hello --invoke hello.wave "Pilot One"
```

`sco-plugin-check` runs your plugin against a host that enforces the plugin rules, without the game. Next: [SDK README](sdk/README.md) and the [SDK reference site](https://scuba-sc.github.io/sco-core/).

## Quick start: build and test the core

```sh
git clone https://github.com/scuba-sc/sco-core.git && cd sco-core
cmake -S . -B build && cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

Linux, macOS and Windows (MSVC). Sanitizer runs, options and CI: [Building and testing](docs/building.md).

## Documentation

| Start here | For |
|---|---|
| [Start your first plugin](docs/getting-started.md) | New author: download, verify, rename, build, check and deploy |
| [Use the API safely](docs/README.md#plugin-api-consumer--use-the-api-safely) | Plugin ABI, contract authority and language/service guides |
| [Embed a host](docs/README.md#sdk-integrator--host--embed-a-host) | SDK integrator: host kit, lifecycle, simulator and controls |
| [Check or update a game build](docs/README.md#game-pack-contributor--check-or-update-a-game-build) | Game-pack contributor: signatures, guards and evidence |
| [Discover via MCP](docs/README.md#agent--ide-user--discover-the-sdk-via-mcp) | Agent/IDE user: read-only SDK discovery |
| [Build, contribute, release](docs/README.md#maintainer--build-contribute-release) | Maintainer: tests, release/decision evidence and publication |
| [Roadmap and direction](docs/roadmap.md) | Where the SDK is going and how to contribute |
| [Curated docs index](docs/README.md) | All six audience paths plus historical evidence |

## Scope

Offline only: the game never connects to Cloud Imperium Games' servers. The only allowed connections are local IPC on the same PC and private sessions between sc-offline players over LAN or VPN. Online play, cheating and anti-cheat circumvention are out of scope: [plugin rules](sdk/docs/plugin-rules.md).

## License

GPL-3.0-only ([LICENSE](LICENSE)); no linking exception ([decision](docs/legal/release-decisions/2026-10-11-plugin-licence.md)). Vendored code and its notices: [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md). Provenance policy: [IP_POLICY.md](IP_POLICY.md).

sco-core is an independent project, not affiliated with, endorsed by or sponsored by Cloud Imperium Games or Roberts Space Industries. Star Citizen® is a trademark of Cloud Imperium Rights LLC, named here only to say which game the optional game pack works with.
