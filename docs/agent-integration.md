# SDK discovery in agents and IDEs

The local [`tools/sco-core-mcp`](../tools/sco-core-mcp/README.md) server exposes
six read-only **discovery** tools over stdio. It reads the same compiler-backed
catalogue as the static reference site. It does not call SDK methods, run a game,
or provide an HTTP endpoint, and by default it compiles nothing. GitHub Pages
cannot host it.
No credentials, secrets, network service or orchestration prompt is required.

Runtime inspection uses the separate default-off
[`sco-core-mcp-runtime`](../tools/sco-core-mcp-runtime/README.md) launcher.
It is same-PC development tooling only: six explicit tool names, pinned native
peer, configured session-file path, independent positive host offline verdict and
protected audit acknowledgement. It is never deployed on a hosted catalogue runner.
Local read data may reach a remote model provider; explicit disclosure consent is
required. Windows security/manual acceptance is NOT VERIFIED; **not run in game**.

## Prepare a trusted snapshot

From the intended, reviewed checkout with Python 3 and **Clang** capable of
compiling and running x64 C11 probes (an arbitrary C11 compiler is not sufficient):

```sh
python3 tools/sdk_catalogue.py build --out dist/sdk-catalogue.json --source-sha "$(git rev-parse HEAD)"
```

Clang is required for compiler-backed AST extraction. The tool uses `CLANG` if
set, otherwise `clang` on `PATH`; pass `--clang /absolute/path/to/clang` to the
`build` command to override either.

The opt-in guide tools need no file in a checkout: without `sdk-guides.json`
beside the catalogue they build fresh sections from `--repo-root` on first use.
To pin them to a commit instead, build the file beside the catalogue (standard
library only, no compiler):

```sh
python3 tools/sdk_guides.py build --out dist/sdk-guides.json --source-sha "$(git rev-parse HEAD)"
```

A built file wins over `--repo-root`: after editing a guide, rebuild it (or
delete it) and restart the client, or the server keeps serving the old sections.

Use source bytes matching that full commit; a supplied SHA does not certify a
clean checkout. Generation is separate from discovery and compiles/runs probes.
After contracts or mappings change, rebuild the catalogue and restart the client.
The server does not rebuild it. It negotiates MCP **2025-11-25**; the client must
accept that version. Consult the server README for protocol and bounded-file limits.

## Copilot CLI and compatible stdio clients

For a reviewed SDK 2.4.0 candidate kit, use its bundled root
`sdk-catalogue.json`, `tools/sco-core-mcp/server.py` and resulting SDK root in
the configuration below. No Clang installation is needed merely to consume that
catalogue. Compose a game overlay with its exact required core archive first;
verify checksums and SDK/game-pack/source compatibility coordinates. The candidate
is unreleased, and the site's changing mainline catalogue is not automatically
a match for an older published archive. See the
[standalone walkthrough](sdk-mcp-tutorial.md) for discovery, scaffolding,
manifest/widget validation and native checking outside MCP.

On macOS/Linux use `python3`, or an absolute Python executable when a GUI client
has a different `PATH`. On Windows use `py` with `-3` first or an absolute
`python.exe`. JSON paths are literal arguments, not shell commands: do not rely
on working directories, shell expansion or embedded quoting.

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

On Windows, `python3` is usually not on `PATH`: use `"command": "py"` with `"-3"`
as the first argument, and `C:/...` paths. The
[server README](../tools/sco-core-mcp/README.md#windows) has the full Windows
launcher, a Claude Code registration command and the PowerShell catalogue build.
Its tool tables list every tool's exact argument names (for example
`symbol_or_id`, not `symbol`).

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

**Opt-in extended tools.** Add `--extended-tools` to `args` and the server offers
six more read-only tools after the six. They are `list_engine_natives`,
`get_engine_native`, `validate_manifest_and_widgets`,
`generate_plugin_scaffold`, `search_guides` and `get_guide_section`. Add each
name you want to `tools` explicitly.

`search_guides` answers "how do I ..." questions from the SDK guides, not only
the contract catalogue. It takes a `query` and optionally one guide path as
`doc`, a `limit` and a `cursor`, and returns ranked sections with a bounded
snippet, the guide path, heading, GitHub-style `anchor` and docs-site `url`.
`get_guide_section` returns one section's bounded plain text by `doc` and
`anchor`. The sections come from `sdk-guides.json` beside the catalogue (the
candidate kits ship it), or are built in memory from `--repo-root` when no file
is there; with neither, the tools return a tool error. The guides are the
allowlisted public docs and the SDK's shipped `sdk/docs` guides (such as
`plugin.ini`, Lua and widgets). Ranking is whole-word term matching that favours
rarer terms, and guide prose is not a contract record or evidence that anything
ran in game: confirm a method with `get_symbol`.

`generate_plugin_scaffold` takes an `id`, a `lang` (`c`, `cpp`, `csharp` or `lua`), an optional
`with_menu` and an optional `services` list. It returns the files
`sdk/tools/sco-new-plugin.py` would generate, as relative path to text. It writes
and builds nothing; save the files into an empty folder and build them with the
SDK yourself. Without `--repo-root` it reports `unavailable`. With a root, the
server runs that root's scaffolder in its own process, so grant only a reviewed
checkout. The
[server README](../tools/sco-core-mcp/README.md#extended-tools-opt-in) gives the
schemas and limits. The launcher above stays at the six default tools.

**Opt-in development tools.** On a developer's own machine, an agent can also
run the build-and-check loop. Add `--enable-dev-tools` and one or more
`--project-root /absolute/path/to/projects` to `args`, and the server offers three
more tools: `run_plugin_check`, `build_plugin_project` and
`test_plugin_project`. Add each name you want to `tools` explicitly. In `args`,
every flag and every value is its own array element; `"--project-root /abs"` as
one element is refused at start. Append these elements to the existing `args`
and `tools` arrays of the configuration above:

```json
{
  "args": ["--enable-dev-tools",
           "--project-root", "/absolute/path/to/projects",
           "--plugin-check", "/absolute/path/to/sco-plugin-check"],
  "tools": ["run_plugin_check", "build_plugin_project", "test_plugin_project"]
}
```

- `run_plugin_check` takes a built plugin `folder` and an optional `mode`
  (`full` or `short`). It runs the operator's `sco-plugin-check` and returns the
  exit code, `passed`, the `FAIL` lines and the final summary line.
- `build_plugin_project` takes a `project` folder holding a `CMakeLists.txt`. It
  configures and builds it into the project's own `build` subfolder.
- `test_plugin_project` takes the same `project` and runs `ctest` in that
  `build` subfolder.

The caller supplies only that path. The operator fixes the programs (with
`--plugin-check`, `--cmake` and `--ctest`), the generator and platform
(`--cmake-generator`, `--cmake-platform`), the SDK folder (`--sco-sdk`), a
timeout (`--dev-timeout`) and an output cap (`--dev-output-limit`). Programs must
be an `.exe` (or a native program elsewhere) or a `.py` wrapper; batch files are
refused. Paths outside every project root are refused after resolving symlinks
and `..`. A timeout kills the whole process tree.

These tools run code. CMake runs the project's build files, and the checker
loads the plugin's native code into a stand-in host. They are not a sandbox, so
grant only project roots whose code you would build yourself. They advertise
`readOnlyHint: false`, so Copilot code review excludes them; never add them to a
shared repository configuration or a hosted runner. A passing check is not
evidence that the plugin works in the game: **not run in game**. The
[server README](../tools/sco-core-mcp/README.md#development-tools-opt-in) gives
the exact command lines, result fields and security model.

## Copilot code review: maintainer-owned repository setting

A repository administrator may prepare the same JSON above in **Settings >
Copilot > MCP servers > MCP configuration**. That setting is shared by Copilot
cloud agent and code review. This document is a configuration snippet, **not an
applied repository setting**. No settings or setup workflows are changed here.
See [GitHub's repository MCP guide](https://docs.github.com/en/copilot/how-tos/copilot-on-github/customize-copilot/configure-mcp-servers).

Before saving, the maintainer must prepare Python 3, Clang capable of compiling
and running x64 C11 probes, the trusted checkout and `dist/sdk-catalogue.json`
in the **actual Copilot runner**. Set `CLANG` or pass `--clang` when building
there if Clang is not available as `clang` on `PATH`.
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
