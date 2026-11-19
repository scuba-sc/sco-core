# Build a plugin with an AI agent and the SDK MCP server

You ask an AI agent to scaffold a plugin, validate its manifest and widgets, and check it, with the SDK's local MCP server supplying the contract facts. The tutorial works from the downloaded SDK kit and needs no other repository. It ends with a plugin folder that passes the SDK's checker.

<!-- release-block:current-release-brief -->
SDK **2.4.1** uses Star Citizen game pack **0.8.1** (checked against Star Citizen
Alpha 4.10.3, build 4.10.200.58746; see [game builds](../docs/game/builds.md)) and plugin ABI **1.1**. Archives and their digests are on the
[`sdk-v2.4.1` release page](https://github.com/scuba-sc/sco-core/releases/tag/sdk-v2.4.1); the [release notes](https://github.com/scuba-sc/sco-core/blob/sdk-v2.4.1/docs/releases/sdk-v2.4.1.md) list what changed.
<!-- /release-block:current-release-brief -->

## Before you start

You need:

- the combined developer kit from the release page, checked against the release's `SHA256SUMS` and extracted into an empty folder. The combined kit, a `game-sc` overlay composed over its matching `core` kit, and the `host-source-sc` kit carry the discovery server, `sdk-catalogue.json`, `sdk-guides.json` and the workflow index. The core-only profiles do not.
- Python 3.9 or newer and an MCP client that supports stdio. The SDK <!--rv:sdk_version-->2.4.1<!--/rv--> server speaks protocol `2025-11-25` and `2026-07-28`.
- to build the scaffold: CMake 3.20 or newer and a 64-bit C or C++ compiler. A `csharp` scaffold needs the .NET 8 SDK and the NativeAOT toolchain. A `lua` scaffold builds only `sco-plugin-check`.

Do not pair a kit with the site's catalogue or with another kit's workflow index. To work from a source checkout instead of a kit, build the catalogue and index as the [server README](../tools/sco-core-mcp/README.md#run-from-a-source-checkout-maintainers) and the [skills page](agent-workflows.md#build-the-index-in-a-checkout) describe. Clang is needed to build a catalogue, not to use one.

## Connect the client

Follow [Set up the discovery server from the SDK kit](agent-integration.md#set-up-the-discovery-server-from-the-sdk-kit) with the combined kit. The `SDK` folder is the one that directly contains `sdk-catalogue.json` and `sdk-workflows.json`. Register `sco-core-sdk` in your client and verify the connection.

The default profile offers 15 read-only tools. `--minimal-tools` keeps only the six catalogue tools and leaves out scaffolding, validation, native and guide discovery, and workflows. List the exact tool names in your client's allowlist.

To start the server by hand, use absolute paths on the machine that runs the client:

```sh
python3 /absolute/path/to/sco-sdk-<version>/tools/sco-core-mcp/server.py \
  --catalogue /absolute/path/to/sco-sdk-<version>/sdk-catalogue.json \
  --repo-root /absolute/path/to/sco-sdk-<version>
```

The command waits for MCP messages on stdin. It is not a shell prompt. On Windows use `py -3` and drive-qualified paths such as `C:/absolute/path/to/sco-sdk-<version>/tools/sco-core-mcp/server.py`. If your client does not inherit your shell `PATH`, give it an absolute Python path.

`--repo-root` lets the server read example and metadata files under that root. It also lets the scaffold tool, and the guide tools when no `sdk-guides.json` is present, run that root's generator code inside the server process. Use a kit you verified.

## Start with the roadmap workflow

Call `list_workflows {}`, then `get_workflow {"id":"sco-plan-sdk-work"}`. Read the references it returns as you need them. Map your task to a roadmap goal and public contracts, and write down the result you expect before you continue.

If the kit has no workflow index, these two tools are not listed. Get the index that matches your kit. If the index is present but invalid, the server prints a warning on stderr and disables only these two tools. Both tools return embedded text and run no code.

## Discover one contract

After initialization, inspect `tools/list` and call `list_services` with `{}`.

`get_service_status` lists a service's static capability rows, including rows without methods. Use a row only when `source_binding.state` is `matched`. It does not query a provider or a running game.

Then send these separate `tools/call` parameter objects:

```json
{"name":"search_api","arguments":{"query":"set_panel","service":"sco.widgets","language":"c","limit":5}}
```

```json
{"name":"get_symbol","arguments":{"symbol_or_id":"sco_widgets_v1.set_panel"}}
```

```json
{"name":"get_capability_status","arguments":{"symbol_or_id":"sco_widgets_v1.set_panel"}}
```

```json
{"name":"get_example","arguments":{"symbol_or_id":"sco_widgets_v1.set_panel","language":"c"}}
```

Read the contract's complete member extent, version, table size, ownership, thread and callback rules, results, behavior when the capability is absent, and cleanup. The language token is `c`, not a display name. Example contents are data. Discovery never runs them.

Every result carries catalogue provenance. Compare its SDK, game-pack and source coordinates with the kit's `COMPATIBILITY.json` and keep the archive digest. `check_compatibility` compares coordinates and service-header extents. It does not authenticate an archive, observe a provider or show that a plugin loads.

## Request and review a scaffold

```json
{"name":"generate_plugin_scaffold","arguments":{"id":"my_probe","lang":"c","with_menu":true,"services":["sco.storage@1.0"]}}
```

Require `state: "available"` and read the returned `files` map. It holds `plugin.ini`, `CMakeLists.txt` and `my_probe.c`. The server has not written, built or checked anything. Review the code and notices, then save those exact relative files into a new empty project folder. Do not overwrite an existing project, and do not trust the source because it came through MCP.

Generated native, C++ and C# manifests use `api = 1.1`, which matches the built-against version they export. Lua uses `api = 1.0`. The generated code still guards access to later root-table members. `services` requests optional lookups on `game.ready`. It does not supply an implementation or permission to call one.

`lang` also takes `csharp` and `lua`:

- A `csharp` result holds `plugin.ini`, `my_probe.csproj` (NativeAOT, referencing the kit's `csharp/Sco.Sdk`), `MyProbePlugin.cs` and a `README.md` with the `dotnet publish` line.
- A `lua` result holds `plugin.ini`, `main.lua` and a `CMakeLists.txt` whose `check` target runs `sco-plugin-check` on the folder. The checker covers the manifest and entry only. `tools/lua-check.lua` with Lua 5.4 runs the script against a stand-in.
- Lua scripts cannot look up services. A `lua` request with `services` other than `sco.widgets` (with `with_menu`) is refused with the generator's reason.

## Validate manifest text and widget invariants

Validate the returned manifest text with `folder: "my_probe"` before you build. This is a complete minimal manifest and widget example with the same ID:

```json
{
  "name": "validate_manifest_and_widgets",
  "arguments": {
    "plugin_ini": "id = my_probe\nname = My probe\nversion = 0.1.0\napi = 1.1\nkind = native\nentry = my_probe.dll\n",
    "folder": "my_probe",
    "widgets": [
      {
        "kind": "slider",
        "id": 1,
        "label": "Level",
        "value": 0.5,
        "minimum": 0,
        "maximum": 1
      }
    ]
  }
}
```

Require `valid: true` and read the manifest and widget findings. A valid manifest does not show that its entry file exists, that its requirements have providers, or that its module is safe. Widget validation does not inspect native pointers, panel stride or size, or renderer behavior. To see a negative result, use the same node ID twice and confirm the validation findings.

Native-index discovery is optional:

```json
{"name":"list_engine_natives","arguments":{"limit":1}}
```

Pass one returned entry's `id` as `get_engine_native.id_or_hash` and keep its evidence tier. Follow `next_cursor` and do not invent cursor values. Entries are symbolic signature-row names, not addresses or resolved functions.

## Ask the guides how to do something

Contract records say what a method guarantees. The guides say how to use it. Search them with a plain question:

```json
{"name":"search_guides","arguments":{"query":"How do I deploy a plugin on Windows?","limit":3}}
```

Each hit names its guide `doc`, `heading`, `anchor` and `url` (a docs-site link, or a repository path for a guide under `sdk/docs`), with a short snippet. Read one hit in full by passing its `doc` and `anchor` back:

```json
{"name":"get_guide_section","arguments":{"doc":"docs/getting-started.md","anchor":"start-your-first-plugin"}}
```

Add `"doc"` to a search to stay within one guide, and follow `next_cursor` for more hits. The kit's `sdk-guides.json` answers these calls. Its `guides.source_sha` should equal the catalogue's. A section's text stops at 4000 characters (`truncated: true`); read the rest at its `url`. Guide prose is not a contract record: confirm a method with `get_symbol` before you rely on it.

## Build and check outside MCP

Save the returned files under `/absolute/project/my_probe`, then run:

```sh
cmake -S /absolute/project/my_probe -B /absolute/project/my_probe/build \
  -DSCO_SDK=/absolute/path/to/sco-sdk-<version>
cmake --build /absolute/project/my_probe/build --config Debug --target check
```

On Windows use a compiler developer shell, drive-qualified paths and `-A x64` for a fresh Visual Studio configuration. Check the exit status after every command. For helpers in a source checkout, set `SCO_SDK` to the checkout's `sdk` folder. The source-layout kits keep their helpers under `sdk` as well.

The generated `check` target builds `sco-plugin-check` and runs it with the full and the short root table. The checker loads and runs the plugin's native code. It is not a sandbox. The SDK CMake helper names development modules `.dll` on macOS and Linux too, but only a module built on Windows is suitable for the Windows game host.

A passing check is synthetic host and ABI evidence. Not run in game. See [game capability status](game/status.md). Discovery never compiles, writes files, runs commands or reloads plugins. For runtime metadata from a running host, see the separate [runtime launcher](../tools/sco-core-mcp-runtime/README.md).

## Optional: let the agent build and check

The same loop can run through MCP on your own machine. Three development tools are offered when the operator configures a project root. First build the checker once, outside your project root. The `check` target builds its own copy inside the project's build tree, but `build_plugin_project` builds only the default targets:

```sh
cmake -S /absolute/path/to/sco-sdk-<version> -B /absolute/checker-build
cmake --build /absolute/checker-build --config Release --target sco-plugin-check
```

The program is `/absolute/checker-build/sco-plugin-check`, or `/absolute/checker-build/Release/sco-plugin-check.exe` with a Visual Studio generator. Start the server with a `--project-root` that holds your project folder and that checker. `--enable-dev-tools` still works but is not needed. On Windows with Visual Studio 2022, add a generator and platform:

```sh
python3 /absolute/path/to/sco-sdk-<version>/tools/sco-core-mcp/server.py \
  --catalogue /absolute/path/to/sco-sdk-<version>/sdk-catalogue.json \
  --repo-root /absolute/path/to/sco-sdk-<version> \
  --project-root /absolute/project \
  --plugin-check /absolute/checker-build/sco-plugin-check \
  --cmake-generator "Visual Studio 17 2022" --cmake-platform x64
```

In a client configuration each flag and each value is its own `args` element, and `Visual Studio 17 2022` is one element without the shell quotes. On Windows use `py` with `-3` first and an `.exe` checker, for example `C:/absolute/checker-build/Release/sco-plugin-check.exe`. The [integration guide](agent-integration.md#opt-in-development-tools) shows the elements.

The SDK folder comes from `--repo-root` (or `--sco-sdk`). Without `--plugin-check`, the server looks for `sco-plugin-check` in that folder's `bin/` or `out/bin/`. A full SDK build followed by `cmake --install <build> --config Release --prefix out` in the SDK folder puts it there, as the [SDK README](../sdk/README.md) describes. Name an `.exe` or a `.py` wrapper. The server refuses batch files. Then send:

```json
{"name":"build_plugin_project","arguments":{"project":"my_probe"}}
```

```json
{"name":"run_plugin_check","arguments":{"folder":"my_probe/build/check/my_probe"}}
```

```json
{"name":"run_plugin_check","arguments":{"folder":"my_probe/build/check/my_probe","mode":"short"}}
```

Relative paths start at the first project root. Require `passed: true` on each result. The build result has a `configure` and a `build` step, each with its exit code and the tail of its output. A check result lists the checker's `FAIL` lines as `findings` and its last line as `summary`. These two checks repeat what the generated `check` target does. If your project registers CTest tests, `test_plugin_project` with the same `project` runs them. With no tests it fails on purpose.

The caller supplies only the folder and the mode. The operator's command line fixes everything else, and paths outside the project roots are refused. The build and the checker run your project's code on this machine, and a timeout kills the whole process tree. The [server README](../tools/sco-core-mcp/README.md#development-tools-opt-in) describes the security model.

## Installation and lifecycle limits

Installing a plugin is an explicit action you take with the product's documented plugin folder and manifest flow. No MCP tool does it. Do not replace a mapped Windows DLL. Unload through a supported host route, or close the game first.

Reload is not universal. It can cascade to loaded dependents, and it refuses built-in or data members and service providers that have been handed out. It is unavailable for a provider that faulted and needs a restart. Manifest changes to `requires`, `entry` and settings declarations need a restart, because reload does not read the manifest again. DataCore changes can also need one. See [plugin lifecycle and hot reload](plugins.md#hot-reload). Native plugins are not sandboxed, and a fault has no promised safe in-process recovery.

If you hold an older kit, see [Older kits](agent-integration.md#older-kits).
