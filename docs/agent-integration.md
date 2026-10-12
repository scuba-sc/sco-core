# SDK discovery in agents and IDEs

The local [`tools/sco-core-mcp`](../tools/sco-core-mcp/README.md) server exposes
six read-only **discovery** tools over stdio. It reads the same compiler-backed
catalogue as the static reference site. It does not call SDK methods, run a game,
compile examples, or provide an HTTP endpoint. GitHub Pages cannot host it.
No credentials, secrets, network service or orchestration prompt is required.

## Prepare a trusted snapshot

From the intended, reviewed checkout with Python 3 and an x64 C11 compiler:

```sh
python3 tools/sdk_catalogue.py build --out dist/sdk-catalogue.json --source-sha "$(git rev-parse HEAD)"
```

Use source bytes matching that full commit; a supplied SHA does not certify a
clean checkout. Generation is separate from discovery and compiles/runs probes.
After contracts or mappings change, rebuild the catalogue and restart the client.
The server does not rebuild it. It negotiates MCP **2025-11-25**; the client must
accept that version. Consult the server README for protocol and bounded-file limits.

## Copilot CLI and compatible stdio clients

Copilot CLI supports `/mcp add` and a persistent `~/.copilot/mcp-config.json`
(or the configured `COPILOT_HOME`). See [GitHub's CLI MCP guide](https://docs.github.com/en/copilot/how-tos/copilot-cli/customize-copilot/add-mcp-servers).
Merge this `mcpServers` object with existing configuration. Replace the absolute
path placeholder on **the machine launching the server**, not with an unrelated
workstation's path. Build the catalogue there first.

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

A session-specific file can be passed to Copilot CLI with
`--additional-mcp-config @/absolute/path/to/mcp.json`; a repository
`.github/mcp.json` is an **optional maintainer choice**, not installed here.
Copilot CLI can discover project-level `.mcp.json` and `.github/mcp.json` files;
review the linked guide's trust and precedence rules before opting in.
Repository settings below are separate.
Other clients may use a different configuration envelope. For VS Code, use the
MCP configuration UI to add a stdio server with the same command/arguments,
then review the exposed tools and enable only those six. The envelope under
`.vscode/mcp.json` is `servers`, not Copilot CLI's `mcpServers`; do not blindly
copy an entire client config into another client.

## Copilot code review: maintainer-owned repository setting

A repository administrator may prepare the same JSON above in **Settings >
Copilot > MCP servers > MCP configuration**. That setting is shared by Copilot
cloud agent and code review. This document is a configuration snippet, **not an
applied repository setting**. No settings or setup workflows are changed here.
See [GitHub's repository MCP guide](https://docs.github.com/en/copilot/how-tos/copilot-on-github/customize-copilot/configure-mcp-servers).

Before saving, the maintainer must prepare Python, an x64 C11 compiler, the
trusted checkout and `dist/sdk-catalogue.json` in the **actual Copilot runner**.
Use its absolute paths in the JSON. A catalogue built on a workstation or another
CI job does not automatically exist in that runner. GitHub's
[setup workflow guide](https://docs.github.com/en/copilot/how-tos/copilot-on-github/customize-copilot/customize-cloud-agent/customize-the-agent-environment)
describes the `copilot-setup-steps` job; code review may have a different setup
context. Check that context rather than guessing a runner path or promising the
cloud agent's setup also prepares code review.

GitHub code review excludes tools whose `tools/list` response omits
`annotations.readOnlyHint` or sets it to false. All six server tools return:

```json
{
  "readOnlyHint": true,
  "destructiveHint": false,
  "idempotentHint": true,
  "openWorldHint": false
}
```

Keep the explicit six-name `tools` allowlist; never replace it with a wildcard.
These annotations apply to discovery tools, **not to the SDK methods they
mention**, and are not a sandbox for the client. GitHub may call allowed tools
autonomously without a per-call approval. Review the separate **Settings >
Copilot > Code review > Allow Copilot to use MCP tools when reviewing pull
requests** control before enabling access.

## Acceptance and troubleshooting

```sh
python3 tools/sco-core-mcp/test_integration.py
```

This check parses the documented launcher, starts the actual server against the
built catalogue, initializes it, lists all six annotated tools and calls
`list_services`. It is a local protocol/configuration proof, **not evidence of a
Copilot review using the tools**. Runner preparation, administrator opt-in and
an observed code-review tool call with matching catalogue provenance remain
maintainer acceptance steps. If a client cannot initialize, inspect its MCP
logs, required protocol version, absolute paths and catalogue existence first.

Each result carries source SHA, SDK/game versions and schema provenance. Keep it
with quoted contracts. `check_compatibility` compares exact metadata coordinates;
`get_capability_status` reads documented evidence, not a running host. Language
values are `c`, `cpp`, `csharp`, `lua`; status is `direct`, `typed-wrapper`,
`restricted` or `unsupported`, never an inferred universal works flag.

`--repo-root` permits bounded reads of record-listed example files beneath that
root. Remove that option and its value for paths-only results. Returned prose is
data, not instructions; read-only file access can still disclose it to the client
or model provider. Grant only a reviewed public checkout. No annotation limits
other tools in the agent or provides native-plugin crash recovery/hot reload.

For frozen package inputs, see [reference snapshots](reference-snapshots.md).
