# sco-core

[![test](https://github.com/scubamount/sco-core/actions/workflows/test.yml/badge.svg)](https://github.com/scubamount/sco-core/actions/workflows/test.yml)
[![cmake](https://github.com/scubamount/sco-core/actions/workflows/cmake.yml/badge.svg)](https://github.com/scubamount/sco-core/actions/workflows/cmake.yml)
[![sdk](https://github.com/scubamount/sco-core/actions/workflows/sdk.yml/badge.svg)](https://github.com/scubamount/sco-core/actions/workflows/sdk.yml)

The mod framework and SDK under [sc-offline](https://github.com/scubamount/sc-offline), the Star Citizen offline mod.

sco-core puts the game's internals (addresses, file formats, engine calls) behind one versioned plain-C plugin interface. Mods target sco-core, not a game build, so when a patch breaks something it gets fixed once here instead of in every mod.

**SDK reference:** <https://scubamount.github.io/sco-core/>

## Who it is for

- **Mod authors:** write a plugin in C, C++20, C# (NativeAOT, so players need no .NET), sandboxed Lua, or as a data pack with no code.
- **Products** such as sc-offline: build sco-core in and ship features as built-in plugins on the same interface.
- **Patch-day maintainers:** check a new game build without starting the game.

## What you get

- **A stable ABI:** [`sco_api.h`](include/sco_api.h), version 1.1. Version 1 only grows, so a plugin built today keeps loading in later 1.x hosts.
- **Host services:** storage (SQLite), UI and hotkeys, typed settings, DataCore patches, local IPC and private-session messages.
- **Game services** from the Star Citizen game pack: actors, vehicles, world, entities and events. Their signatures resolve on 4.10.196.36804, but none has been run in a game session yet: [status of every game.* table](docs/game/status.md).
- **Crash containment and hot reload:** a plugin that faults is contained, and plugins reload while the game runs. Exact rules, including when a restart is required: [Plugins](docs/plugins.md).

## Quick start: write a plugin

Download `sco-sdk-<version>.zip` from [releases](https://github.com/scubamount/sco-core/releases) (latest published: `sdk-v1.1.0`), or build it with `python3 sdk/package.py`. Then:

```sh
cd sco-sdk-<version>
cmake -S . -B build && cmake --build build && cmake --install build --prefix out
out/bin/sco-plugin-check out/data/plugins/hello --invoke hello.wave "Pilot One"
```

`sco-plugin-check` runs your plugin against a host that enforces the plugin rules, without the game. Next: [SDK README](sdk/README.md) and the [SDK reference site](https://scubamount.github.io/sco-core/).

## Quick start: build and test the core

```sh
git clone https://github.com/scubamount/sco-core.git && cd sco-core
cmake -S . -B build && cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

Linux, macOS and Windows (MSVC). Sanitizer runs, options and CI: [Building and testing](docs/building.md).

## Documentation

| Start here | For |
|---|---|
| [Docs index](docs/README.md) | Every document, by audience |
| [Plugin API v1](docs/api-v1.md) | The ABI: every function, struct and rule |
| [C++](docs/sdk-cpp.md) · [C#](docs/sdk-csharp.md) · [Lua](sdk/docs/lua.md) · [Data packs](sdk/docs/data-packs.md) | Writing a plugin in each language |
| [How it works](docs/architecture.md) | Layers, startup, threading, runtime, limits |
| [Checking a game build](docs/sigcheck.md) | Patch-day signature checks |
| [Agent and IDE integration](docs/agent-integration.md) | The read-only `sco-core-mcp` server |
| [CHANGELOG](CHANGELOG.md) · [Contributing](CONTRIBUTING.md) | Changes and how to contribute |

## Scope

Offline only: the game never connects to Cloud Imperium Games' servers. The only allowed connections are local IPC on the same PC and private sessions between sc-offline players over LAN or VPN. Online play, cheating and anti-cheat circumvention are out of scope: [plugin rules](sdk/docs/plugin-rules.md).

## License

GPL-3.0-only ([LICENSE](LICENSE)); no linking exception ([decision](docs/legal/release-decisions/2026-10-11-plugin-licence.md)). Vendored code and its notices: [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md). Provenance policy: [IP_POLICY.md](IP_POLICY.md).

sco-core is an independent project, not affiliated with, endorsed by or sponsored by Cloud Imperium Games or Roberts Space Industries. Star Citizen® is a trademark of Cloud Imperium Rights LLC, named here only to say which game the optional game pack works with.
