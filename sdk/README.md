# sco SDK

Consumer agents: use the [workflow bundle](../docs/agent-workflows.md), starting with [roadmap and SDK planning](skills/sco-plan-sdk-work/SKILL.md). Read the bundle directly or follow your client's installation instructions; shipping the files does not activate them. The guide explains which profiles support MCP discovery and the content-hashed index.

Build plugins for [sc-offline](https://github.com/scuba-sc/sc-offline), the Star Citizen offline mod. A plugin is one folder that sc-offline loads from `data\plugins\<id>\`. There are three kinds:

| Kind | What it is | Example |
|---|---|---|
| `native` | A 64-bit Windows DLL written in C against `sco_api.h`, in C++20 with the `include/scosdk/` headers, or in C# with `csharp/Sco.Sdk` (NativeAOT). The core and combined kits also include experimental raw Rust bindings. | [`examples/hello`](examples/hello), [`examples/cpp_hello`](examples/cpp_hello), [`examples/cs_hello`](examples/cs_hello), [`examples/rust_hello`](examples/rust_hello); two plugins sharing a service: [`examples/counter_service`](examples/counter_service) and [`examples/counter_panel`](examples/counter_panel) |
| `data` | Files only: missions, rules, scripts, lists and game-data overrides (`datacore\*.toml`). Runs no code | [`examples/travel_pack`](examples/travel_pack), [`examples/quantum_pack`](examples/quantum_pack) |
| `lua` | A Lua 5.4 script, run in a sandbox by sc-offline's bundled Lua runtime | [`examples/greeter`](examples/greeter), [`examples/notebook`](examples/notebook) (storage) |

<!-- release-block:current-release -->
SDK **2.4.1** uses Star Citizen game pack **0.8.1** (checked against Star Citizen
Alpha 4.10.3, build 4.10.200.58746; see [game builds](../docs/game/builds.md)) and plugin ABI **1.1**. Download it from the
[`sdk-v2.4.1` release page](https://github.com/scuba-sc/sco-core/releases/tag/sdk-v2.4.1) and verify each archive against that release's
`SHA256SUMS`; the [release notes](https://github.com/scuba-sc/sco-core/blob/sdk-v2.4.1/docs/releases/sdk-v2.4.1.md) list what changed. The previous release is
[SDK 2.4.0](https://github.com/scuba-sc/sco-core/blob/sdk-v2.4.0/docs/releases/sdk-v2.4.0.md).
<!-- /release-block:current-release -->

To identify the kit you have, read `COMPATIBILITY.json` (versions, profile and source revisions; `COMPATIBILITY.json.game-sc` for the game overlay) and `VERSION` (`sdk/VERSION` in source-layout kits).

The release has five profile archives, `SHA256SUMS` and `release.json`. The ABI only grows, but plugins still check service versions, sized extents, capabilities and manifest requirements. In sc-offline, plugin loading is on by default (`plugins = on` in `sc-offline.ini`) and optional folders can ship disabled; that default is a product choice, not kernel policy. See the [SDK/MCP tutorial](../docs/sdk-mcp-tutorial.md).

In a source checkout `sdk/VERSION` is <!--rv:sdk_version-->2.4.1<!--/rv-->, `sdk/DEVELOPMENT_VERSION` is the next development coordinate and `include/sc_game_pack.h` is game pack <!--rv:game_pack_version-->0.8.1<!--/rv-->. [`sco.storage.metadata` 2.0](../docs/storage-metadata.md) and the `sco.widgets` 1.4 [tagged events](../docs/design/widgets.md#tagged-events-and-old-provider-refusal-14) are new in 2.4.1 and absent from SDK 2.4.0. For an extracted kit, use its own `COMPATIBILITY.json`, `VERSION` or `VERSIONS.txt`, and checksums.

**Native plugins run with the game's full rights.** Install only plugins you trust; sc-offline does not review them. Plugins never connect to Cloud Imperium Games' servers. The only connections allowed are local IPC (`sco.ipc`) and private LAN/VPN co-presence through `sco.net`, both host services: see [Plugin rules](docs/plugin-rules.md).

## What's in the SDK

| Path | What |
|---|---|
| `include/sco_api.h` | The frozen root plugin ABI; include the public service headers for the services you use |
| `include/sco_*.h` | The host services' tables (core): `sco_storage.h`, `sco_storage_v2.h` (`sco.storage.metadata` 2.0, new in SDK 2.4.1: [contract and limits](../docs/storage-metadata.md)), `sco_ui.h`, `sco_ui_surfaces.h`, `sco_widgets.h`, `sco_settings.h`, `sco_ipc.h`, `sco_net.h`, `sco_content.h`, `sco_diagnostics_publisher.h`. The game-capable extension adds `sco_datacore.h` |
| `include/sc_*.h` | Core: the MIT wires and bridge layouts (`sc_ipc.h`, `sc_net.h`, `sc_titanlink.h`, `sc_voxel_bridge.h`). Star Citizen extension: the game pack's service tables and events (`sc_spatial.h`, `sc_spawn.h`, `sc_actors.h`, `sc_vehicles.h`, `sc_world.h`, `sc_entities.h`, `sc_creative.h`, `sc_game_events.h`; [game services](../docs/game-services.md), [status](../docs/game/status.md)) and its version (`sc_game_pack.h`) |
| `include/scosdk/` | The header-only C++20 layer over `sco_api.h`: [the C++ layer](#the-c20-layer-scosdk). `scosdk/game/` and `scosdk/datacore.hpp` come with the Star Citizen extension |
| `rust/` | Experimental raw Rust bindings, in the core and combined kits from SDK 2.4.1: [Rust plugin support](rust/README.md). The source kits, the game overlay and the SDK 2.4.0 archives do not contain them |
| `csharp/` | The C# layer (`Sco.Sdk`, .NET 8, NativeAOT) and its layout test: [the C# layer](#the-c-layer-scosdk). `Sco.Sdk/Game/`, `DataCore*.cs` and the tests' `Game*.cs` come with the Star Citizen extension; the project compiles whichever are present |
| `cmake/sco-plugin.cmake` | `sco_add_plugin()` and `sco_add_pack()`: build a plugin and lay it out |
| `template/` | A native plugin to copy and rename: [Start your first plugin](../docs/getting-started.md#4-build-the-checker-copy-and-rename-the-template) |
| `examples/` | Native C: `hello`, `counter_service` and `counter_panel` (a provider and a consumer of one service table), `widgets` (retained neutral UI), `content_reader` (`sco.content`), `diagnostics_publisher` (optional diagnostic publisher). Native C++20: `cpp_hello`. Native C#: `cs_hello`, `cs_widgets` (NativeAOT). Lua: `greeter`, `notebook` (storage with `sco.store`), `widgets_lua`. Experimental raw Rust: `rust_hello`, and `launcher`, a reference launcher ([launcher contract](../docs/launcher-contract.md), [building a launcher](../docs/building-a-launcher.md)), both in the core and combined kits only. The Star Citizen extension adds `travel_pack` (data), `quantum_pack` (data: DataCore overrides), `container_reader` (native C, `game.inventory.contents`) and `actor_stats_probe` (native C probe with an in-game checklist) |
| `tools/sco-plugin-check.c` | Checks a built plugin folder on your machine, without the game |
| `tools/sco-plugin-verify.py` | Read-only check of a plugin ZIP's structure and manifest, without extracting it: [Verify a plugin archive](#verify-a-plugin-archive) |
| `tools/lua-check.lua` | Runs a Lua plugin against a stand-in `sco` table, without the game |
| `docs/` | [plugin.ini](docs/plugin-ini.md), [data packs](docs/data-packs.md), [Lua](docs/lua.md), [plugin rules](docs/plugin-rules.md), [API reference](../docs/api-v1.md), [C++ SDK](../docs/sdk-cpp.md), [C# SDK](../docs/sdk-csharp.md), [launcher contract](../docs/launcher-contract.md), [building a launcher](../docs/building-a-launcher.md) |
| `VERSIONS.txt` | The kernel, plugin ABI and game-pack versions and the checked game builds (in the zip only). The extension zip keeps its own as `VERSIONS.txt.game-sc`, so unpacking it over the core overwrites nothing |
| `SHA256SUMS` | The SHA-256 of every other file (in the zip only; `SHA256SUMS.game-sc` in the extension zip) |
| `COMPATIBILITY.json` | Core SDK version, plugin ABI version, game-overlay version (null when absent), source commits and the extension's exact base core archive SHA-256 |
| `ATTRIBUTIONS.json` | Per-artifact file inventory with source, origin, license, copyright, provenance status and upstream attribution |
| `SBOM.spdx.json` | Embedded SPDX 2.3 JSON: exact file hashes, licenses, package verification code and relationships. Self and `SHA256SUMS` are excluded to avoid circular hashes; `SHA256SUMS` hashes the SBOM |

### Zips

`sdk/components.json` lists every SDK file with its profile, origin and licence. `sdk/package.py` builds a zip from it and nothing else; `--check` validates it and `--list-headers core` prints a profile's public headers.

| Zip | Holds |
|---|---|
| `sco-sdk-core-source-<version>.zip` | Kernel source-development kit in repository layout: host/runtime, loaders, required vendored Lua/SQLite, CMake build, tools and synthetic tests. Build with `-DSCO_GAME_SC=OFF`; it has no game implementation, game ABI headers, game layout pins or game-contract fixtures, so no game-pack version. [Build it](#build-the-kernel-source-kit) |
| `sco-sdk-host-source-sc-<version>.zip` | Standalone Star Citizen host source in repository layout: all `core-source` components plus the complete game implementation, DataCore, headers and synthetic test dependencies. Build with `-DSCO_GAME_SC=ON`; [host-source guide](../docs/host-source-sc.md). Included in the SDK 2.4.0 and 2.4.1 releases ([2.4.0 archive identities](../docs/releases/sdk-v2.4.0.md#stable-archive-identity)) |
| `sco-sdk-core-<version>.zip` | The game-neutral SDK: `sco_api.h`, the host services, the C++, C# and Lua layers, the generic examples and tools |
| `sco-sdk-game-sc-<game pack version>.zip` | The optional Star Citizen extension: the game pack's tables and events, DataCore, their C++ and C# wrappers, `travel_pack` and `quantum_pack`. Unpack it over the core SDK its `VERSIONS.txt.game-sc` names; it keeps its own `VERSIONS.txt.game-sc` and `SHA256SUMS.game-sc`, so nothing of the core is overwritten |
| `sco-sdk-<version>.zip` | Core and game extension in one zip, the shape `sdk-v1.1.0` shipped. Releases use it until consumers move to the two zips |

A locally built package can share a published archive's version and filename without having the same bytes. Release identity is the archive digest in the published release's `SHA256SUMS`; version or filename alone is not enough.

The `core-source` and `host-source-sc` kits are separate from the plugin SDK zips. Do not overlay them. The combined zip, the `game-sc` overlay and the `host-source-sc` archive also ship `sdk-catalogue.json`, `sdk-guides.json` and the discovery launcher `tools/sco-core-mcp/server.py`; the `core` and `core-source` archives do not. To use the SDK with an AI agent, set the combined kit up as described in [SDK discovery in agents and IDEs](../docs/agent-integration.md#set-up-the-discovery-server-from-the-sdk-kit).

The game-capable archives also include an optional local consumer requirements report: `tools/sdk_consumer_requirements.py`, `docs/sdk-consumer-requirements.md` and a `container_reader` requirements template. Copy the template and bind its placeholders to the selected archive catalogue's exact bytes and declared source SHA before you run it with the matching packaged registry. The report describes source evidence, not runtime availability. In the host-source archive the example is under `sdk/examples/` and the registry under `sdk/`; in plugin kits they are under `examples/` and the kit root.

All generated metadata in the extension has a `.game-sc` suffix, and composition preserves both inventories and SBOMs. The packer computes the extension's required base digest from the canonical core archive built from the same source checkout. A core archive with the same version but different bytes is **not** interchangeable. `sdk/test-zip.sh <core.zip> <game-sc.zip>` checks archive roles, versions, source commits and the exact digest before you extract or copy. Raw unzip tools do not enforce compatibility, so a consumer that composes these archives must do the same descriptor checks first.

Each profile ships its own license and attribution inventory, and keeps existing license texts and third-party notices. The embedded SBOM describes the extracted payload. Packaging, reproducibility and SBOM checks are in [Building and testing](../docs/building.md).

## Build the kernel source kit

Unpack the source-development zip into an empty directory, then use CMake 3.20+, a C11/C++20 compiler and Python 3:

```sh
cmake -S . -B build -DSCO_GAME_SC=OFF -DSCO_BUILD_TESTS=ON
cmake --build build --config Debug --parallel
ctest --test-dir build -C Debug --output-on-failure
```

Windows **x64** is the supported product platform. Linux x64 and macOS (x64/ARM64) are development platforms for the neutral libraries, host simulator and synthetic tests; their binaries are not game-loadable Windows plugins.

The kit has no `src/game`, `src/datacore`, game ABI headers or wrappers, game layout pins, `tests/game_contracts`, game examples or Lua game adapter. The generic scanner, hook, spatial-math, loader and content-index tests remain. The kit is not the complete game-pack source: configuring `SCO_GAME_SC=ON` fails with a message asking for `-DSCO_GAME_SC=OFF`. To build the game pack, use the `host-source-sc` kit or a checkout. `package.py --profile core-source` produces the kit; `test-zip.sh <source.zip>` checks it.

## Build the examples

You need CMake 3.20 or newer (Visual Studio 2019 ships 3.20) and a C and C++20 compiler (Visual Studio 2019 16.11 or newer, clang 14, gcc 11). For plugins the game can load, that means Visual Studio 2019 or newer with the "Desktop development with C++" workload, on Windows. On macOS and Linux the same commands build plugins you can check with `sco-plugin-check`; the game needs the Windows DLL.

Windows (Developer PowerShell for VS):

```powershell
cmake -S . -B build -A x64
cmake --build build --config Release
cmake --install build --config Release --prefix out
```

macOS or Linux:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
cmake --install build --prefix out
```

`out/data/plugins/` now holds `hello/`, `cpp_hello/`, `widgets/`, `widgets_lua/`, `content_reader/`, `counter_service/`, `counter_panel/`, `greeter/`, `notebook/` and `my_plugin/` (the template), laid out the way sc-offline reads them. `travel_pack/` and `quantum_pack/` are there too when the Star Citizen extension is unpacked over the core SDK. `out/bin/` holds `sco-plugin-check`. To start your own plugin from `template/`, follow [Start your first plugin](../docs/getting-started.md#4-build-the-checker-copy-and-rename-the-template).

## Check a plugin

```sh
out/bin/sco-plugin-check out/data/plugins/hello --no-load
out/bin/sco-plugin-check out/data/plugins/hello
out/bin/sco-plugin-check out/data/plugins/hello --cap teleport --invoke hello.wave "Pilot One"
lua5.4 tools/lua-check.lua examples/greeter --invoke greeter.greet "Pilot One" true
```

`--no-load` checks the folder without loading or running plugin code. It applies the host's `plugin.ini` rules, compares the manifest id with the folder name, and requires a regular, non-symlink folder, manifest and entry file. For a data pack it inventories recognized files in host-indexed locations, skips symlinks and non-regular files, and stops at 4,096 indexed files, 16,384 filesystem entries examined or 16 script-directory levels. Manifest and entry paths are limited by a 1,023-byte buffer, data-scan paths by 4,096 bytes, and Windows paths also obey the OS limit. Displayed paths escape control, quote, backslash and non-ASCII bytes. The scan reads names and metadata only, not file content. `--no-load` cannot be combined with `--cap`, `--short-tables` or `--invoke`. A pass means the folder is well formed. It does not establish code safety, binary ABI compatibility, provider availability or runtime compatibility.

Without `--no-load`, a native plugin is loaded: the checker calls `sco_plugin_query` and `sco_plugin_load` against a stand-in host that enforces the documented rules, fires `game.ready` and a few ticks, runs every command that takes no arguments (and the one you name with `--invoke`), then unloads. This runs the plugin's native code with your user rights. Every argument after `--invoke NAME` goes to that command, even one that looks like an option such as `--no-load`. For a data pack the checker lists the files the host would index. `--cap NAME` makes `has(NAME)` answer 1; by default every capability is missing, as on a game build the mod does not support yet. Exit code 0 means every check passed.

An automatic no-argument call may return `SCO_NOT_FOUND` when there is no current resource, such as an equip job. The checker prints that without counting it as a failure and does not report the command as successful. An explicit `--invoke` still fails on `SCO_NOT_FOUND`, including an unknown command name. `SCO_FAILED`, `SCO_BAD_ARG`, unknown results and API misuse are failures.

`release_service` withdraws one of your own services. It does not release a table you got from `query_service`: that table is borrowed, and the host answers `SCO_NOT_FOUND`. The checker fails a `release_service` for any name the plugin does not provide at that moment, and says so when the plugin only queried it.

`--short-tables` hands the plugin a synthetic 1.0-sized `sco_api` prefix: `size` 88 (`offsetof(sco_api, provide_service)`) and `minor` 0. Calling a 1.1 member (`provide_service`, `query_service`, `release_service`, `invoke_raw`, `register_raw`) without first checking `api->size >= offsetof(sco_api, member) + sizeof(api->member)` fails the check and reports the size the call needed. Use it to exercise size guards. Host admission separately requires a supported exported API version.

The stand-in serves `sco.widgets` 1.4 by default. It validates complete preorder trees, subtree updates and removals, 1.1 tooltip/severity/paging tails, 1.2 stable tags, 1.3 numeric presentation and 1.4 tagged events, including tag uniqueness in the resulting panel. Each node is read only through its advertised size, and capability readback reports features and limits. Under `--short-tables` the widgets table has its 1.0 extent (`size` 24), with extension tails ignored. Calling a later member without complete-member extent and non-null checks fails the check, as does an unchecked `query_service` under the short root table; the stand-in still returns the table so later unchecked calls are diagnosed too.

The stand-in host has no game thread, no crash guard and no game features. A plugin that passes has not run in game.

## Verify a plugin archive

Run the read-only archive verifier before you share a plugin ZIP. The ZIP must contain exactly one top-level folder named for the plugin id, with `plugin.ini` directly inside it. For native and Lua plugins, the manifest's `entry` must exactly match a regular file directly inside that folder. Archive paths must be safe and unique across case-insensitive and Unicode-normalized filesystems. The verifier refuses Windows-invalid characters, alternate-data-stream syntax, reserved device names, trailing dots or spaces, symlinks, special files, encrypted entries, ZIP64 archives and compression methods other than stored and deflate.

```sh
python3 tools/sco-plugin-verify.py ../pilot.zip --checker out/bin/sco-plugin-check
python3 tools/sco-plugin-verify.py ../pilot.zip --checker out/bin/sco-plugin-check --json
```

It prints the archive SHA-256, a SHA-256 for each file, the parsed plugin id and kind, the API value declared in `plugin.ini` and the dependency names declared by `requires`. It streams archive contents for hashing and sends only bounded `plugin.ini` text to `sco-plugin-check`'s manifest-only parser, which returns before any folder inspection or loading. It never writes archive entries to disk or opens an entry as code, and it checks declared resource bounds before hashing or opening members. The bounds are 256 MiB compressed archive, 4,096 entries, 128 MiB per file, 512 MiB total uncompressed data, a 200:1 maximum compression ratio, an 8 MiB central directory, a 16 KiB manifest and 4,194,304 aggregate Unicode characters of original, NFC-casefold and NFKC-casefold path-prefix material for collision checks.

A pass means the archive structure, manifest syntax, folder identity and declared-entry presence are sound. The verifier does not compare the reported API and dependencies with a host or resolve them, and it does not load the plugin. It is separate from `sco-plugin-check`, which loads native plugins in its stand-in host.

## Install a plugin

1. Copy the plugin's folder, for example `out\data\plugins\hello`, into sc-offline's `data\plugins\` folder, so you have `data\plugins\hello\plugin.ini`.
2. In `sc-offline.ini` set `plugins = on`.
3. Run `sc-offline.exe status` to check installation and configuration; it does not report plugin load states. If sc-offline reaches host startup reporting, it may record discovered plugin states in `data\mod.log`. The file can be missing or left over from an earlier run.

If a host report marks a plugin `faulted`, the host keeps its backing state and resources. Its cached service tables are not safe to call. Restart the host before you retry.

To switch one plugin off, create an empty file named `disabled` in its folder.

## Generate a plugin folder

`tools/sco-new-plugin.py` (Python 3.8 or newer) writes a ready-to-build plugin folder in one command, in C, C++, C# or Lua:

```sh
python3 tools/sco-new-plugin.py --id my_radar --lang c --out ../my_radar
python3 tools/sco-new-plugin.py --id my_radar --lang cpp --with-menu --services game.actors,sco.widgets --out ../my_radar
cmake -S ../my_radar -B ../my_radar/build -DSCO_SDK="$PWD"
cmake --build ../my_radar/build --target check
```

- C and C++: the folder holds `plugin.ini` (`api = 1.1`), `<id>.c` (C11) or `<id>.cpp` (C++20, `include/scosdk/`), and a `CMakeLists.txt` that builds the plugin with `cmake/sco-plugin.cmake`. Its `check` target builds `sco-plugin-check` from `tools/` and runs it on the plugin twice: as is, and with `--short-tables`.
- Every generated plugin registers `<id>.ping`. Native, C++ and C# manifests declare API 1.1, matching the version the plugin exports as built-against. Size guards and short-table checks exercise defensive prefix access; they are not API 1.0 loader admission. Lua declares its script-runtime minimum.
- `--with-menu` adds a `sco.widgets` panel (`<id>.main`). C++ builds it with `scosdk::PanelBuilder` ([UI cookbook](../docs/ui.md)); C writes the `sco_widget_node` array itself. Without the service the plugin runs with no panel.
- `--services NAME[@MAJOR.MINOR],...` (default version 1.0) looks each service up on `game.ready` and logs the ones that are missing; the plugin keeps working. `plugin.ini` lists them on a commented `requires` line. Uncomment it if the plugin cannot run without them. Include a service's header to call its table.
- `--lang csharp` writes `plugin.ini`, `<id>.csproj` (a NativeAOT class library that references `csharp/Sco.Sdk`, like [`cs_hello`](examples/cs_hello)), `<Name>Plugin.cs` (`Sco.Sdk` with the three exports; `--with-menu` uses `PanelBuilder`, `--services` uses `Query`) and a `README.md`. Publish and check it with the [.NET 8 SDK and NativeAOT toolchain](../docs/sdk-csharp.md#setup). `ScoSdk` (or the `SCO_SDK` environment variable) is this folder, and the publish copies `plugin.ini` next to the DLL. `out/bin/sco-plugin-check` comes from [Build the examples](#build-the-examples):

  ```sh
  python3 tools/sco-new-plugin.py --id my_radar --lang csharp --with-menu --out ../my_radar
  dotnet publish ../my_radar -c Release -r win-x64 -o ../my_radar/out/my_radar -p:ScoSdk="$PWD"
  out/bin/sco-plugin-check ../my_radar/out/my_radar
  out/bin/sco-plugin-check ../my_radar/out/my_radar --short-tables
  ```

- `--lang lua` writes `plugin.ini` (`kind = lua`), `main.lua` (`<id>.ping`; `--with-menu` adds a [`sco.widgets`](docs/lua.md#renderer-neutral-widgets-scowidgets-10) panel when the host has the service) and a `CMakeLists.txt` that installs the folder with `sco_add_pack` and whose `check` target runs `sco-plugin-check` on it. `lua5.4 tools/lua-check.lua ../my_radar` runs the script against a stand-in. The Lua sandbox has no service lookup, so `--services` is refused for Lua (except `sco.widgets` with `--with-menu`).
- The id, `--services` and text rules are the same in every language; a combination a language cannot support is refused with the reason.
- `--name` and `--author` set the display name and author. The tool refuses an unsafe id, an output that is a file or a symbolic link, and a folder that is not empty, and it never overwrites a file. The id rules are in [Start your first plugin](../docs/getting-started.md#name-your-plugin).
- Other tools can import the file and call `render_plugin(spec)`, which returns `{relative path: text}` and writes nothing.

## Language and service notes

### The C++20 layer (scosdk)

`include/scosdk/` is a header-only C++20 layer over `sco_api.h`, with nothing beyond the standard library. Derive from `sco::sdk::Plugin`, override `OnLoad`, and `SCO_PLUGIN` writes the three exports:

```cpp
#include "scosdk/scosdk.hpp"

class MyPlugin : public sco::sdk::Plugin {
public:
    sco_result OnLoad() override {
        return sco::sdk::CommandBuilder(*this, "my_plugin.ping")
            .Title("Ping")
            .Handle([](const sco::sdk::Args&, sco::sdk::Reply& reply) { reply.Set("pong"); return SCO_OK; })
            .Register();
    }
};

SCO_PLUGIN(MyPlugin, "my_plugin", "1.0.0", "you");
```

- Commands with typed arguments (`CommandBuilder`, `Args`, `Reply`), event subscriptions as handles that unsubscribe themselves (`Subscribe`), tasks as `std::function` (`RunOnGameThread`), services (`Provide`, `ServiceRef<T>`) and raw handlers with the size handshake done for you (`RegisterRaw<In, Out>`, `InvokeRaw`).
- The SDK owns your handlers and strings for the plugin's life and releases them at unload; nothing it hands the host can be called after it was freed.
- No exception crosses into the host: one that escapes `OnLoad` or a handler is caught, logged and answered with `SCO_FAILED`.

Build a C++ plugin with the same `sco_add_plugin`, in a project that enables `CXX`. [`examples/cpp_hello`](examples/cpp_hello/cpp_hello.cpp) shows the whole layer in one file. The [C++ SDK reference](../docs/sdk-cpp.md) covers lifetimes, the exception boundary and threads.

### The C# layer (Sco.Sdk)

`csharp/Sco.Sdk` is a .NET 8 library over `sco_api.h` and the host services, with no package references. A plugin derives from `Sco.Sdk.Plugin`, declares the three exports as `[UnmanagedCallersOnly(EntryPoint = "sco_plugin_query")]` (and `_load`, `_unload`) methods that forward to `PluginExports`, and is published with NativeAOT into one native DLL. No .NET runtime is needed next to the game.

```powershell
cd examples\cs_hello
dotnet publish -c Release -r win-x64 -o out\cs_hello
copy plugin.ini out\cs_hello\
..\..\out\bin\sco-plugin-check out\cs_hello --invoke cs_hello.wave "Pilot One"
```

- You need the .NET 8 SDK and, on Windows, Visual Studio 2019 or newer with "Desktop development with C++" (NativeAOT links with MSVC). CMake does not build C# plugins; `dotnet publish` does.
- Commands (`CommandBuilder`, `Args`, `Reply`), `Subscribe` handles, `RunOnGameThread`, `Invoke` / `InvokeAsync`, services (`Provide`, `Query` with `ServiceRef<T>.Covers`), raw handlers with the size handshake, and `Storage`, `DataCore` and `Ui` for the host services.
- Every callback's `ctx` is an id into a registry swept at unload, never a `GCHandle`. No exception crosses into the host; it is logged, and `SCO_FAILED` is returned where there is a result.
- `dotnet run -c Release --project csharp/Sco.Sdk.Tests` checks every C# struct against the pinned C layout.

[`examples/cs_hello`](examples/cs_hello/CsHello.cs) shows the whole layer in one file. The [C# SDK reference](../docs/sdk-csharp.md) covers setup, lifetimes, threads, the exception boundary and the NativeAOT rules.

### Read-only pack content

Hosts expose `sco.content` 1.0 (`sco_content.h`, `scosdk/content.hpp`) for bounded copied startup lists and XML scripts, from SDK 2.1.0. See [data packs](docs/data-packs.md) and `examples/content_reader`. It is native C and C++ only: no file paths, no execution rights, no C# or Lua route and no implicit product base-list import.

### Renderer-neutral widget UI

New plugins use `sco.widgets` (`sco_widgets.h`, `scosdk/widgets.hpp`): a 1.0 base with additive versions through 1.4, retained copied trees, typed game-thread events and no plugin renderer linkage. The [widget guide](docs/widgets.md) and `examples/widgets` show all eight widgets and ROW/COLUMN layout with synthetic data. `sco-plugin-check` supplies deterministic headless interaction coverage. The C# and Lua layers add typed subtree update and removal, capabilities, tooltip/severity/paging fields, row events, stable tags and numeric presentation. The wrappers query service versions in descending order (1.4, 1.3, 1.2, 1.1, then 1.0) and check complete member extents and required feature bits. Tagged callback routes need service 1.4 and both the stable-tag and event-tag features; older providers refuse these requests before changing anything. The tagged-event route is new in SDK 2.4.1 and absent from SDK 2.4.0. The optional host-only ImGui adapter is source-kit code, not a public plugin header.

### Optional ImGui for legacy `sco.ui`

This is the legacy `sco.ui` draw-context route, which stays supported. Widget plugins neither include ImGui headers nor link its implementation, and non-drawing plugins need neither. The kernel UI registry passes an opaque draw context and does not depend on ImGui.

The frozen `sco_api.h` 1.1 C ABI and the `sco.ui` table do not freeze the pointed-to `ImGuiContext`. Sharing a raw context couples you to a third-party ABI, with no promise of compatibility across ImGui versions or later 1.x hosts.

The shipped public headers target sc-offline's ImGui snapshot at `1ee0727cc63866a95e709ae61dd5dd355b511253`: **1.92.8 WIP**, `IMGUI_VERSION_NUM` **19277**. The label alone is not enough; the headers' SHA-256 pins are in [provenance.json](../docs/legal/provenance.json). A legacy `sco.ui` drawing plugin must use the implementation from that host revision, an unmodified `imconfig.h`, matching ABI-affecting definitions and compatible compiler settings. Bind the host's context and allocator only during a draw call. Rebuild when the host changes its ImGui snapshot or configuration; the frozen C table has no runtime negotiation. Header equality is not runtime proof. On a host without a compatible rendering contract, skip draws and continue without UI. See [the UI contract](../docs/ui.md#optional-third-party-abi-coupling-not-the-frozen-c-abi).

## License

GPL-3.0-only ([LICENSE](../LICENSE)), like sco-core. The project expects plugins that include its headers or compile its SDK sources to be distributed under GPL-3.0-compatible terms. Whether the GPL applies to a particular plugin depends on how it is built and on applicable law; this is not legal advice. No linking exception is granted. Third-party notices: [THIRD_PARTY_NOTICES.md](../THIRD_PARTY_NOTICES.md).

## Private development packages

From a clean Git checkout, `python3 sdk/package.py --development --profile host-source-sc --out <directory>` (or another declared profile) builds a private package. It uses the committed `sdk/DEVELOPMENT_VERSION` in archive names and generated SDK metadata, writes it to the kit `VERSION`, records the exact source commit, and implies no tag or release. The game-sc overlay keeps its game-pack version and adds the development SDK identifier to its archive name. The published archive's identity is its recorded checksum.
