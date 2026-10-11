# sco-core

[![test](https://github.com/scubamount/sco-core/actions/workflows/test.yml/badge.svg)](https://github.com/scubamount/sco-core/actions/workflows/test.yml)
[![cmake](https://github.com/scubamount/sco-core/actions/workflows/cmake.yml/badge.svg)](https://github.com/scubamount/sco-core/actions/workflows/cmake.yml)
[![sdk](https://github.com/scubamount/sco-core/actions/workflows/sdk.yml/badge.svg)](https://github.com/scubamount/sco-core/actions/workflows/sdk.yml)

sco-core is the engine abstraction layer and mod framework under [sc-offline](https://github.com/scubamount/sc-offline), the Star Citizen offline mod. It keeps Star Citizen's internals (addresses, file formats, engine calls) behind one versioned plain-C interface, so mods are written against sco-core instead of against a game build, and a game patch is fixed once, in sco-core, instead of in every mod.

It gives mod authors:

- **One stable ABI, several languages.** [`sco_api.h`](include/sco_api.h) is the contract. Write a plugin in C, in C++20 with [`scosdk/`](docs/sdk-cpp.md), in C# with [`Sco.Sdk`](docs/sdk-csharp.md) (published with NativeAOT, so players need no .NET), in sandboxed [Lua](sdk/docs/lua.md), or as a [data pack](sdk/docs/data-packs.md) with no code at all.
- **Host services.** Per-plugin [storage](docs/storage.md) (`sco.storage`: key-value and SQL over SQLite), [UI](docs/ui.md) (`sco.ui`: menu tabs, overlays, hotkeys bound to commands), [typed settings](docs/plugins.md#typed-settings-settings) (`sco.settings`: a `[settings]` section in `plugin.ini` becomes values the player edits in the plugin's menu page and the plugin reads) and [game-data patches](docs/datacore.md) (`sco.datacore`: DataCore overrides and new records that survive game patches), and, for bridges and multiplayer, [local IPC](docs/ipc.md) (`sco.ipc`: shared-memory channels to a bridge on the same PC) and [session messages](docs/net.md) (`sco.net`: typed channels between the players of a private session the product opened). Plugins also provide services and raw handlers to each other.
- **Game services.** The Star Citizen game pack publishes services under the owner `game` ([game services](docs/game-services.md)): `teleport.spatial` 1.0 and `spawn.entities` 1.2 (moved from sc-offline), `game.actors` 1.1 (your player, NPCs a plugin spawns and despawns, removed when it unloads, and an actor's health), `game.vehicles` 1.0 (ship seats, seating your own actors and NPCs, Flight Ready), `game.world` 1.0 (ray casts and the camera's position and rotation; no field of view) and `game.entities` 1.1 (any entity's class and transform, spawning and despawning entities anywhere, all removed when the plugin unloads unless it `keep`s them), plus the `game.player.spawned`, `game.player.died` and `game.zone.changed` events on the event bus and the `game.mining` capability (the biome builder's cell function, for sc-offline's mining). C++, C# and read-only Lua wrappers. **Not yet verified in game:** the rows resolve on 4.10.196.36804, but no service has run in a game session since it landed, and `game.entities.watch`, `game.entities.query_radius`, `game.events.vehicle_seat` and `game.actors.state` are off until an in-game check: [status of every game.* table](docs/game/status.md). The game pack also holds internal helpers such as `sco::game::missions::ScriptLibrary`. It has its own version line (`SCO_GAME_PACK_VERSION` in `sc_game_pack.h`, 0.2.0, tags `game-sc-vX.Y`) and a list of the game builds it was verified on ([game pack](docs/game-pack.md)). `game.creative` 1.0 gives your own player god mode, fly and infinite ammo, each behind its own capability (`game.creative.*`).
- **A crash-contained runtime.** Commands, events and game-thread tasks are owned by the plugin that made them; a plugin that faults is unloaded, and everything it registered goes with it. A fault inside a service call is blamed on the provider whose code faulted, not on its caller, and a provider whose service table was handed out is never unmapped ([design](docs/design/service-safety.md)).
- **Hot reload.** `sco::app::Reload(id)` and the host command `sco.reload <plugin>` unload one plugin and load it again while the game runs: a native from a fresh copy of its DLL (so the original can be rebuilt), a Lua plugin by running its script again, everything it registered replaced and what it spawned despawned ([plugins](docs/plugins.md#hot-reload)).

> **SDK 1.2.0** (`sdk/VERSION`, tag `sdk-v1.2.0`, game pack 0.2.0; the tag is the maintainer's to push, and until then the latest published release is `sdk-v1.1.0`). The plugin ABI is version 1.1 and stable, and `sco_api.h` is unchanged since sdk-v1.1.0: version 1 only grows at the end of structs, so a plugin built against this SDK keeps loading in every later 1.x host. The zip's `VERSIONS.txt` states the kernel, ABI and game-pack versions and the verified game build (4.10.196.36804: rows verified by sigcheck; in-game verification pending). The C++ headers in `include/sco/` are the host-side API for products (sc-offline) and are not part of the plugin ABI. What changed: [CHANGELOG](CHANGELOG.md), [release notes](docs/releases/sdk-v1.2.0.md). History and decisions: [Framework plan](docs/framework.md).

## How it fits together

```
 plugins     C / C++20 (scosdk) / C# (Sco.Sdk, NativeAOT)   Lua (sco-lua sandbox)   data packs
                        |                                         |                    |
 ABI         ---------- sco_api.h 1.1: plain C, versioned, size-prefixed ----------------
                        |              service tables: sco_*.h (host) and sc_*.h (game pack)
 game pack   game.* services (owner "game"): actors, vehicles, world, entities, events;
             teleport.spatial, spawn.entities; signature rows (SCO_GAME_SC)
 services    sco.storage   sco.ui   sco.settings   sco.ipc   sco.net   sco.datacore (patches)
             plugin-provided services, raw handlers
 host kit    sco/app.h      Start / Tick / Stop, built-in plugins, host-owned services
 host        sco/host.h     the sco_api table, per-plugin handles     sco/caps.h  capabilities
             sco/plugins.h  discovery, plugin.ini, loader, crash guard, content index
 runtime     sco/runtime.h  game-thread tasks, event bus, command registry, owners
 engine      sco/hook.h       detours (length decoding, far jumps, transactions, slot swaps)
             sco/engine/*     64-bit math, zone tree       sco/vfs.h  virtual game files
             sco/datacore.h   DataCore parser and patcher  sco/game/pak.h  the game's file reads
 game core   sco/signatures.h, sco/scan.h, sco/game/*   signature rows, scanners, report
```

A plugin only ever sees `sco_api.h` and the service headers. Everything it adds (subscriptions, commands, tasks, service tables, tabs, hotkeys) is owned by its handle, so unloading, refusing or disabling a plugin removes all of it in one call.

`sco-host-sim` runs the whole stack, from the host kit down, without the game: CI loads the SDK examples through it on Linux and Windows.

## Quick start

### Write a plugin

Download `sco-sdk-<version>.zip` from the [releases](https://github.com/scubamount/sco-core/releases) (the first is `sdk-v1.1.0`; this tree builds 1.2.0), or build it from this repository (`python3 sdk/package.py`). It holds the headers, a template, a CMake helper, two checkers and these examples:

| Example | Language | Shows |
|---|---|---|
| `hello` | C | A command with arguments and an event subscription |
| `cpp_hello` | C++20 (`scosdk/`) | Commands, a service, a subscription, with RAII handles and the exception boundary |
| `cs_hello` | C# (`Sco.Sdk`, NativeAOT) | The same from .NET, as a native DLL |
| `greeter` | Lua | Commands and events in the sandbox |
| `notebook` | Lua | Storage with `sco.store` |
| `travel_pack` | Data | Lists and rules, no code |
| `quantum_pack` | Data | DataCore overrides in `.toml` |

```sh
cd sco-sdk-<version>
cmake -S . -B build && cmake --build build && cmake --install build --prefix out
out/bin/sco-plugin-check out/data/plugins/hello --invoke hello.wave "Pilot One"
```

`sco-plugin-check` loads a built plugin against a host that enforces the documented rules, without the game. Start with the [SDK README](sdk/README.md), then the [plugin API reference](docs/api-v1.md) and your language's page: [C++](docs/sdk-cpp.md), [C#](docs/sdk-csharp.md), [Lua](sdk/docs/lua.md), [data packs](sdk/docs/data-packs.md).

### Build and test the core

```sh
git clone https://github.com/scubamount/sco-core.git
cd sco-core
tools/test.sh                                   # every unit test, under ASan+UBSan and TSan (clang or gcc)
```

or with CMake, on any platform including Windows with MSVC:

```sh
cmake -S . -B build
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

Run plugins through the real host, without the game:

```sh
build/sco-host-sim sdk/out/data/plugins --ticks 3 --invoke hello.wave "Pilot One"
```

Details, options and what CI runs: [Building and testing](docs/building.md).

### Check a game build

```sh
tools/sigcheck.sh /path/to/StarCitizen.exe      # exit 0 = every row OK
sco-dcb info Game2.dcb                          # the DataCore file's layout check
sco-dcb check Game2.dcb path/to/pack            # would this data pack still apply?
```

On patch day these say what still resolves and which data packs still apply, before anyone starts the game. See [Checking a game build](docs/sigcheck.md) and [DataCore files and packs](docs/datacore.md).

## Repository layout

| Path | What |
|---|---|
| [`include/sco_api.h`](include/sco_api.h) | The plugin ABI: the one header every native plugin includes |
| `include/sco_storage.h`, `sco_ui.h`, `sco_settings.h`, `sco_datacore.h`, `sco_ipc.h`, `sco_net.h` | The host services' tables, each pinned by its own `tests/abi_*.c` |
| `include/sc_*.h` | The game pack's service tables and events (`sc_spatial.h`, `sc_spawn.h`, `sc_actors.h`, `sc_vehicles.h`, `sc_world.h`, `sc_entities.h`, `sc_game_events.h`), its version (`sc_game_pack.h`) and the MIT wires and bridge layouts (`sc_ipc.h`, `sc_net.h`, `sc_titanlink.h`, `sc_voxel_bridge.h`) |
| [`include/scosdk/`](docs/sdk-cpp.md) | The C++20 SDK layer (header-only) |
| [`sdk/csharp/`](docs/sdk-csharp.md) | The C# SDK layer (`Sco.Sdk`, .NET 8, no package references) |
| [`plugins/lua/`](plugins/lua/README.md) | sco-lua: Lua 5.4.8 (vendored, MIT) in a sandbox, built only on `sco_api.h` |
| [`sdk/`](sdk/README.md) | The SDK: template, examples, CMake helper, checkers, packaging (`sdk/VERSION`) |
| [`include/sco/`](docs/api.md) | The host-side C++ API for products: host kit, runtime, hooks, engine math, vfs, DataCore, game tables |
| `src/` | The core, the runtime (`api/`), the host table (`host/`), the host kit (`app/`), the loader (`plugins/`), services (`storage/`, `ui/`, `settings/`, `ipc/`, `net/`, `datacore/`), `hook/`, `engine/`, `vfs/`, game tables and game services (`game/`, `game/services/`) |
| `third_party/` | SQLite (public domain) and toml++ (MIT), vendored and untouched |
| `tests/` | Unit tests, the ABI pins (`abi_*.c`) and fixtures |
| `tools/` | `test.sh`, `sco-sigcheck`, `sco-host-sim`, `sco-dcb` |
| `docs/` | Everything else: [docs index](docs/README.md) |

Only the image reader, the loader's and crash guard's Windows halves and the detour engine are Windows code. Everything else builds and is tested on Linux too, so neither the tests nor the checkers need Windows or the game.

## Documentation

| Doc | For |
|---|---|
| [Docs index](docs/README.md) | Every document, by audience |
| [Plugin API v1](docs/api-v1.md) | `sco_api.h`: every function, struct and rule of the ABI |
| [C++ SDK](docs/sdk-cpp.md), [C# SDK](docs/sdk-csharp.md), [Lua](sdk/docs/lua.md), [Data packs](sdk/docs/data-packs.md) | Writing a plugin in each language |
| [Storage](docs/storage.md), [UI](docs/ui.md), [DataCore](docs/datacore.md), [IPC](docs/ipc.md), [Multiplayer messages](docs/net.md) | The host services |
| [Game services](docs/game-services.md), [Status of every game.* table](docs/game/status.md), [Game pack](docs/game-pack.md) | The Star Citizen game pack: services, what is on and how far it has been checked, versions and verified builds |
| [Plugins](docs/plugins.md) | Discovery, `plugin.ini`, the loader and crash containment |
| [How it works](docs/architecture.md) | Signature rows, startup, threading, the runtime, limits |
| [C++ API reference](docs/api.md) | Every function and type in `include/sco/` |
| [Building and testing](docs/building.md) | `tools/test.sh`, CMake, the SDK zip, CI, releases |
| [Framework plan](docs/framework.md) | Phase status, decisions, lessons learned, what comes next |
| [Contributing](CONTRIBUTING.md) | What fits, tests, how changes reach sc-offline and how lessons come back |

## Use from a product

A product (sc-offline today) builds sco-core into its own DLL (`add_subdirectory`, see [Building § Using sco-core from another CMake project](docs/building.md#using-sco-core-from-another-cmake-project)) and calls the host kit: `sco::app::Start` on the game thread once its own patches are in, `sco::app::Tick` from its main-thread hook, `sco::app::Stop` from the game's own quit path. Its features ship as built-in plugins on the same ABI as third-party ones, so the API is exercised by the code that ships. See [How it works § Startup](docs/architecture.md#startup), [Plugins](docs/plugins.md) and the [Framework plan § Lessons](docs/framework.md#lessons).

## Scope

sco-core is for an offline mod: the game never connects to Cloud Imperium Games' servers. It finds addresses and reports on them, and gives plugins a bounded way to use the game. Two kinds of connection are allowed, both through host services: local IPC with other processes on the same PC (bridges to other games; `sco.ipc` 1.0), and private co-presence between sc-offline players over a LAN or VPN (`sco.net` 1.0, the message channels; opening a session is the product's; the co-presence built on it belongs to the product: [design](docs/design/multiplayer.md)). Online play, cheating, getting around anti-cheat and the other bans in the [plugin rules](sdk/docs/plugin-rules.md) are out of scope for good, as in [sc-offline](https://github.com/scubamount/sc-offline/blob/main/CONTRIBUTING.md#what-fits-this-project) and the [plugin rules](sdk/docs/plugin-rules.md).

## License

GPL-3.0-only, the same licence family as sc-offline. See [LICENSE](LICENSE). The project expects plugins that include its headers or compile its SDK sources to be distributed under GPL-3.0-compatible terms. Whether the GPL applies to a particular plugin depends on how it is built and on applicable law; this is not legal advice. No linking exception is granted ([decision](docs/legal/release-decisions/2026-10-11-plugin-licence.md)). Vendored: Lua (MIT, [`plugins/lua/third_party/lua/LICENSE`](plugins/lua/third_party/lua/LICENSE)), SQLite (public domain, [`third_party/sqlite`](third_party/sqlite/README.md)), toml++ (MIT, [`third_party/tomlplusplus`](third_party/tomlplusplus/README.md)), Dear ImGui's headers in the SDK (MIT); notices in [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md). Where code comes from and how that is recorded: [IP_POLICY.md](IP_POLICY.md).

sco-core is an independent project, not affiliated with, endorsed by or sponsored by Cloud Imperium Games or Roberts Space Industries. Star Citizen® is a trademark of Cloud Imperium Rights LLC, named here only to say which game the optional game pack works with.
