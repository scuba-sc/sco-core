# sco SDK

Build plugins for [sc-offline](https://github.com/scubamount/sc-offline), the Star Citizen offline mod. A plugin is one folder that sc-offline loads from `data\plugins\<id>\`. There are three kinds:

| Kind | What it is | Example |
|---|---|---|
| `native` | A 64-bit Windows DLL written in C against `sco_api.h`, in C++20 with the `include/scosdk/` headers, or in C# with `csharp/Sco.Sdk` (NativeAOT) | [`examples/hello`](examples/hello), [`examples/cpp_hello`](examples/cpp_hello), [`examples/cs_hello`](examples/cs_hello) |
| `data` | Files only: missions, rules, scripts, lists and game-data overrides (`datacore\*.toml`). Runs no code | [`examples/travel_pack`](examples/travel_pack), [`examples/quantum_pack`](examples/quantum_pack) |
| `lua` | A Lua 5.4 script, run in a sandbox by sc-offline's bundled Lua runtime | [`examples/greeter`](examples/greeter), [`examples/notebook`](examples/notebook) (storage) |

> **SDK 2.1.0** (tag `sdk-v2.1.0`; the plugin ABI is **1.1**, unchanged since `sdk-v1.1.0`; Star Citizen game pack 0.4.0; `VERSIONS.txt` in the zip says so, with the verified game build). See the [SDK 2.1.0 release notes](../docs/releases/sdk-v2.1.0.md). The API is stable: version 1 only grows, so a plugin built with this SDK keeps loading in later 1.x hosts. In sc-offline, plugin loading is on by default (`plugins = on` in `sc-offline.ini`); optional folders can still ship disabled. This product default is not a kernel policy. See the [SDK/MCP tutorial](../docs/sdk-mcp-tutorial.md) for isolated owned examples and lifecycle limits.

**Native plugins run with the game's full rights.** Only install plugins you trust; sc-offline doesn't review them. Plugins never connect to Cloud Imperium Games' servers; the only connections allowed are local IPC (`sco.ipc`) and private LAN/VPN co-presence through `sco.net`, both host services: see [Plugin rules](docs/plugin-rules.md).

## What's in the SDK

| Path | What |
|---|---|
| `include/sco_api.h` | The only header a native C plugin includes |
| `include/sco_*.h` | The host services' tables: `sco_storage.h`, `sco_ui.h`, `sco_widgets.h`, `sco_settings.h`, `sco_ipc.h`, `sco_net.h` (core); `sco_datacore.h` (Star Citizen extension) |
| `include/sc_*.h` | Core: the MIT wires and bridge layouts (`sc_ipc.h`, `sc_net.h`, `sc_titanlink.h`, `sc_voxel_bridge.h`). Star Citizen extension: the game pack's service tables and events (`sc_spatial.h`, `sc_spawn.h`, `sc_actors.h`, `sc_vehicles.h`, `sc_world.h`, `sc_entities.h`, `sc_creative.h`, `sc_game_events.h`; [game services](../docs/game-services.md), [status](../docs/game/status.md)) and its version (`sc_game_pack.h`) |
| `include/scosdk/` | The C++20 layer over `sco_api.h`, header-only: [C++ plugins](#c-plugins). `scosdk/game/` and `scosdk/datacore.hpp` come with the Star Citizen extension |
| `csharp/` | The C# layer (`Sco.Sdk`, .NET 8, NativeAOT) and its layout test: [C# plugins](#c-plugins-1). `Sco.Sdk/Game/`, `DataCore*.cs` and the tests' `Game*.cs` come with the Star Citizen extension; the project compiles whichever are present |
| `cmake/sco-plugin.cmake` | `sco_add_plugin()` and `sco_add_pack()`: build a plugin and lay it out |
| `template/` | A native plugin to copy and rename |
| `examples/` | `hello` (native C), `widgets` (native C, retained neutral UI), `cpp_hello` (native C++20), `cs_hello` (native C#, NativeAOT), `greeter` (Lua), `notebook` (Lua: storage with `sco.store`); the Star Citizen extension adds `travel_pack` (data) and `quantum_pack` (data: DataCore overrides) |
| `tools/sco-plugin-check.c` | Checks a built plugin folder on your machine, without the game |
| `tools/lua-check.lua` | Runs a Lua plugin against a stand-in `sco` table, without the game |
| `docs/` | [plugin.ini](docs/plugin-ini.md), [data packs](docs/data-packs.md), [Lua](docs/lua.md), [plugin rules](docs/plugin-rules.md), [API reference](../docs/api-v1.md), [C++ SDK](../docs/sdk-cpp.md), [C# SDK](../docs/sdk-csharp.md) |
| `VERSIONS.txt` | The kernel, plugin ABI and game-pack versions and the verified game builds (in the zip only). The extension zip keeps its own as `VERSIONS.txt.game-sc`, so unpacking it over the core overwrites nothing |
| `SHA256SUMS` | The SHA-256 of every other file (in the zip only; `SHA256SUMS.game-sc` in the extension zip) |
| `COMPATIBILITY.json` | Core SDK version, plugin ABI version, game-overlay version (null when absent), source commits and the extension's exact base core archive SHA-256 |
| `ATTRIBUTIONS.json` | Per-artifact file inventory with source, origin, license, copyright, provenance status and upstream attribution |
| `SBOM.spdx.json` | Embedded SPDX 2.3 JSON: exact file hashes, licenses, package verification code and relationships. Self and `SHA256SUMS` are explicitly excluded to avoid circular hashes; `SHA256SUMS` hashes the SBOM |

### Zips

`sdk/components.json` lists every SDK file with its profile, origin and licence; `sdk/package.py`
builds a zip from it and nothing else (`--check` validates it, `--list-headers core` prints the
public headers of a profile).

| Zip | Holds |
|---|---|
| `sco-sdk-core-source-<version>.zip` | Kernel source-development kit in repository layout: host/runtime, loaders, required vendored Lua/SQLite, CMake build, tools and synthetic tests. Build with `-DSCO_GAME_SC=OFF`; no game implementation, game ABI headers, game layout pins or game-contract fixtures |
| `sco-sdk-host-source-sc-<version>.zip` | Standalone Star Citizen host source in repository layout: all `core-source` components plus complete game implementation, DataCore, headers and synthetic test dependencies. Build with `-DSCO_GAME_SC=ON`; [host-source guide](../docs/host-source-sc.md). Published from `sdk-v2.1.0` |
| `sco-sdk-core-<version>.zip` | The game-neutral SDK: `sco_api.h`, the host services, the C++, C# and Lua layers, the generic examples and tools. Its version is `sdk/VERSION` alone |
| `sco-sdk-game-sc-<game pack version>.zip` | The optional Star Citizen extension: the game pack's tables and events, DataCore, their C++ and C# wrappers, `travel_pack` and `quantum_pack`. Unpack it over the core SDK its `VERSIONS.txt.game-sc` names; it keeps its own `VERSIONS.txt.game-sc` and `SHA256SUMS.game-sc`, so nothing of the core is overwritten |
| `sco-sdk-<version>.zip` | Both in one zip, the shape `sdk-v1.1.0` shipped. Releases use it until consumers move to the two zips |

All generated metadata in the extension has a `.game-sc` suffix; composition preserves both
inventories and SBOMs. The packer computes the required base digest from the canonical core
archive produced from the same source checkout. A core archive with the same version but
different bytes is **not** interchangeable. `sdk/test-zip.sh <core.zip> <game-sc.zip>` checks
archive roles, versions, source commits and the exact digest before extraction or copying.
Raw unzip tools do not enforce compatibility; consumers that compose these archives must
perform the same descriptor checks first.

Each profile ships its own license/attribution inventory, including generated records; existing
license texts and third-party notices are retained. The embedded SBOM describes the extracted
payload, while the optional `ip_audit.py zip --sbom` sidecar also binds the complete archive hash.
`ip_audit.py zip` and packaging tests use an **offline stdlib structural check of the required
SPDX 2.3 fields**, IDs, relationships, inventories and checksums, not a full SPDX-spec validator.
No pinned SPDX validator is available in the offline test environment.

`python3 sdk/test_package.py` builds every profile in separate fresh working/output directories
under different `TZ`, `LC_ALL`, Python hash seeds and umasks, then compares SHA-256 digests and
checks fixed ZIP timestamps/modes and absence of checkout/output paths. This proves byte identity
within one Python/zlib environment, not across compression-library versions. Packaged repository
document links are pinned to the source commit rather than a moving branch.

## Read-only pack content

New hosts expose `sco.content` 1.0 (`sco_content.h`, `scosdk/content.hpp`) for
bounded copied startup lists and XML scripts. See [data packs](docs/data-packs.md)
and `examples/content_reader`. Native C/C++ only; no file paths, execution rights,
C#/Lua route or implicit product base-list import. SDK 2.0.0 predates the service;
it ships from SDK 2.1.0. Frozen plugin ABI 1.1 is unchanged.

## Renderer-neutral widget UI

New C/C++ plugins use `sco.widgets` 1.0 (`sco_widgets.h`, `scosdk/widgets.hpp`):
retained copied trees, typed game-thread events and no plugin renderer linkage.
The [widget guide](docs/widgets.md) and `examples/widgets` show all eight widgets
plus ROW/COLUMN layout with synthetic data. `sco-plugin-check` supplies deterministic
headless interaction coverage. The C# route is deferred in
[#142](https://github.com/scubamount/sco-core/issues/142); the Lua route is deferred
in [#143](https://github.com/scubamount/sco-core/issues/143), both follow-ups to #139.
Do not treat the existing C#/Lua `sco.ui` features as support for neutral widgets.
The optional host-only adapter is source-kit code, never a public plugin header.

## Optional ImGui: a host-specific third-party ABI

This is the legacy `sco.ui` draw-context route, which remains supported; neutral
widget plugins neither include ImGui headers nor link its implementation.

ImGui is **optional**. Non-drawing plugins need neither its headers nor implementation;
the kernel UI registry passes an opaque draw context and does not depend on ImGui.
The frozen `sco_api.h` **1.1 C ABI** and `sco.ui` table do not freeze the pointed-to
`ImGuiContext`. Raw context sharing is **third-party ABI coupling**, not a promise
of compatibility across arbitrary ImGui versions or later 1.x hosts.

The shipped public headers target sc-offline's exact ImGui snapshot at
`1ee0727cc63866a95e709ae61dd5dd355b511253`: **1.92.8 WIP**, `IMGUI_VERSION_NUM` **19277**.
The label/number alone is insufficient; check the SHA-256 pins:

| File in `third_party/imgui/` | SHA-256 |
| --- | --- |
| `imgui.h` | `cd65b1905d5cbdaeab82f6ff180ae8767bd72c7bc99ee258c74665eb2d3ee1fa` |
| `imconfig.h` | `1353bfa59da16eec481723fd4e5d910356f0406b79e3c4142e0c9867bafffab5` |
| `LICENSE.txt` | `173506a2d6f7fb67990d257fb2507f188690eca39060c39469ae7bef43aae2a3` |

Legacy `sco.ui` drawing plugins must use the implementation from that targeted host revision,
unmodified `imconfig.h`, matching ABI-affecting definitions and compatible compiler
settings. Bind the host's context and allocator only during a draw call. Rebuild
when the host changes its ImGui snapshot or configuration; there is no generic
runtime negotiation in the frozen C table. Header equality is not runtime proof.
On a host without a compatible rendering contract, skip draws and continue without
UI. See [the UI contract](../docs/ui.md#optional-third-party-abi-coupling-not-the-frozen-c-abi).

## Build the kernel source kit

The source-development zip is independent of the plugin SDK zips; do not overlay them.
Unpack it into an empty directory, then use CMake 3.20+, a C11/C++20 compiler and Python 3:

```sh
cmake -S . -B build -DSCO_GAME_SC=OFF -DSCO_BUILD_TESTS=ON
cmake --build build --config Debug --parallel
ctest --test-dir build -C Debug --output-on-failure
```

Windows **x64** is the supported product platform. Linux x64 and macOS (x64/ARM64)
are development platforms for the neutral libraries, host simulator and synthetic tests;
their binaries are not game-loadable Windows plugins. ARM64 detour installation returns
`Error::Unsupported` without changing memory. Hook execution tests run on x64 Windows/Linux;
byte decoding and unsupported-host checks run on all development platforms.
The VFS snapshot uses atomic shared-pointer operations even on C++20 standard libraries
without the `atomic<shared_ptr>` specialization.

`src/game`, `src/datacore`, all game ABI headers/wrappers, game layout pins,
`tests/game_contracts`, game examples and the Lua game adapter are absent. Generic
scanner, hook, spatial-math and loader/content-index tests remain. Game-only cases of
shared test drivers live in separately classified include fragments in the repository;
`SCO_GAME_SC=OFF` never includes them. `sdk/components.json` profile membership and
its `game-contract` group define the exclusion inventory.
The released 1.1 header in `tests/abi/released/sco_api.h` is checked against the
existing golden SHA-256, so the frozen-plugin test needs neither Git nor a network.
The source kit is not the complete game-pack source and cannot build `SCO_GAME_SC=ON`;
configuring that mode fails explicitly and requests `-DSCO_GAME_SC=OFF`.
`package.py --profile core-source` produces it; `test-zip.sh <source.zip>` verifies
checksums, configures and builds from the extracted files alone, then runs the focused
synthetic host tests. `python3 sdk/test_package.py` also verifies the manifest-classified
game exclusions, exact public-header membership (including nested includes), and
configures, builds and runs every neutral CTest from a freshly extracted archive.
This full extracted-root regression runs both locally and in the existing SDK CI job.
External C/C++ example sources are copied outside the extraction and compiled against
its installed headers. Compiler dependency output must identify each SDK header as
an exact approved public destination in `components.json`; directory prefixes do not
approve nested files. Macro, transitive and system-marked includes are checked, and
hidden checkout dependencies are refused. `test-zip.sh` runs the same check, or use
`package.py --profile core --check-installed-headers <extracted-root>` directly.
Packaging refuses recognized PEM/OpenSSH private-key and GitHub token shapes before
writing ZIP bytes; this bounded check does not recognize every possible secret.
CI also inspects the full checkout's kernel-only target graph,
compiler depfiles, compiled objects and link inputs; symbol scans are supplementary.

## Build the examples

You need CMake 3.20 or newer (Visual Studio 2019 ships 3.20) and a C and C++20 compiler (Visual Studio 2019 16.11 or newer, clang 14, gcc 11). For plugins the game can load, that's Visual Studio 2019 or newer with the "Desktop development with C++" workload, on Windows. On macOS and Linux the same commands build plugins you can check with `sco-plugin-check`; the game needs the Windows DLL.

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

`out/data/plugins/` now holds `hello/`, `cpp_hello/`, `widgets/`, `greeter/`, `notebook/` and `my_plugin/` (the template), plus `travel_pack/` and `quantum_pack/` when the Star Citizen extension is unpacked over the core SDK, laid out the way sc-offline reads them, and `out/bin/` holds `sco-plugin-check`.

## Check a plugin

```sh
out/bin/sco-plugin-check out/data/plugins/hello
out/bin/sco-plugin-check out/data/plugins/hello --cap teleport --invoke hello.wave "Pilot One"
lua5.4 tools/lua-check.lua examples/greeter --invoke greeter.greet "Pilot One" true
```

`sco-plugin-check` reads `plugin.ini` with the host's rules. For a native plugin it then loads the DLL, calls `sco_plugin_query` and `sco_plugin_load` against a stand-in host that enforces the documented rules, fires `game.ready` and a few `tick`s, runs every command that takes no arguments (and the one you name with `--invoke`), and unloads. For a data pack it lists the files the host would index. `--cap NAME` makes `has(NAME)` answer 1; by default every capability is missing, as on a game build the mod doesn't support yet. Exit code 0 means every check passed.

`release_service` withdraws one of your own services; it does not release a table you got from `query_service` (that table is borrowed, and the host answers `SCO_NOT_FOUND`). The checker fails a `release_service` for any name the plugin doesn't provide at that moment, naming the case where the plugin only queried it.

`--short-tables` hands the plugin a 1.0 host's `sco_api`: `size` 88 (`offsetof(sco_api, provide_service)`) and `minor` 0. Calling a 1.1 member (`provide_service`, `query_service`, `release_service`, `invoke_raw`, `register_raw`) without first checking `api->size >= offsetof(sco_api, member) + sizeof(api->member)` fails the check, with the size the call needed. Run it once on a plugin that should still load on an older host.

The stand-in serves `sco.widgets` 1.1 by default: `update_nodes`, `remove_nodes` and `capabilities` apply the host's rules (IDs, preorder subtrees, a replaced root keeps its parent, a new root joins parent 0 or a ROW/COLUMN, no removal that empties the panel), each node is read as 1.0 or 1.1 by its own `size`, 1.1 tails (severity, tooltip, LIST/TABLE paging) are validated, panels holding a 1.1 node get `sco_widget_event_1_1` events with `row`, and `capabilities` reports the host's features and limits. Under `--short-tables` the widgets table is its 1.0 extent (`size` 24, 1.1 node tails ignored): calling `update_nodes`, `remove_nodes` or `capabilities` without first checking `size >= offsetof(sco_widgets_v1, member) + sizeof(member)` and a non-NULL pointer fails the check. An unchecked `query_service` under `--short-tables` fails too, but still returns that 1.0 table, so later unchecked widgets calls are reported as well.

The stand-in host is not the game: it has no game thread, no crash guard and no game features. A plugin that passes still needs a run in the game.

## Install a plugin

1. Copy the plugin's folder, for example `out\data\plugins\hello`, into sc-offline's `data\plugins\` folder, so you have `data\plugins\hello\plugin.ini`.
2. In `sc-offline.ini` set `plugins = on`.
3. Play. `data\mod.log` lists every plugin with its state: `loaded`, `off`, `disabled`, `crashed`, `faulted` or `refused: <reason>`. `sc-offline.exe status` shows the same list. `faulted` means a provider fault retained backing state and resources rather than releasing them: restart the game; cached service tables are not safe to call, and no recovery is guaranteed.

To switch one plugin off, create an empty file named `disabled` in its folder.

## Generate a plugin folder

`tools/sco-new-plugin.py` (Python 3.8 or newer) writes a ready-to-build native plugin in one command:

```sh
python3 tools/sco-new-plugin.py --id my_radar --lang c --out ../my_radar
python3 tools/sco-new-plugin.py --id my_radar --lang cpp --with-menu --services game.actors,sco.widgets --out ../my_radar
cmake -S ../my_radar -B ../my_radar/build -DSCO_SDK="$PWD"
cmake --build ../my_radar/build --target check
```

- The folder holds `plugin.ini` (`api = 1.0`), `<id>.c` (C11) or `<id>.cpp` (C++20, `include/scosdk/`), and a `CMakeLists.txt` that builds the plugin with `cmake/sco-plugin.cmake`. Its `check` target builds `sco-plugin-check` from `tools/` and runs it on the plugin twice: as is, and with `--short-tables`.
- Every generated plugin registers `<id>.ping`. The code checks `sco_api`'s size before any 1.1 member and each service table's `size` before using it, so the plugin also loads on a 1.0 host.
- `--with-menu` adds a `sco.widgets` panel (`<id>.main`). C++ builds it with `scosdk::PanelBuilder` ([UI cookbook](../docs/ui.md)); C writes the `sco_widget_node` array itself. Without the service the plugin runs with no panel.
- `--services NAME[@MAJOR.MINOR],...` (default version 1.0) looks each service up on `game.ready` and logs the ones that are missing; the plugin keeps working. `plugin.ini` lists them on a commented `requires` line. Uncomment it if the plugin can't run without them. Include a service's header to call its table.
- `--name` and `--author` set the display name and author. The id follows the rules below. The tool refuses an unsafe id, an output that is a file or a symbolic link, and a folder that isn't empty, and it never overwrites a file.
- Other tools can import the file and call `render_plugin(spec)`, which returns `{relative path: text}` and writes nothing.

## Start your own native plugin

1. Copy `template\` to a new folder outside the SDK.
2. Pick an id: 1-31 characters from `a-z`, `0-9` and `_`, not `sco`, `host`, `menu` or `game`. It is the folder name, the `id` in `plugin.ini`, the `name` in `sco_plugin_info` and the prefix of every command (`<id>.<action>`).
3. Replace every `my_plugin` in `plugin.c`, `plugin.ini` and `CMakeLists.txt` with it.
4. Build, pointing at the unpacked SDK:

   ```sh
   cmake -S . -B build -DSCO_SDK=/path/to/unpacked-sco-sdk
   ```

The template registers one command, `my_plugin.ping`. [`examples/hello/hello.c`](examples/hello/hello.c) adds `has()`, events and a command with an argument. The [API reference](../docs/api-v1.md) covers every function.

## C++ plugins

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
- No exception crosses into the host: one that escapes `OnLoad` or a handler is caught, logged, and answered with `SCO_FAILED`.

Build a C++ plugin with the same `sco_add_plugin`, in a project that enables `CXX`; [`examples/cpp_hello`](examples/cpp_hello/cpp_hello.cpp) shows the whole layer in one file. The [C++ SDK reference](../docs/sdk-cpp.md) covers lifetimes, the exception boundary and threads.

## C# plugins

`csharp/Sco.Sdk` is a .NET 8 library over `sco_api.h` and the host services, with no package references. A plugin derives from `Sco.Sdk.Plugin`, declares the three exports as `[UnmanagedCallersOnly(EntryPoint = "sco_plugin_query")]` (and `_load`, `_unload`) methods that forward to `PluginExports`, and is published with NativeAOT into one native DLL: no .NET runtime is needed next to the game.

```powershell
cd examples\cs_hello
dotnet publish -c Release -r win-x64 -o out\cs_hello
copy plugin.ini out\cs_hello\
..\..\out\bin\sco-plugin-check out\cs_hello --invoke cs_hello.wave "Pilot One"
```

- You need the .NET 8 SDK and, on Windows, Visual Studio 2019 or newer with "Desktop development with C++" (NativeAOT links with MSVC). CMake doesn't build C# plugins; `dotnet publish` does.
- Commands (`CommandBuilder`, `Args`, `Reply`), `Subscribe` handles, `RunOnGameThread`, `Invoke` / `InvokeAsync`, services (`Provide`, `Query` with `ServiceRef<T>.Covers`), raw handlers with the size handshake, and `Storage`, `DataCore` and `Ui` for the host services.
- Every callback's `ctx` is an id into a registry swept at unload, never a `GCHandle`; no exception crosses into the host (logged, and `SCO_FAILED` where there is a result).
- `dotnet run -c Release --project csharp/Sco.Sdk.Tests` checks every C# struct against the pinned C layout.

[`examples/cs_hello`](examples/cs_hello/CsHello.cs) shows the whole layer in one file. The [C# SDK reference](../docs/sdk-csharp.md) covers setup, lifetimes, threads, the exception boundary and the NativeAOT rules.

## License

GPL-3.0-only ([LICENSE](../LICENSE)), like sco-core. The project expects plugins that include its headers or compile its SDK sources to be distributed under GPL-3.0-compatible terms. Whether the GPL applies to a particular plugin depends on how it is built and on applicable law; this is not legal advice. No linking exception is granted. Third-party notices: [THIRD_PARTY_NOTICES.md](../THIRD_PARTY_NOTICES.md).
