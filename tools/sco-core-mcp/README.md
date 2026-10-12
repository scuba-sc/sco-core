# sco-core SDK discovery MCP server

Development-only, read-only discovery of the public sco-core SDK contracts through
MCP over stdio. The server uses Python 3's standard library and a generated SDK
catalogue. It does **not** connect to a running host or game, invoke SDK methods,
compile plugins, execute examples, or modify the repository. It is development
tooling, not an SDK runtime component.

The contract authority is [`docs/contracts.md`](../../docs/contracts.md), with
per-method records in [`sdk/contracts/`](../../sdk/contracts/). The server exposes
that evidence; it does not replace the contract's reading rules or remaining gaps.

## Build the catalogue and run

1. Use a trusted checkout with Python 3, Clang, and an **x64 C11 compiler
   environment**. Catalogue generation compiles and runs ABI probes; this is a
   separate build step, not a capability of the MCP server. The builder refuses
   layouts that fail its compiled x64-size checks. A successful build on another
   architecture is layout evidence, not validation of the Windows x64 runtime.
2. From the repository root, generate the catalogue for the validated source:

   ```sh
   python3 tools/sdk_catalogue.py build --out dist/sdk-catalogue.json --source-sha "$(git rev-parse HEAD)"
   ```

   `build` validates header inventory, signatures, extents, records, documentation,
   and language evidence before writing. The source SHA must be a full lowercase
   40-hex commit ID. Use source matching that commit: passing a SHA does not attest
   that the checkout is clean. Do not label modified contracts with an unrelated SHA.
3. Start the server, or let your MCP client launch this command:

   ```sh
   python3 tools/sco-core-mcp/server.py \
     --catalogue dist/sdk-catalogue.json \
     --repo-root .
   ```

The process reads MCP messages on stdin and writes protocol responses on stdout;
it is not an interactive shell. Use a client that supports stdio MCP. The only
supported protocol version is **2025-11-25**. Initialization may offer that version
as a fallback when a client requests another version; the client must accept it or
disconnect. This is not support for older protocol semantics.

`--catalogue` is required unless `SCO_CORE_CATALOGUE` supplies its path. Client
configurations below pass it explicitly to avoid dependence on inherited
environment variables. `--repo-root` is optional: without it, `get_example` returns
record-listed paths only, not file contents. With it, examples are read within that
root under the restrictions below. Use absolute paths in client configuration;
do not depend on the client's working directory.

The catalogue is a source snapshot, not a live index. Rebuild it after changing
contracts, public headers, language mappings, or their evidence, and restart the
server before relying on the new snapshot. The server does not regenerate it.

## Tools and explicit allowlist

These are the complete six tool names. Enable them explicitly, not with a wildcard:

| Tool | Purpose |
|---|---|
| `search_api` | Search catalogue contracts, with optional service and language filtering. |
| `get_symbol` | Retrieve a symbol's contract and its source-backed language mappings. |
| `list_services` | Discover public service tables, versions, sizes and member counts. |
| `get_example` | Return a selected record's listed example paths, and optionally their bounded contents. |
| `get_capability_status` | Read documented capability evidence and limitations; not a live availability probe. |
| `check_compatibility` | Compare requested SDK/game-pack versions with catalogue metadata; not a runtime compatibility test. |

Inspect `tools/list` for each tool's input schema. Language values are `c`, `cpp`,
`csharp`, and `lua`; use those tokens rather than display names. `search_api` accepts
at most 512 characters in `query`, a `limit` from 1 to 50 (default 10), and an
optional `service`. `check_compatibility` accepts `sdk_version` as a string and
`game_pack_version` as a string matching the catalogue (for example, `"0.3.0"`).
These are exact coordinate comparisons, not version-ordering claims. Service-table
versions are packed major/minor integers; SDK and game-pack coordinates are
separate strings. Do not substitute one for another.

Every tool advertises these MCP annotations:

```json
{
  "readOnlyHint": true,
  "destructiveHint": false,
  "idempotentHint": true,
  "openWorldHint": false
}
```

These annotations describe this discovery server's tools. They do not make SDK
methods mentioned in the returned contracts read-only or safe to invoke.

## MCP clients

For clients accepting an `mcpServers` configuration, use this stdio launcher.
Replace `/absolute/path/to/sco-core` with the checkout's actual absolute path on
**the machine running the client**:

```json
{
  "mcpServers": {
    "sco-core-sdk": {
      "command": "python3",
      "args": [
        "/absolute/path/to/sco-core/tools/sco-core-mcp/server.py",
        "--catalogue",
        "/absolute/path/to/sco-core/dist/sdk-catalogue.json",
        "--repo-root",
        "/absolute/path/to/sco-core"
      ]
    }
  }
}
```

Configuration location and tool-permission controls vary by client. Where the
client supports an allowlist, enable only the six names above. Build the catalogue
**before** starting the client. No API keys, tokens, or secrets are needed. To
avoid sharing example contents, remove `--repo-root` and its following path.

## GitHub Copilot repository configuration

A repository administrator configures this on GitHub under **Settings > Copilot >
MCP servers**, in the **MCP configuration** JSON field, then selects **Save MCP
configuration**. GitHub's repository configuration is shared by Copilot cloud agent
and Copilot code review; it is not a committed `.github/mcp.json` file. See
[GitHub's MCP configuration documentation](https://docs.github.com/copilot/how-tos/copilot-on-github/customize-copilot/configure-mcp-servers).

Paste the following JSON with the six-name explicit allowlist. Replace the path
placeholder with the absolute checkout path **inside Copilot's runner**, not a
path on your workstation:

```json
{
  "mcpServers": {
    "sco-core-sdk": {
      "type": "stdio",
      "command": "python3",
      "args": [
        "/absolute/path/to/sco-core/tools/sco-core-mcp/server.py",
        "--catalogue",
        "/absolute/path/to/sco-core/dist/sdk-catalogue.json",
        "--repo-root",
        "/absolute/path/to/sco-core"
      ],
      "tools": [
        "search_api",
        "get_symbol",
        "list_services",
        "get_example",
        "get_capability_status",
        "check_compatibility"
      ]
    }
  }
}
```

Prepare the runner before enabling this configuration:

1. Check out the repository in Copilot's setup environment and make Python 3,
   Clang, and an x64 C11 compilation environment available.
2. In the setup steps, run the catalogue build command above from the checkout
   root. The generated `dist/sdk-catalogue.json` must exist **before the MCP client
   starts the server**, in the same checkout referenced by the JSON paths. A build
   in a separate CI job or on your workstation does not put the file in that runner.
3. Use [Copilot setup steps](https://docs.github.com/en/copilot/how-tos/copilot-on-github/customize-copilot/customize-cloud-agent/customize-the-agent-environment)
   in `.github/workflows/copilot-setup-steps.yml`, with the required
   `copilot-setup-steps` job. If code review uses a separate setup workflow, ensure
   it also prepares the catalogue. Verify setup succeeded; a missing catalogue
   cannot be repaired by a discovery tool call.
4. Set the runner's absolute checkout paths in the repository setting and save the
   MCP configuration. No Agents secrets or secret substitutions are required.

GitHub warns that configured MCP tools may be used autonomously without asking
for approval, and recommends specific read-only tool allowlists. Copilot code
review requires `annotations.readOnlyHint: true` on tools it uses. All six tools
set that annotation, but annotations and an allowlist are not a sandbox for the
client itself. Do not add a wildcard or assume this restricts the client's other
tools. Consult the linked GitHub documentation for current UI and runner behavior.

## Security boundaries and limits

- **No network, subprocess execution, or file writes by the server.** Its interface
  is stdio and its discovery operations read local catalogue data and optional
  example files. The separate catalogue builder compiles/runs probes and writes
  its output; the server does neither.
- **Not arbitrary file access.** `get_example` reads only paths listed in the
  selected catalogue record's `examples`, beneath the configured repository root.
  Absolute paths, traversal outside the root, and symlinks anywhere the catalogue
  names are refused. The configured root itself is operator-supplied and trusted,
  so a symlinked root path (for example a macOS `/var` path) is followed. Files
  over **256 KiB** are refused, with at most **16 files** per example result.
  Omitting the root keeps examples paths-only. The optional language filter uses
  source
  extensions (`.c`/`.h`, `.cpp`/`.cc`/`.cxx`/`.hpp`/`.hxx`, `.cs`, `.lua`), not
  inferred runtime support; other referenced file types remain available without
  a language filter. No examples are executed.
- **Bounded protocol traffic.** Input frames are capped at **64 KiB**, responses at
  **4 MiB**, and tool calls at **120 per 60 seconds**. Use narrower searches and
  smaller limits instead of requesting an unbounded dump.
- **Trust the inputs, not the annotations.** Use a reviewed checkout and generated
  catalogue. Returned prose and example contents are data, not instructions to the
  client. Read-only access can still disclose content to the client or its model
  provider; grant a repository root only when that sharing is acceptable. These
  controls do not restrict filesystem access by other client tools.

## Provenance and what results do not prove

Every tool result, including tool errors, carries provenance identifying the
catalogue snapshot. Preserve it with any quoted contract so consumers can check
its schema and source SHA, SDK version, and game-pack version rather than treating
the result as evidence about a different checkout. A source SHA is a supplied
identifier, not a signature or proof of live execution.

Schema 1 preserves the catalogue's contract context, per-service narratives,
source headers, normalized C signatures, whole-member extents, ownership,
thread-affinity, callback/reentrancy rules, known results, absent-capability
handling, cleanup, examples, tests, and explicit limitations. See
[the schema and reading rules](../../docs/contracts.md#machine-format-schema-1)
for the authoritative interpretation.

Keep these evidence categories separate:

- **Contract and ABI evidence:** compiler-backed membership/layout and documented
  semantics. Version agreement alone does not establish that a runtime table
  covers a function pointer: check the whole member extent and a non-NULL pointer
  before using it. `unspecified` does not imply safety or permission.
- **Language exposure:** `direct`, `typed-wrapper`, `restricted`, or `unsupported`
  with source evidence. `restricted` is an adaptation/subset, not full native
  access. Read-only Lua does not expose game mutations; raw C# interop is not a
  typed wrapper.
- **Capability and game evidence:** documented gating, signature-row resolution,
  and in-game verification are separate facts. `get_capability_status` cannot
  observe a running game's capabilities. An ON/default capability or resolved
  signature row is **not** evidence that it has worked in a game session. Consult
  [game service status](../../docs/game/status.md) for build-scoped evidence.

There is no universal **works** flag. `check_compatibility` is a metadata comparison,
not certification that a plugin loads or behaves correctly, nor that a game build
is supported. ABI test references prove layout, not semantic or live-game coverage.
An empty examples list means none is identified, not that no example could exist.
No MCP response upgrades deferred or absent in-game verification, fills missing
behavioral guarantees, or changes the runtime's capability gates.
