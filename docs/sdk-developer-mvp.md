# SDK developer-platform MVP

Use the combined developer kit to discover supported APIs, generate a plugin,
build it and check both full and short host-table behavior. The game-sc overlay
also carries discovery tooling when composed over its exact required core kit.
The host-source kit keeps repository layout for building a compatible host.

The kit contains `sdk-catalogue.json`, `sdk-guides.json`, `tools/sco-core-mcp/server.py`,
native/status metadata, `tools/sco-new-plugin.py`, checker source and CMake integration.
[Set up the discovery server from the SDK kit](agent-integration.md#set-up-the-discovery-server-from-the-sdk-kit) covers
download and checksum, each client (Claude Code, Codex, Cursor, VS Code, Copilot CLI),
Windows and macOS/Linux launchers and how to verify the connection. For a client that
reads an `mcpServers` object, the entry is the following. The SDK folder is the folder that
directly contains `sdk-catalogue.json`, `sco-sdk-2.4.0` for the combined kit; use absolute
paths, and on Windows `py` with `-3` as the first argument. Discovery now defaults to
fourteen tools including workflow retrieval; use `--minimal-tools` for six catalogue tools.
Published RC1 bytes retain their older defaults until a new kit is approved and published.

```json
{
  "mcpServers": {
    "sco-core-sdk": {
      "command": "python3",
      "args": [
        "/absolute/path/to/sco-sdk-2.4.0/tools/sco-core-mcp/server.py",
        "--catalogue",
        "/absolute/path/to/sco-sdk-2.4.0/sdk-catalogue.json",
        "--repo-root",
        "/absolute/path/to/sco-sdk-2.4.0"
      ]
    }
  }
}
```

Start with `list_workflows` and `get_workflow` for `sco-plan-sdk-work`: retain the
roadmap source/goal, public SDK contract boundary and measurable acceptance.
Discover a service with `search_api`; read `get_symbol` and
`get_capability_status` before using it. `get_example` reads record-listed code.
`list_engine_natives` exposes symbolic names and evidence tiers, not addresses.
`validate_manifest_and_widgets` checks configuration/tree invariants.
`generate_plugin_scaffold` returns project files without writing or building them.
`search_guides` and `get_guide_section` answer "how do I ..." questions from the
guide sections in `sdk-guides.json`; guide prose is not a contract record.
The agent writes the returned files into its own new project and runs:

```sh
cmake -S project -B project/build -DSCO_SDK=/absolute/path/to/sco-sdk-2.4.0
cmake --build project/build --config Debug --target check
```

For `lang: "csharp"` there is no `CMakeLists.txt`: publish with
`dotnet publish project -c Release -r win-x64 -o project/out/<id> -p:ScoSdk=/absolute/path/to/sco-sdk-2.4.0`
and run `sco-plugin-check` on `project/out/<id>` with and without `--short-tables`
(the generated `README.md` has the steps). For `lang: "lua"` the `check` target
checks the manifest and entry only.

The check target exercises a stand-in host and does not sandbox native code.
Python standard library suffices for discovery; CMake and a C/C++ compiler are
required for builds. A catalogue producer additionally needs Clang for compiled
ABI extraction. Source SHA and checksum inventory identify a snapshot; neither
is a publication approval or gameplay certification.

For execution, load the DLL in scuba-engine's synthetic host and connect its
separate stdio MCP bridge to the loopback host URL. Inspect host/plugin status,
invoke registered commands, and read or submit retained widget events. The static
SDK server does not gain runtime mutation tools. Game deployment remains through
a compatible product host with its own admission and capability checks.

Known boundaries: native stored-container reads, live DataCore pool binding and
mission/economy engine adapters are not provided by those contract seams. Actor
read values have no promised injury-tier interpretation. A successful scaffold,
checker or studio workflow is not an in-game result. C#/Lua support is per-method
in the catalogue, not implied by C/C++ availability.

## Capability families and ownership

| Developer need | Discover first | Composition and limits |
|---|---|---|
| Plugin commands, tasks, events and service lookup | `sco_api` entrypoints and method records | Game-neutral kernel; check complete member extents and borrowed lifetimes |
| Retained controls and persistent configuration | `sco.widgets`, `sco.storage`, settings contracts | Plugin owns policy/model; use copied data and explicit save outcomes |
| Actors and raw conditions | `game.actors`, `game.actor_stats` | Read availability is distinct from mutation authority and medical interpretation |
| Stored items versus equipped slots | `game.inventory.contents`, `game.inventory`, `game.loadout` | Separate identities, generation and asynchronous completion; stored-item native reader unavailable |
| Placement, coordinate frames and travel | `spawn.entities`, `game.entities`, `game.spatial`, `game.relocation`, `game.world` | Session handles, frame-aware poses and owned cleanup; request acceptance is not arrival |
| Ship selection and recipes | `game.asop`, DataCore pack tools | Product catalogue/policy remains downstream; record changes can affect only future instances or next launch |
| Missions and transactions | `game.missions`, `game.economy` | Typed seams do not supply native broker or wallet mechanics; no inferred payout |
| Private co-presence and bridges | `sco.net`, `sco.ipc` | Separate consumers compose sessions/transport; no game-server connection implied |

The compiler-built catalogue is the machine-readable contract index; status
metadata records capability defaults and evidence; native metadata records symbolic
locator rows. These layers answer different questions. A native row, catalogue
entry or drawn control is never an automatic assertion of runtime functionality.
