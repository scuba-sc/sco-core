# SDK discovery in agents and IDEs

The local [`tools/sco-core-mcp`](../tools/sco-core-mcp/README.md) server exposes
read-only **discovery** tools over stdio: six by default, twelve with
`--extended-tools`. It reads the same compiler-backed catalogue as the static
reference site. It does not call SDK methods, run a game, or provide an HTTP
endpoint, and by default it compiles nothing. GitHub Pages cannot host it.
No credentials, secrets, network service or orchestration prompt is required.

Runtime inspection uses the separate default-off
[`sco-core-mcp-runtime`](../tools/sco-core-mcp-runtime/README.md) launcher.
It is same-PC development tooling only: six explicit tool names, pinned native
peer, configured session-file path, independent positive host offline verdict and
protected audit acknowledgement. It is never deployed on a hosted catalogue runner.
Local read data may reach a remote model provider; explicit disclosure consent is
required. Windows security/manual acceptance is NOT VERIFIED; **not run in game**.

## Set up the discovery server from the SDK kit

This is the one setup path for an SDK developer, and the one the
[server README](../tools/sco-core-mcp/README.md) and the
[SDK tutorial](sdk-mcp-tutorial.md) point to. If you maintain a source checkout and
build your own catalogue, go to
[Maintainers: source checkout](#maintainers-source-checkout-and-your-own-catalogue).

You need Python 3.9 or newer and an MCP client that supports stdio and protocol
`2025-11-25`, the only version the server speaks; a client that cannot accept it
disconnects when it initializes. The kit needs no compiler, Clang, network service
or credentials.

**Known issue in release candidate 1.** The kit's bundled guides (`sdk-guides.json`,
served by `search_guides` and `get_guide_section`) and the READMEs inside it were
frozen before publication. They can say that 2.4.0 is not on the release page and
that 2.3.0 is the newest published SDK. Both are out of date: the
[docs site](https://scuba-sc.github.io/sco-core/) and `main` supersede them, and the
final 2.4.0 kit will be built from current docs. See the
[release notes](releases/sdk-v2.4.0.md#known-issues-in-release-candidate-1).

### Download and verify the kit

Download `sco-sdk-2.4.0.zip` and `SHA256SUMS` from the
[`sdk-v2.4.0-rc.1`](https://github.com/scuba-sc/sco-core/releases/tag/sdk-v2.4.0-rc.1)
release into an empty folder. Use this combined kit, not `sco-sdk-core-2.4.0.zip`:
the core archive has no catalogue, no guides and no server. (The `game-sc` overlay
composed over its exact core archive also carries them; see
[archive composition](../sdk/README.md#zips).)

macOS and Linux (on macOS use `shasum -a 256 -c -` for the check):

```sh
gh release download sdk-v2.4.0-rc.1 -R scuba-sc/sco-core -p 'sco-sdk-2.4.0.zip' -p SHA256SUMS
grep ' sco-sdk-2.4.0.zip$' SHA256SUMS | sha256sum -c -
```

Windows PowerShell:

```powershell
gh release download sdk-v2.4.0-rc.1 -R scuba-sc/sco-core -p 'sco-sdk-2.4.0.zip' -p SHA256SUMS
$expected = ((Get-Content SHA256SUMS | Where-Object { $_ -match ' sco-sdk-2\.4\.0\.zip$' }) -split '\s+')[0]
if ((Get-FileHash sco-sdk-2.4.0.zip -Algorithm SHA256).Hash -ieq $expected) { 'SHA256 OK' } else { 'SHA256 MISMATCH: stop' }
```

Stop on a mismatch and download both files again. You can also download them in a
browser from the release page. The same digest is in the
[asset table](getting-started.md#sdk-240-release-candidate-1-assets); what a
checksum downloaded beside its archive does and does not establish is described in
[Download, verify, extract](getting-started.md#3-download-verify-extract).

### Find the SDK folder

**SDK** means the folder that directly contains `sdk-catalogue.json`. For the
combined kit that is `<extract folder>/sco-sdk-2.4.0`. Extract into a new, empty
folder, then check the path. Some extraction tools add one more folder level, so
check rather than assume.

macOS and Linux:

```sh
mkdir sdk-kit && unzip -q sco-sdk-2.4.0.zip -d sdk-kit
SDK="$PWD/sdk-kit/sco-sdk-2.4.0"
test -f "$SDK/sdk-catalogue.json" && echo "SDK ok: $SDK" || echo "not the SDK folder: $SDK"
```

Windows PowerShell:

```powershell
Expand-Archive sco-sdk-2.4.0.zip -DestinationPath sdk-kit
$SDK = (Resolve-Path sdk-kit/sco-sdk-2.4.0).Path -replace '\\', '/'
if (Test-Path "$SDK/sdk-catalogue.json") { "SDK ok: $SDK" } else { "not the SDK folder: $SDK" }
```

The SDK folder holds `sdk-catalogue.json`, `sdk-guides.json`, `native_index.json`,
`status_registry.json`, `COMPATIBILITY.json`, `VERSION` and
`tools/sco-core-mcp/server.py`. The kit has no `sdk/` and no `dist/` folder.
Client configuration files do not expand `$SDK`, so write the absolute path out.
The examples below use `/absolute/path/to/sco-sdk-2.4.0` (macOS and Linux) and
`C:/absolute/path/to/sco-sdk-2.4.0` (Windows) for it.

### Choose a profile

Every example on this page uses one of two profiles:

| Profile | Server flag | Tools |
| --- | --- | --- |
| **SDK developer profile (recommended)** | `--extended-tools` | 12 read-only tools |
| **Minimal profile** | none | 6 read-only tools |

The six default tools are `search_api`, `get_symbol`, `list_services`,
`get_example`, `get_capability_status` and `check_compatibility`. `--extended-tools`
adds `list_engine_natives`, `get_engine_native`, `validate_manifest_and_widgets`,
`generate_plugin_scaffold`, `search_guides` and `get_guide_section`. The
[SDK tutorial](sdk-mcp-tutorial.md)'s scaffold, validation, native and guide steps
need all twelve.

- The developer profile also runs the kit's own `tools/sco-new-plugin.py`, on the
  first scaffold call, inside the server process, and `tools/sdk_guides.py` only
  if `sdk-guides.json` is missing. Use it on a kit you downloaded from the release
  and verified. The minimal profile runs no code from the SDK folder.
- Where your client has a tool allowlist, list the exact names of the profile you
  chose. Never use a wildcard. A name left out of an allowlist hides that tool from
  the agent.
- `--repo-root` is optional. Without it the server returns no example contents,
  engine natives, scaffold, status evidence or release-approval records (each
  reports `unavailable`), and stability labels are `unknown`. The guide tools still
  work when `sdk-guides.json` sits beside the catalogue. Leave `--repo-root` out only
  when you do not want example file contents to reach your model provider.
- Three build-and-check tools are a separate opt-in
  ([development tools](#opt-in-development-tools)). Do not put them in a shared
  configuration.

### Register the server in your client

These rules apply to every client below.

- **Name.** Use `sco-core-sdk`. Some clients put the name in tool names; in Claude
  Code a permission rule reads `mcp__sco-core-sdk__search_api`.
- **Python launcher.** On macOS and Linux use `python3`, or an absolute
  interpreter path when a GUI client does not inherit your shell `PATH`. On
  Windows use `py` with `-3` as the first argument, or an absolute path to
  `python.exe`, which is the safer choice for a GUI client. A standard Windows
  Python install provides `py` (and usually `python`) but not `python3`, and a
  `python3` alias that does exist can be the Microsoft Store stub, which opens the
  Store instead of starting Python.
- **Arguments.** Every flag and every value is its own array element:
  `"--catalogue"`, then the path, never one `"--catalogue /path"` string. Every
  path is absolute, with forward slashes on Windows. Do not rely on the client's
  working directory, `~`, environment variables or shell quoting.
- **The arguments in order.** `SDK/tools/sco-core-mcp/server.py`, `--catalogue`,
  `SDK/sdk-catalogue.json`, `--repo-root`, `SDK`, then `--extended-tools` for the
  developer profile. Leave `--extended-tools` out for the minimal profile.

**What was tested.** The server command itself was run on Windows with `py -3` from
the release candidate 1 kit. With `--extended-tools` it served twelve tools, without
it six, and the tool calls tried returned results. Not run in game.
`tools/sco-core-mcp/test_integration.py` starts the server with the arguments of
the JSON blocks on this page, using a kit-shaped folder it assembles from this
repository. The per-client configuration files below follow each client's documented
format. They were not each tested in that client.

#### Claude Code

User scope, so every project sees the server. Windows PowerShell:

```powershell
claude mcp add --scope user sco-core-sdk -- py -3 C:/absolute/path/to/sco-sdk-2.4.0/tools/sco-core-mcp/server.py --catalogue C:/absolute/path/to/sco-sdk-2.4.0/sdk-catalogue.json --repo-root C:/absolute/path/to/sco-sdk-2.4.0 --extended-tools
```

macOS and Linux:

```sh
claude mcp add --scope user sco-core-sdk -- python3 /absolute/path/to/sco-sdk-2.4.0/tools/sco-core-mcp/server.py --catalogue /absolute/path/to/sco-sdk-2.4.0/sdk-catalogue.json --repo-root /absolute/path/to/sco-sdk-2.4.0 --extended-tools
```

The `--` ends Claude Code's own options; everything after it is the server command.
For project scope, save the generic `mcpServers` object below as `.mcp.json` in the
project root. Claude Code asks you to approve a project-scoped server before it
connects. The paths are absolute, so do not commit that file for other people.

#### Generic `mcpServers` JSON

Clients that read an `mcpServers` object (Claude Code's `.mcp.json`, Cursor and
others) take this entry. macOS and Linux:

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
        "/absolute/path/to/sco-sdk-2.4.0",
        "--extended-tools"
      ]
    }
  }
}
```

Windows: `py` with `-3` first, and `C:/` paths.

```json
{
  "mcpServers": {
    "sco-core-sdk": {
      "command": "py",
      "args": [
        "-3",
        "C:/absolute/path/to/sco-sdk-2.4.0/tools/sco-core-mcp/server.py",
        "--catalogue",
        "C:/absolute/path/to/sco-sdk-2.4.0/sdk-catalogue.json",
        "--repo-root",
        "C:/absolute/path/to/sco-sdk-2.4.0",
        "--extended-tools"
      ]
    }
  }
}
```

Merge the `sco-core-sdk` entry into the file's existing `mcpServers` object. Do not
paste a whole client configuration into another client.

#### Codex

Either register it from a shell, or edit `~/.codex/config.toml` (on Windows,
`.codex\config.toml` under your user folder). The command, on macOS and Linux:

```sh
codex mcp add sco-core-sdk -- python3 /absolute/path/to/sco-sdk-2.4.0/tools/sco-core-mcp/server.py --catalogue /absolute/path/to/sco-sdk-2.4.0/sdk-catalogue.json --repo-root /absolute/path/to/sco-sdk-2.4.0 --extended-tools
```

The equivalent `config.toml` table:

```toml
[mcp_servers.sco-core-sdk]
command = "python3"
args = [
  "/absolute/path/to/sco-sdk-2.4.0/tools/sco-core-mcp/server.py",
  "--catalogue",
  "/absolute/path/to/sco-sdk-2.4.0/sdk-catalogue.json",
  "--repo-root",
  "/absolute/path/to/sco-sdk-2.4.0",
  "--extended-tools",
]
```

On Windows set `command = "py"` and put `"-3"` first in `args`, with `C:/` paths.

#### Cursor

Cursor reads the same `mcpServers` object as above, from `.cursor/mcp.json` in a
project or `~/.cursor/mcp.json` for every project. Cursor is a GUI client, so on
Windows use the `py` form above, or an absolute `python.exe` as `command` with no
`-3` argument.

#### VS Code

VS Code reads `.vscode/mcp.json`, and its top-level key is `servers`, not
`mcpServers`. It also wants `"type": "stdio"`:

```json
{
  "servers": {
    "sco-core-sdk": {
      "type": "stdio",
      "command": "python3",
      "args": [
        "/absolute/path/to/sco-sdk-2.4.0/tools/sco-core-mcp/server.py",
        "--catalogue",
        "/absolute/path/to/sco-sdk-2.4.0/sdk-catalogue.json",
        "--repo-root",
        "/absolute/path/to/sco-sdk-2.4.0",
        "--extended-tools"
      ]
    }
  }
}
```

On Windows use `"command": "py"`, `"-3"` first in `args` and `C:/` paths. Review the
tools VS Code lists and enable the profile's tools. A workspace file with absolute
paths belongs on your machine only.

#### Copilot CLI

Copilot CLI supports `/mcp add` and a persistent `~/.copilot/mcp-config.json`
(or the configured `COPILOT_HOME`). See
[GitHub's CLI MCP guide](https://docs.github.com/en/copilot/how-tos/copilot-cli/customize-copilot/add-mcp-servers).
Merge this `mcpServers` entry with the existing configuration, on **the machine
launching the server**:

```json
{
  "mcpServers": {
    "sco-core-sdk": {
      "type": "stdio",
      "command": "python3",
      "args": [
        "/absolute/path/to/sco-sdk-2.4.0/tools/sco-core-mcp/server.py",
        "--catalogue",
        "/absolute/path/to/sco-sdk-2.4.0/sdk-catalogue.json",
        "--repo-root",
        "/absolute/path/to/sco-sdk-2.4.0",
        "--extended-tools"
      ],
      "tools": [
        "search_api",
        "get_symbol",
        "list_services",
        "get_example",
        "get_capability_status",
        "check_compatibility",
        "list_engine_natives",
        "get_engine_native",
        "validate_manifest_and_widgets",
        "generate_plugin_scaffold",
        "search_guides",
        "get_guide_section"
      ]
    }
  }
}
```

For the minimal profile, remove `--extended-tools` and the last six names from
`tools`. On Windows use `"command": "py"` and `"-3"` first in `args`.

A session-specific file can be passed with
`--additional-mcp-config @/absolute/path/to/mcp.json`. A repository
`.github/mcp.json` is an **optional maintainer choice**, not installed here. Copilot
CLI can discover project-level `.mcp.json` and `.github/mcp.json` files; review the
linked guide's trust and precedence rules before opting in. Repository settings are
separate, see
[the maintainer-owned repository setting](#copilot-code-review-maintainer-owned-repository-setting).

### Verify the connection

1. Restart the client, or reload its MCP servers.
2. Check that the server is connected. In Claude Code, `claude mcp list` or `/mcp`
   shows `sco-core-sdk` as connected; approve a project-scoped server first. In other
   clients, open the MCP or tools panel and look for `sco-core-sdk` running.
3. Check `tools/list`: twelve tools for the developer profile, six for the minimal
   profile, all with `readOnlyHint: true`. Ask the agent to list the server's tools
   if your client has no tools view.
4. Call `list_services` with `{}`. Every result carries `provenance`. Compare it
   with `SDK/COMPATIBILITY.json`: `sdk_version` must equal `core_sdk_version`
   (2.4.0 for this kit), `game_pack_version` must equal `game_overlay_version`
   (0.8.0), and `source_sha` must equal the `source_commits` entry. A different value
   means the server is reading another catalogue, such as the site's changing
   mainline file.
5. If the client only says that the server failed, run the exact command in a
   terminal. A healthy server prints nothing and waits for MCP messages on stdin;
   stop it with Ctrl+C. A bad `--catalogue` path prints one line and exits.

Common failures:

| What you see | Cause and fix |
| --- | --- |
| `sco-core-mcp: catalogue refused: FileNotFoundError` and the client shows the server as failed or disconnected | The `--catalogue` file does not exist. SDK is the wrong folder (the extract folder instead of `sco-sdk-2.4.0`, or a checkout's `dist/` path). Run the existence check from "Find the SDK folder". |
| The server connects, but `get_example` fails and engine natives, the scaffold or status report `unavailable` | `--repo-root` is not the SDK folder, for example one level too high. The server starts anyway and the `reason` names the file it could not read under `--repo-root`. Set it to SDK. |
| `Unknown tool` (JSON-RPC `-32602`), or only six tools | `--extended-tools` is missing, or the client's allowlist leaves the names out. Add the flag and the six extended names, then restart. |
| The server does not start on Windows, or the Microsoft Store opens | `python3` is not a standard Windows command. Use `py` with `-3`, or an absolute `python.exe`. |
| The client disconnects while initializing | The client does not support protocol `2025-11-25`. Update the client. |
| `check_compatibility` says a `tag` does not match the advertised pattern | Known issue in release candidate 1: the tag pattern is `sdk-vMAJOR.MINOR.PATCH`, so `sdk-v2.4.0-rc.1` is refused. Compare `sdk_version` and `game_pack_version` instead. |
| `search_guides` text says 2.4.0 is not on the release page | Known issue in release candidate 1, described at the top of this section. The [docs site](https://scuba-sc.github.io/sco-core/) supersedes the kit's guides. |

### The extended tools

`search_guides` answers "how do I ..." questions from the SDK guides, not only
the contract catalogue. It takes a `query` and optionally one guide path as
`doc`, a `limit` and a `cursor`, and returns ranked sections with a bounded
snippet, the guide path, heading, GitHub-style `anchor` and docs-site `url`.
`get_guide_section` returns one section's bounded plain text by `doc` and
`anchor`. The sections come from `sdk-guides.json` beside the catalogue (the
kit ships it), or are built in memory from `--repo-root` when no file is there;
with neither, the tools return a tool error. The guides are the allowlisted
public docs and the SDK's shipped `sdk/docs` guides (such as `plugin.ini`, Lua
and widgets). Ranking is whole-word term matching that favours rarer terms, and
guide prose is not a contract record or evidence that anything ran in game:
confirm a method with `get_symbol`.

`generate_plugin_scaffold` takes an `id`, a `lang` (`c`, `cpp`, `csharp` or `lua`), an optional
`with_menu` and an optional `services` list. It returns the files
`sco-new-plugin.py` would generate (`tools/` in a kit, `sdk/tools/` in a checkout),
as relative path to text. It writes and builds nothing; save the files into an
empty folder and build them with the SDK yourself. Without `--repo-root` it
reports `unavailable`. With a root, the server runs that root's scaffolder in its
own process, so grant only a kit or checkout you have reviewed. The
[server README](../tools/sco-core-mcp/README.md#extended-tools-opt-in) gives the
schemas and limits.

### Opt-in development tools

On a developer's own machine, an agent can also run the build-and-check loop. Add
`--enable-dev-tools` and one or more `--project-root /absolute/path/to/projects` to
the server's `args`, and the server offers three more tools: `run_plugin_check`,
`build_plugin_project` and `test_plugin_project`. Where your client has an
allowlist, add each name you want to `tools` explicitly. In `args`, every flag and
every value is its own array element; `"--project-root /abs"` as one element is
refused at start. Append these elements to the existing `args` and `tools` of the
`sco-core-sdk` entry above; do not add a second server:

```json
{
  "args": ["--enable-dev-tools",
           "--project-root", "/absolute/path/to/projects",
           "--plugin-check", "/absolute/path/to/sco-plugin-check"],
  "tools": ["run_plugin_check", "build_plugin_project", "test_plugin_project"]
}
```

On Windows the checker must be an `.exe` (for example
`C:/absolute/path/to/checker-build/Release/sco-plugin-check.exe`) or a `.py`
wrapper, and a Visual Studio generator needs `"--cmake-generator"`,
`"Visual Studio 17 2022"`, `"--cmake-platform"` and `"x64"` as four more elements.
The [SDK tutorial](sdk-mcp-tutorial.md#optional-let-the-agent-build-and-check) shows
how to build the checker.

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

## Maintainers: source checkout and your own catalogue

A source checkout has no kit layout: the catalogue is a file you build
(`dist/sdk-catalogue.json`), the SDK files sit under `sdk/`, and Clang is required.
Outside developers should use the kit above.

From the intended, reviewed checkout with Python 3.9 or newer and **Clang** capable of
compiling and running x64 C11 probes (an arbitrary C11 compiler is not sufficient):

```sh
python3 tools/sdk_catalogue.py build --out dist/sdk-catalogue.json --source-sha "$(git rev-parse HEAD)"
```

In Windows PowerShell, with a Clang on `PATH` that targets x64:

```powershell
py -3 tools/sdk_catalogue.py build --out dist/sdk-catalogue.json --source-sha (git rev-parse HEAD)
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
The server does not rebuild it. Build the catalogue **before** registering the
server, on the machine that launches it.

This is the checkout form of the minimal profile. Replace the placeholder with the
checkout's absolute path on **that machine**. On Windows use `"py"` with `"-3"`
first and `C:/` paths.

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

For the developer profile in your own checkout, add `"--extended-tools"` to `args` and
the six extended names to `tools`. The profile rule, the arguments rules and the
client files above apply unchanged; only the paths differ.

## Copilot code review: maintainer-owned repository setting

A repository administrator may prepare the checkout JSON above in **Settings >
Copilot > MCP servers > MCP configuration**. That setting is shared by Copilot
cloud agent and code review. This document is a configuration snippet, **not an
applied repository setting**. No settings or setup workflows are changed here.
See [GitHub's repository MCP guide](https://docs.github.com/en/copilot/how-tos/copilot-on-github/customize-copilot/configure-mcp-servers).
Use the minimal profile there: a hosted runner should run no code from the
checkout, so leave out `--extended-tools` and the development tools.

Before saving, the maintainer must prepare Python 3.9 or newer, Clang capable of compiling
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
`annotations.readOnlyHint` or sets it to false. All twelve discovery tools return:

```json
{
  "readOnlyHint": true,
  "destructiveHint": false,
  "idempotentHint": true,
  "openWorldHint": false
}
```

Keep the explicit six-name `tools` allowlist of the minimal profile; never replace
it with a wildcard.
These annotations apply to discovery tools, **not to the SDK methods they
mention**, and are not a sandbox for the client. GitHub may call allowed tools
autonomously without a per-call approval. Review the separate **Settings >
Copilot > Code review > Allow Copilot to use MCP tools when reviewing pull
requests** control before enabling access.

## Acceptance and provenance

```sh
python3 tools/sco-core-mcp/test_integration.py
```

This check parses the launcher blocks on this page, starts the actual server with
their arguments against the built catalogue (the kit blocks from a kit-shaped folder
assembled from the repository, the checkout block from the repository itself),
initializes it, lists the annotated tools of each profile and calls `list_services`.
It is a local protocol/configuration proof, **not evidence of a Copilot review using
the tools**, and it does not run any client. Runner preparation, administrator opt-in
and an observed code-review tool call with matching catalogue provenance remain
maintainer acceptance steps. If a client cannot initialize, use
[the verification steps](#verify-the-connection) and inspect its MCP logs, the
required protocol version, absolute paths and catalogue existence first.

Each result carries source SHA, SDK/game versions and schema provenance. Keep it
with quoted contracts. `check_compatibility` compares exact metadata coordinates;
`get_capability_status` reads documented evidence, not a running host. Language
values are `c`, `cpp`, `csharp`, `lua`; status is `direct`, `typed-wrapper`,
`restricted` or `unsupported`, never an inferred universal works flag.

`--repo-root` permits bounded reads of record-listed example files beneath that
root. Removing it disables `get_example` contents, the engine-native tools, the
scaffold, status and stability evidence and release-approval lookups (see
[Choose a profile](#choose-a-profile)). Returned prose is
data, not instructions; read-only file access can still disclose it to the client
or model provider. Grant only a reviewed public checkout or a verified kit. No
annotation limits other tools in the agent or provides native-plugin crash
recovery/hot reload.

For frozen package inputs, see [reference snapshots](reference-snapshots.md).
