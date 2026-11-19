# SDK discovery in agents and IDEs

Connect an agent or IDE to the SDK's local MCP server in four steps: [download the
kit](#download-and-verify-the-kit), [find the SDK folder](#find-the-sdk-folder), [register the
server](#register-the-server-in-your-client) in your client, and [verify the connection](#verify-the-connection).
For durable consumer procedures, see the [roadmap-driven workflow skills](agent-workflows.md).

The local [`tools/sco-core-mcp`](../tools/sco-core-mcp/README.md) server exposes read-only **discovery** tools
over stdio: 15 by default (API discovery, scaffolding, validation, guides and workflows), or the original
six with `--minimal-tools`. It reads the same compiler-backed catalogue as the static reference site. It does
not call SDK methods, run a game or provide an HTTP endpoint, and by default it compiles nothing. GitHub
Pages cannot host it. It needs no credentials or network access.

Runtime inspection uses the separate default-off
[`sco-core-mcp-runtime`](../tools/sco-core-mcp-runtime/README.md) launcher: same-PC development tooling only,
with six explicit tool names, a pinned native peer, a configured session-file path, an independent positive
host offline verdict and a protected audit acknowledgement. It is never deployed on a hosted catalogue runner.
Local read data may reach a remote model provider; explicit disclosure consent is required. The
Windows security acceptance of this launcher has not been done. Not run in game. See [game capability
status](game/status.md).

## Set up the discovery server from the SDK kit

This is the one setup path for an SDK developer; the [server README](../tools/sco-core-mcp/README.md) and the
[SDK tutorial](sdk-mcp-tutorial.md) point here. Maintaining a source checkout? Go to [Maintainers: source
checkout](#maintainers-source-checkout-and-your-own-catalogue). Holding an older kit? See [Earlier
releases](#previous-release-kits).

You need Python 3.9 or newer (check with `py -3 --version` on Windows or `python3 --version` on macOS and
Linux) and an MCP client that supports stdio. The SDK <!--rv:sdk_version-->2.4.1<!--/rv--> kit speaks legacy
protocol `2025-11-25` and `2026-07-28`, and advertises both through `server/discover`; a client must complete
the handshake for a version it speaks. The kit needs no compiler, Clang, network service or credentials.

**Release identity.** The current release:

<!-- release-block:current-release-brief -->
SDK **2.4.1** uses Star Citizen game pack **0.8.1** (checked against Star Citizen
Alpha 4.10.3, build 4.10.200.58746; see [game builds](../docs/game/builds.md)) and plugin ABI **1.1**. Archives and their digests are on the
[`sdk-v2.4.1` release page](https://github.com/scuba-sc/sco-core/releases/tag/sdk-v2.4.1); the [release notes](https://github.com/scuba-sc/sco-core/blob/sdk-v2.4.1/docs/releases/sdk-v2.4.1.md) list what changed.
<!-- /release-block:current-release-brief -->

The release page lists the five profile archives, their SBOMs, `SHA256SUMS` and `release.json`. Later source
edits do not alter any published archive.

### Download and verify the kit

Download the combined kit, not the `core` archive, which has no catalogue, guides or server. (The `game-sc`
overlay composed over its exact core archive also carries them; see
[archive composition](../sdk/README.md#zips).) Work in a new, empty folder that you will keep, such as
`~/sco-sdk` or `C:/sco-sdk`. The commands check the archive against `SHA256SUMS` before extraction. The
`gh release download` line needs the GitHub CLI (`gh`), signed in with `gh auth login`; without it, download
the archive and `SHA256SUMS` in a browser from the release page and run the lines after the `gh` line.

macOS and Linux (on macOS use `shasum -a 256 -c -`):

<!-- release-block:kit-download-sh -->
```sh
set -e
gh release download sdk-v2.4.1 -R scuba-sc/sco-core -p 'sco-sdk-2.4.1.zip' -p SHA256SUMS
grep ' sco-sdk-2.4.1.zip$' SHA256SUMS | sha256sum -c -
mkdir sdk-kit && unzip -q sco-sdk-2.4.1.zip -d sdk-kit
SDK="$PWD/sdk-kit/$(basename sco-sdk-2.4.1.zip .zip)"
test -f "$SDK/sdk-catalogue.json"
```
<!-- /release-block:kit-download-sh -->

Windows PowerShell:

<!-- release-block:kit-download-ps -->
```powershell
gh release download sdk-v2.4.1 -R scuba-sc/sco-core -p 'sco-sdk-2.4.1.zip' -p SHA256SUMS
if ($LASTEXITCODE -ne 0) { throw 'SDK download failed' }
$expected = ((Get-Content SHA256SUMS | Where-Object { $_ -match (' ' + [regex]::Escape('sco-sdk-2.4.1.zip') + '$') }) -split '\s+')[0]
if (!$expected -or (Get-FileHash sco-sdk-2.4.1.zip -Algorithm SHA256).Hash -ine $expected) { throw 'SHA256 mismatch; stop' }
Expand-Archive sco-sdk-2.4.1.zip -DestinationPath sdk-kit
$SDK = (Join-Path (Resolve-Path sdk-kit).Path ([IO.Path]::GetFileNameWithoutExtension('sco-sdk-2.4.1.zip'))) -replace '\\', '/'
if (!(Test-Path "$SDK/sdk-catalogue.json")) { throw 'Not the SDK folder' }
```
<!-- /release-block:kit-download-ps -->

Stop on a mismatch and download both files again. A checksum file downloaded beside its archive only catches a
damaged download; what it does and does not establish is described in [Download, verify,
extract](getting-started.md#3-download-verify-extract).

### Find the SDK folder

The **SDK folder** is the folder that directly contains `sdk-catalogue.json`: for the combined kit,
`<extract folder>/sco-sdk-<version>`. This page writes its path as `SDK`, and as `$SDK` in a shell. Keep the
folder where you extracted it: the client stores the absolute path, so after you move or delete it, replace
the registration (see "Replacing a registration" under
[Register the server in your client](#register-the-server-in-your-client)). Some extraction tools add one more
folder level, so check the path. The download block above already extracted the kit and set `SDK`; run the
block below only if you extracted the zip another way, or run just its last line to check.

macOS and Linux:

<!-- release-block:kit-extract-sh -->
```sh
mkdir sdk-kit && unzip -q sco-sdk-2.4.1.zip -d sdk-kit
SDK="$PWD/sdk-kit/$(basename sco-sdk-2.4.1.zip .zip)"
test -f "$SDK/sdk-catalogue.json" && echo "SDK ok: $SDK" || echo "not the SDK folder: $SDK"
```
<!-- /release-block:kit-extract-sh -->

Windows PowerShell:

<!-- release-block:kit-extract-ps -->
```powershell
Expand-Archive sco-sdk-2.4.1.zip -DestinationPath sdk-kit
$SDK = (Join-Path (Resolve-Path sdk-kit).Path ([IO.Path]::GetFileNameWithoutExtension('sco-sdk-2.4.1.zip'))) -replace '\\', '/'
if (Test-Path "$SDK/sdk-catalogue.json") { "SDK ok: $SDK" } else { "not the SDK folder: $SDK" }
```
<!-- /release-block:kit-extract-ps -->

`Expand-Archive` stops with `Failed to create file ... already exists` when `sdk-kit` already holds the kit's
files; an existing empty folder is fine. Extract into a new folder name instead, or add `-Force`, which
overwrites files of the same name with the zip's copies (keep nothing you edited there) and leaves other files
alone. If it says `not the SDK folder`, find the real one with `find sdk-kit -name sdk-catalogue.json` (macOS
and Linux) or `Get-ChildItem sdk-kit -Recurse -Filter sdk-catalogue.json` (PowerShell), set `$SDK` to the
absolute path of the folder that holds the file (forward slashes on Windows) and check again.

The SDK folder holds `sdk-catalogue.json`, `sdk-guides.json`, `native_index.json`, `status_registry.json`,
`COMPATIBILITY.json`, `VERSION` and `tools/sco-core-mcp/server.py`. The kit has no `sdk/` and no `dist/` folder,
so the paths that tools return, which use the source-checkout layout and start with `sdk/`, lose that prefix
here: `sdk/docs/plugin-ini.md` is `docs/plugin-ini.md` and `sdk/examples/widgets/widgets.c` is
`examples/widgets/widgets.c`, both under the SDK folder.

Client configuration files do not expand `$SDK`, so write the absolute path out there: the examples use
`/absolute/path/to/sco-sdk-<version>` (macOS and Linux) and `C:/absolute/path/to/sco-sdk-<version>` (Windows).
The `claude mcp add` and `codex mcp add` commands below can use `$SDK` because your shell expands it.

### Check the kit's identity

Start with the extracted kit's own `COMPATIBILITY.json`, `VERSION`/`VERSIONS.txt` and checksums, and keep each
kit's source, archive and host identities distinct. The current SDK archives use game pack <!--rv:game_pack_version-->0.8.1<!--/rv-->,
which targets Star Citizen build <!--rv:game_build-->4.10.200.58746<!--/rv-->; see also the [SDK source context](../sdk/README.md) and
[game-pack status](game-pack.md).

- The profile determines which indexes exist. `core` and `core-source` have a workflow index but no developer
  catalogue and no game overlay (`game_overlay_version` is null); a workflow index records SDK/source
  coordinates, not a game-pack version. The game-capable `game-sc`, `legacy` and `host-source-sc` profiles
  include the developer catalogue, whose SDK/game-pack/source coordinates should match that profile's
  compatibility descriptor. A standalone `game-sc` overlay uses the workflow bundle from its exact matching
  core kit, and its `base_core_sha256` must match that core archive.
- In the repository source tree, `sdk/VERSION` and `sdk/DEVELOPMENT_VERSION` record the release and next
  development coordinates; neither is an extracted-kit value. Opt-in development packaging writes the
  development version to the kit's `VERSION` and metadata; `core-source` omits the game header.

### Option: a separately supplied source build

For a combined `legacy` plugin kit built outside the published release, get the archive, its expected SHA-256
and its source revision from whoever supplies it; a locally built archive is not a release asset. Use this
instead of the download above. On macOS or Linux, set `ARCHIVE` and `EXPECTED` (the recorded digest) and run:

```sh
python3 - "$ARCHIVE" "$EXPECTED" <<'PY'
import hashlib, pathlib, sys
actual = hashlib.sha256(pathlib.Path(sys.argv[1]).read_bytes()).hexdigest()
if actual.lower() != sys.argv[2].lower():
    raise SystemExit('SHA-256 mismatch; stop')
print('SHA-256 OK:', actual)
PY
```

On PowerShell, compare `(Get-FileHash $Archive -Algorithm SHA256).Hash` with the recorded digest and stop if
they differ. A matching digest identifies the bytes; it does not establish their approval or origin. Extract
into an empty directory, set `SDK` (or `$SDK`) to the folder containing `sdk-catalogue.json`, and check
`COMPATIBILITY.json`, `sdk-catalogue.json` and `sdk-workflows.json` against the supplied source revision and
each other. Record their digests and the archive's. For separate core and game-sc archives, verify each and
follow [archive composition](../sdk/README.md#zips) first; the example above is not that procedure. Then
continue at [Choose a profile](#choose-a-profile).

### Choose a profile

| Profile | Server flag | Tools |
| --- | --- | --- |
| **SDK developer profile (recommended default)** | none | 15 read-only tools |
| **Minimal profile** | `--minimal-tools` | 6 read-only catalogue tools |

The original six are `search_api`, `get_symbol`, `list_services`, `get_example`, `get_capability_status` and
`check_compatibility`. The default also offers `get_service_status`, which lists service tables and their
static capability/evidence rows (schema-2 catalogues bind those rows to the exact registry bytes; schema-1
catalogues report the status as unbound; it does not probe a host), and `list_engine_natives`,
`get_engine_native`, `validate_manifest_and_widgets`, `generate_plugin_scaffold`, `search_guides`,
`get_guide_section`, and, when a valid workflow index is loaded, `list_workflows` and `get_workflow`. The legacy `--extended-tools` and `--workflow-tools`
flags are accepted compatibility aliases; neither is needed for current-source defaults. Do not combine them
with `--minimal-tools`.

The [SDK tutorial](sdk-mcp-tutorial.md) uses the default profile. Call `list_workflows {}` and start with
`sco-plan-sdk-work` to connect the task to the current roadmap and public SDK before choosing an
implementation workflow.

- Both profiles run the kit's own `tools/sco-core-mcp/server.py` on your machine and read data files under
  `--repo-root`. The developer profile adds the scaffold tool, which runs the kit's `tools/sco-new-plugin.py`
  inside the server process on the first scaffold call. The guide tools only read the kit's `sdk-guides.json`,
  so a kit without it gets a tool error from them. Use the developer profile on a kit you downloaded and
  verified.
- Where your client has a tool allowlist, list the exact names of the profile you chose. Never use a wildcard.
  A name left out of an allowlist hides that tool from the agent.
- `--repo-root` is optional. Without it `get_example` returns the listed example paths only (`paths_only:
  true`, no file contents); engine natives, the scaffold, status evidence and release-approval records report
  `unavailable`; and stability labels are `unknown`. The guide tools still work when `sdk-guides.json` sits
  beside the catalogue. Leave it out only when you do not want example file contents to reach your model
  provider.
- Three build-and-check tools appear only when the operator configures a `--project-root`; see [development
  tools](#opt-in-development-tools).

### Register the server in your client

These rules apply to every client below.

- **Name.** Use `sco-core-sdk`. Some clients put the name in tool names; in Claude Code a permission rule
  reads `mcp__sco-core-sdk__search_api`.
- **Python launcher.** On macOS and Linux use `python3`, or an absolute interpreter path when a GUI client
  does not inherit your shell `PATH`. On Windows use `py` with `-3` as the first argument, or an absolute path
  to `python.exe`, the safer choice for a GUI client. Find it with
  `py -3 -c "import sys; print(sys.executable)"` (`python3 -c ...` on macOS and Linux); it prints something
  like `C:\Users\<you>\AppData\Local\Programs\Python\Python314\python.exe`. In a configuration file write
  it with forward slashes, as `command`, with no `-3` argument.
- **Arguments.** Every flag and every value is its own array element: `"--catalogue"`, then the path, never
  one `"--catalogue /path"` string. Every path is absolute, with forward slashes on Windows. In a
  configuration file, do not rely on the client's working directory, `~`, environment variables or shell
  quoting. In a shell command, quote each path (see [Claude Code](#claude-code)).
- **The arguments in order.** `SDK/tools/sco-core-mcp/server.py`, `--catalogue`, `SDK/sdk-catalogue.json`,
  `--repo-root`, `SDK` for the default developer profile (`SDK` is the SDK folder's absolute path). Append
  `--minimal-tools` for the six-tool minimal profile.
- **Replacing a registration.** After you move the SDK folder or change a path, remove the old entry and add
  it again; `claude mcp add` refuses a name that is already registered (`already exists in user config`).
  Claude Code: `claude mcp remove --scope user sco-core-sdk`, then the `claude mcp add` command. Codex: `codex
  mcp remove sco-core-sdk`, then the `codex mcp add` command. Any other client: remove the `sco-core-sdk`
  entry in its configuration file or MCP panel, then add it again with the new paths.

**What has been run.** `tools/sco-core-mcp/test_integration.py` starts the server with the arguments of the
JSON blocks on this page, using a kit-shaped folder it assembles from this repository; it has been run on
Windows only. On Windows, with a throwaway configuration folder, `claude mcp add --scope user` registered the
server (Claude Code 2.1.306), `claude mcp list` showed it connected, and `claude mcp remove` removed it;
`codex mcp add` and `codex mcp remove` (Codex CLI 0.118.0) wrote and removed the entry in Codex's
`config.toml`, but Codex did not start the server or call a tool. No agent called a tool through any client,
and none of the other client files below was run in its client: each follows that client's documented format.
The `python3` forms, and Ctrl+D as the way to stop the server, were not run on macOS or Linux.

#### Claude Code

User scope, so every project sees the server. Windows PowerShell:

```powershell
claude mcp add --scope user sco-core-sdk -- py -3 "$SDK/tools/sco-core-mcp/server.py" --catalogue "$SDK/sdk-catalogue.json" --repo-root "$SDK"
```

macOS and Linux:

```sh
claude mcp add --scope user sco-core-sdk -- python3 "$SDK/tools/sco-core-mcp/server.py" --catalogue "$SDK/sdk-catalogue.json" --repo-root "$SDK"
```

These commands use the `$SDK` you set in [Find the SDK folder](#find-the-sdk-folder), so run them in that
terminal session; Claude Code stores the absolute path your shell expands. Keep the double quotes: in a POSIX
shell, an unquoted `$SDK` whose path contains a space (such as `.../Jane Doe/...`) breaks into two arguments,
and the server fails with `can't open file`. PowerShell keeps `$SDK` whole, but quote it anyway, and always
quote a path you type out. In a new terminal, set `$SDK` again first (the line that sets it in the check block
above, run from the folder that holds `sdk-kit`), or write the absolute path out inside the quotes.

The `--` ends Claude Code's own options; everything after it is the server command. For project scope, save
the generic `mcpServers` object below as `.mcp.json` in the project root. Claude Code asks you to approve a
project-scoped server before it connects. The paths are absolute, so do not commit that file.

#### Generic `mcpServers` JSON

Clients that read an `mcpServers` object (Claude Code's `.mcp.json`, Cursor and others) take this entry. macOS
and Linux:

```json
{
  "mcpServers": {
    "sco-core-sdk": {
      "command": "python3",
      "args": [
        "/absolute/path/to/sco-sdk-<version>/tools/sco-core-mcp/server.py",
        "--catalogue",
        "/absolute/path/to/sco-sdk-<version>/sdk-catalogue.json",
        "--repo-root",
        "/absolute/path/to/sco-sdk-<version>"
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
        "C:/absolute/path/to/sco-sdk-<version>/tools/sco-core-mcp/server.py",
        "--catalogue",
        "C:/absolute/path/to/sco-sdk-<version>/sdk-catalogue.json",
        "--repo-root",
        "C:/absolute/path/to/sco-sdk-<version>"
      ]
    }
  }
}
```

Merge the `sco-core-sdk` entry into the file's existing `mcpServers` object. Do not paste a whole client
configuration into another client.

#### Codex

Either register it from a shell, or edit `~/.codex/config.toml` (on Windows, `.codex\config.toml` under your
user folder). The command, on macOS and Linux:

```sh
codex mcp add sco-core-sdk -- python3 "$SDK/tools/sco-core-mcp/server.py" --catalogue "$SDK/sdk-catalogue.json" --repo-root "$SDK"
```

On Windows PowerShell:

```powershell
codex mcp add sco-core-sdk -- py -3 "$SDK/tools/sco-core-mcp/server.py" --catalogue "$SDK/sdk-catalogue.json" --repo-root "$SDK"
```

Both use the same `$SDK` and the same quotes as the Claude Code commands. The equivalent `config.toml` table:

```toml
[mcp_servers.sco-core-sdk]
command = "python3"
args = [
  "/absolute/path/to/sco-sdk-<version>/tools/sco-core-mcp/server.py",
  "--catalogue",
  "/absolute/path/to/sco-sdk-<version>/sdk-catalogue.json",
  "--repo-root",
  "/absolute/path/to/sco-sdk-<version>"
]
```

On Windows set `command = "py"` and put `"-3"` first in `args`, with `C:/` paths.

#### Cursor

Cursor reads the same `mcpServers` object as above, from `.cursor/mcp.json` in a project or
`~/.cursor/mcp.json` for every project. It is a GUI client, so on Windows use the `py` form, or an absolute
`python.exe` as `command` with no `-3` argument.

#### VS Code

VS Code reads `.vscode/mcp.json`, and its top-level key is `servers`, not `mcpServers`. It also wants `"type":
"stdio"`:

```json
{
  "servers": {
    "sco-core-sdk": {
      "type": "stdio",
      "command": "python3",
      "args": [
        "/absolute/path/to/sco-sdk-<version>/tools/sco-core-mcp/server.py",
        "--catalogue",
        "/absolute/path/to/sco-sdk-<version>/sdk-catalogue.json",
        "--repo-root",
        "/absolute/path/to/sco-sdk-<version>"
      ]
    }
  }
}
```

On Windows use `"command": "py"`, `"-3"` first in `args` and `C:/` paths. Review the tools VS Code lists and
enable the profile's tools. A workspace file with absolute paths belongs on your machine only.

#### Copilot CLI

Copilot CLI supports `/mcp add` and a persistent `~/.copilot/mcp-config.json` (or the configured
`COPILOT_HOME`). See [GitHub's CLI MCP
guide](https://docs.github.com/en/copilot/how-tos/copilot-cli/customize-copilot/add-mcp-servers). Merge this
`mcpServers` entry with the existing configuration, on **the machine launching the server**:

```json
{
  "mcpServers": {
    "sco-core-sdk": {
      "type": "stdio",
      "command": "python3",
      "args": [
        "/absolute/path/to/sco-sdk-<version>/tools/sco-core-mcp/server.py",
        "--catalogue",
        "/absolute/path/to/sco-sdk-<version>/sdk-catalogue.json",
        "--repo-root",
        "/absolute/path/to/sco-sdk-<version>"
      ],
      "tools": [
        "search_api",
        "get_symbol",
        "list_services",
        "get_example",
        "get_capability_status",
        "check_compatibility",
        "get_service_status",
        "list_engine_natives",
        "get_engine_native",
        "validate_manifest_and_widgets",
        "generate_plugin_scaffold",
        "search_guides",
        "get_guide_section",
        "list_workflows",
        "get_workflow"
      ]
    }
  }
}
```

For the minimal profile, append `--minimal-tools` and retain only the original six names in `tools`. On
Windows use `"command": "py"` and `"-3"` first in `args`.

A session-specific file can be passed with `--additional-mcp-config @/absolute/path/to/mcp.json`. A repository
`.github/mcp.json` is an optional maintainer choice; Copilot CLI can discover
project-level `.mcp.json` and `.github/mcp.json` files, so review the linked guide's trust and precedence rules
first. Repository settings are separate: see [the maintainer-owned repository
setting](#copilot-code-review-maintainer-owned-repository-setting).

### Verify the connection

1. Restart the client, or reload its MCP servers.
2. Check that the server is connected. In Claude Code, `claude mcp list` or `/mcp` shows `sco-core-sdk` as
   connected; approve a project-scoped server first. In other clients, open the MCP or tools panel and look
   for `sco-core-sdk` running.
3. Check `tools/list`: fifteen tools for the default developer profile with the kit's workflow index (thirteen
   without it), six for explicit `--minimal-tools`,
   all with `readOnlyHint: true`. Ask the agent to list the server's tools if your client has no tools view.
   Older kits list different counts; see [Previous release kits](#previous-release-kits).
4. Ask the agent: "Call the `sco-core-sdk` tool `list_services` with no arguments and show me the raw
   `provenance` and `stability_context`." Most clients have no box for calling a tool by hand, and an agent
   tends to summarise away exactly these fields, so ask for the raw result. Compare `provenance` with the
   game-capable developer kit's `sdk-catalogue.json` and `COMPATIBILITY.json`: `provenance.sdk_version` and
   `provenance.game_pack_version` should match the catalogue, whose `sdk_version`, `game_pack_version` and
   `source_sha` should match the descriptor's `core_sdk_version`, `game_overlay_version` and applicable
   `source_commits`. Game-neutral `core` and `core-source` kits have no developer catalogue or game overlay, so
   this check does not apply to them. A mismatch means the server may be reading another catalogue, such as
   the site's changing mainline file.
5. Check `--repo-root` in the same result, because steps 1 to 4 pass even when it is wrong or missing.
   `stability_context.game_state` must be `read`, and no service may show `stability` `unknown` (the `game.*`
   services are `stable` or `experimental`). `game_state` `unavailable` with every `game.*` service `unknown`
   means `--repo-root` is missing or is not the SDK folder; set it to the SDK folder. With the developer
   profile, also ask: "Call `list_engine_natives` with `limit` 1 and show me the raw result." Its `state` must
   be `available`.
6. If the client only says that the server failed, run the exact command in a terminal. A healthy server
   prints nothing and waits for MCP messages on stdin. A wrong path prints one line and exits; a malformed
   argument prints a usage block (see the table below). To stop a healthy server, end its input: Ctrl+Z then
   Enter on Windows (exit code 0, no message), Ctrl+D on macOS and Linux, or close the terminal. On Windows,
   Ctrl+C alone does not stop it: the server waits in a console read and exits only after you also press
   Enter, with a `KeyboardInterrupt` traceback.

Common failures:

| What you see | Cause and fix |
| --- | --- |
| `python.exe: can't open file '.../tools/sco-core-mcp/server.py': [Errno 2] No such file or directory` (on macOS and Linux, `python3: can't open file ...`), exit code 2, and the client shows the server as failed | Python could not find `server.py`, so the path to it is wrong. The path is the extract folder instead of `sco-sdk-<version>`, a path has a typo, the `/absolute/path/to/...` placeholder was left in, or a path with a space was not quoted in a shell command. Run the existence check from [Find the SDK folder](#find-the-sdk-folder). |
| `sco-core-mcp: catalogue refused: FileNotFoundError`, exit code 1, and the client shows the server as failed or disconnected | `server.py` started, but the `--catalogue` file does not exist: for example a checkout's `dist/sdk-catalogue.json` path in a kit, or the extract folder in place of `sco-sdk-<version>`. Run the same existence check. |
| The server connects, but `get_example` returns `Example unavailable: requires a contained regular UTF-8 file without symlinks`; `list_engine_natives` or `generate_plugin_scaffold` return `unavailable` with a `reason` such as `sdk/native_index.json unavailable beneath --repo-root`; `list_services` shows `stability_context.game_state` `unavailable` | `--repo-root` is not the SDK folder: it is one level too high, another folder, or left out (left out, `get_example` instead returns paths only with `paths_only: true`). The server starts anyway. The messages use checkout paths (`sdk/...`); a kit has no `sdk/` folder, and those files sit at its root (`native_index.json`, `tools/sco-new-plugin.py`), so nothing is missing from the kit. Set `--repo-root` to the SDK folder and restart; step 5 above checks it. |
| A usage block that ends with `server.py: error: unrecognized arguments: ...`, `server.py: error: --catalogue or SCO_CORE_CATALOGUE is required` or `server.py: error: argument --repo-root: expected one argument`, exit code 2 | An argument is malformed: a flag and its value were joined in one `args` element (`"--catalogue C:/..."`), `--catalogue` is missing, or a flag has no value. Make every flag and every value its own element, in the order under [Register the server in your client](#register-the-server-in-your-client). |
| `Unknown tool` (JSON-RPC `-32602`), or only six tools | Check whether `--minimal-tools` or the client allowlist restricts discovery, and whether this is an older published kit. The SDK <!--rv:sdk_version-->2.4.1<!--/rv--> kit defaults to fifteen; for older kits see [Previous release kits](#previous-release-kits). |
| The server does not start on Windows, or the Microsoft Store opens | A standard Windows Python install provides `py` (and usually `python`) but not `python3`, and a `python3` alias that does exist can be the Microsoft Store stub. Use `py` with `-3`, or an absolute `python.exe` (`py -3 -c "import sys; print(sys.executable)"` prints its path). |
| The client disconnects while initializing | The client may support neither protocol version the server speaks, `2025-11-25` and `2026-07-28`. Use a client that supports one of them. An archive from before 2.4.1 may speak only `2025-11-25`. |
| `check_compatibility` says a `tag` does not match the advertised pattern | The current historical-record lookup accepts `sdk-vMAJOR.MINOR.PATCH` only, so pre-release suffixes are refused. Compare `sdk_version`, `game_pack_version` and `source_sha`, and verify the archive digest separately; these checks do not prove publication. |
| `search_guides` text says a release candidate is the latest release or that a final release is pending | An older kit contains guide snapshots frozen before its release page existed (see [Previous release kits](#previous-release-kits)). The release page of the kit you downloaded and its checksums establish release identity. |

### Workflow discovery and missing indexes

`list_workflows` takes no arguments. `get_workflow` requires `id` and accepts an optional `path` returned for
that workflow; with no path it reads its SKILL.md. Read only the relevant references from the returned
resource list. Results carry catalogue provenance and separate workflow bundle/index/file hashes, and the index
must match the catalogue's SDK/source coordinates. Tool arguments cannot read arbitrary files.

Both tools are listed only when a valid index is loaded. Without one they are not listed, and a call to either
name is refused as an unknown tool. Place the matching `sdk-workflows.json` beside the catalogue or select
it with operator flag `--workflows`. A missing default index is silent. A selected missing file, or a present
malformed, corrupted or source-mismatched index (including one beside the catalogue), prints a warning on stderr
and disables only these two tools; the rest of the server starts normally.

### The default developer tools

`search_guides` answers "how do I ..." questions from the SDK guides, not only the contract catalogue. It
takes a `query` and optionally one guide path as `doc`, a `limit` and a `cursor`, and returns ranked sections
with a bounded snippet, the guide path, heading, GitHub-style `anchor` and `url` (a docs-site link, or a
repository path for a guide under `sdk/docs`). `get_guide_section` returns one section's bounded plain text by
`doc` and `anchor`. The sections come from `sdk-guides.json` beside the catalogue (the kit ships it), or are
built in memory from `--repo-root`; with neither, the tools return a tool error. The guides are the
allowlisted public docs and the SDK's shipped `sdk/docs` guides. Guide prose is not a contract record or evidence that anything ran in game: confirm a method with `get_symbol`.

`generate_plugin_scaffold` takes an `id`, a `lang` (`c`, `cpp`, `csharp` or `lua`), an optional `with_menu`
and an optional `services` list. It returns the files `sco-new-plugin.py` would generate (`tools/` in a kit,
`sdk/tools/` in a checkout) as relative path to text, and writes and builds nothing: save them into an empty
folder and build with the SDK yourself. Without `--repo-root` it reports `unavailable`; with a root, the
server runs that root's scaffolder in its own process, so grant only a kit or checkout you have reviewed. The
[server README](../tools/sco-core-mcp/README.md#extended-tools-default) gives the schemas and limits.

### Local development tools

<a id="opt-in-development-tools"></a>

On a developer's own machine, add one or more `--project-root /absolute/path/to/projects` entries to the
server's `args`; it then offers three more tools for the build-and-check loop: `run_plugin_check`,
`build_plugin_project` and `test_plugin_project`. Where your client has an allowlist, add each name to `tools`.
In `args`, every flag and every value is its own array element; `"--project-root /abs"` as one element is
refused at start. Append these elements to the existing `args` and `tools` of the `sco-core-sdk` entry above;
do not add a second server:

```json
{
  "args": ["--project-root", "/absolute/path/to/projects",
           "--plugin-check", "/absolute/path/to/sco-plugin-check"],
  "tools": ["run_plugin_check", "build_plugin_project", "test_plugin_project"]
}
```

On Windows the checker must be an `.exe` (for example
`C:/absolute/path/to/checker-build/Release/sco-plugin-check.exe`) or a `.py` wrapper, and a Visual Studio
generator needs `"--cmake-generator"`, `"Visual Studio 17 2022"`, `"--cmake-platform"` and `"x64"` as four
more elements. The [SDK tutorial](sdk-mcp-tutorial.md#optional-let-the-agent-build-and-check) shows how to
build the checker.

`run_plugin_check` takes a built plugin `folder` and an optional `mode` (`full` or `short`); it returns the
exit code, `passed`, the `FAIL` lines and the summary line. `build_plugin_project` and `test_plugin_project`
take a `project` folder holding a `CMakeLists.txt`: the first builds it into the project's own `build`
subfolder, the second runs `ctest` there. The caller supplies only paths. `--enable-dev-tools` remains a
compatibility flag and still requires a project root. The operator fixes the programs (`--plugin-check`,
`--cmake`, `--ctest`), generator and platform (`--cmake-generator`, `--cmake-platform`), SDK folder
(`--sco-sdk`), timeout (`--dev-timeout`) and output cap (`--dev-output-limit`). Programs must be an `.exe` (or
a native program elsewhere) or a `.py` wrapper; batch files are refused. Paths outside every project root are
refused after resolving symlinks and `..`. A timeout kills the whole process tree.

These tools run code: CMake runs the project's build files, and the checker loads the plugin's native code
into a stand-in host. They are not a sandbox, so grant only project roots whose code you would build yourself,
and keep project-root configuration local. They advertise `readOnlyHint: false`, so Copilot code review
excludes them; never add them to a shared repository configuration or a hosted runner. A passing check says
nothing about the game. The
[server README](../tools/sco-core-mcp/README.md#development-tools-opt-in) gives the exact command lines, result
fields and security model.

### Previous release kits

<a id="published-stable-sdk-240-release"></a>

Published archives keep their own tool sets and bytes. Download and verify an older kit as above, with its tag
and archive name. The 2.4.0 [release notes](releases/sdk-v2.4.0.md#stable-sdk-240-archive-identities) record
every 2.4.0 profile identity.

- **SDK 2.4.0** uses game pack 0.8.0. It has the fifteen-tool default and a schema-2 catalogue, including
  `get_service_status`. Its server speaks protocol `2025-11-25` only.

The 2.4.0 kit ships bundled guides (`sdk-guides.json`, served by `search_guides` and
`get_guide_section`) and READMEs frozen before their release pages existed, so they can say "a release candidate is the latest
release" or "the final 2.4.0 release is pending". That describes the source snapshot, not release status; use
the release page and its `SHA256SUMS`. Their guide links are out of date too: the `url` in a `search_guides`
result for a `docs/` page names the previous docs-site address (`scubamount.github.io`), which now returns
404. Replace that host with `scuba-sc.github.io` and keep the path. The docs site publishes release notes for
2.4.0 and 2.4.1 only, so a result for the 2.1.0, 2.2.0 or 2.3.0 notes still gives a 404: read those under
`docs/releases/` in the repository. For a guide under `sdk/docs` the `url` is a repository path, not a link;
in the kit that guide is under `docs/` (see [Find the SDK folder](#find-the-sdk-folder)).

## Other MCP servers for the same work

This server answers questions about the SDK. These MCP servers cover the steps around it; each is set up from
its own page.

| Server | Use it to | Setup |
| --- | --- | --- |
| sc-offline | Look at an sc-offline install: status, installed plugins, log tails, settings. Read-only; it runs no command | [sc-offline AI agents guide](https://github.com/scuba-sc/sc-offline/blob/main/docs/ai-agents.md) (`sc-offline.exe mcp`, from the sc-offline release that adds it) |
| scuba-engine | Load a plugin in the sandbox host with a synthetic game, invoke its commands and widgets, and set up mock state, without Star Citizen | [scuba-engine-mcp](https://github.com/scuba-sc/scuba-engine/tree/main/tools/scuba-engine-mcp) |
| reference launcher | Read a launcher folder: status, plugin list, settings, redacted log. Read-only | [Build a launcher](building-a-launcher.md#5-connect-an-ai-agent-through-mcp) |

Results from any of them are observations of that host or sandbox, not in-game evidence.

## Maintainers: source checkout and your own catalogue

A source checkout has no kit layout: the catalogue is a file you build (`dist/sdk-catalogue.json`), the SDK
files sit under `sdk/`, and Clang is required. Outside developers should use the kit above. From the
intended, reviewed checkout, with Python 3.9 or newer and **Clang** capable of compiling and running x64 C11
probes (an arbitrary C11 compiler is not sufficient):

```sh
python3 tools/sdk_catalogue.py build --out dist/sdk-catalogue.json --source-sha "$(git rev-parse HEAD)"
```

In Windows PowerShell, with a Clang on `PATH` that targets x64:

```powershell
py -3 tools/sdk_catalogue.py build --out dist/sdk-catalogue.json --source-sha (git rev-parse HEAD)
```

The tool uses `CLANG` if set, otherwise `clang` on `PATH`; pass `--clang /absolute/path/to/clang` to override
either. Use source bytes matching that full commit; a supplied SHA does not certify a clean checkout.
Generation compiles and runs probes, separately from discovery. Build the catalogue **before** registering the
server, on the machine that launches it. After contracts or mappings change, rebuild it and restart the
client; the server does not rebuild it.

The guide tools need no generated file in a checkout: without `sdk-guides.json` beside the catalogue they
build sections from `--repo-root` on first use. To pin them to a commit instead, build the file beside the
catalogue (standard library only, no compiler):

```sh
python3 tools/sdk_guides.py build --out dist/sdk-guides.json --source-sha "$(git rev-parse HEAD)"
```

A built file wins over `--repo-root`: after editing a guide, rebuild it (or delete it) and restart the client,
or the server keeps serving the old sections.

This is the checkout form of the minimal profile. Replace the placeholder with the checkout's absolute path on
**that machine**; on Windows use `"py"` with `"-3"` first and `C:/` paths.

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
        "/absolute/path/to/sco-core",
        "--minimal-tools"
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

For the default profile in your own checkout, remove `"--minimal-tools"` from `args` and list all fifteen
names in `tools`. Build the workflow index beside the catalogue with `python3 tools/sdk_workflows.py --root .
--out dist/sdk-workflows.json`. Without a valid index the two workflow tools are not listed (thirteen tools
instead of fifteen). An explicit missing index, or any malformed/hash-inconsistent/source-mismatched
index that is present, prints a warning on stderr and disables only those two tools. The arguments and client-file rules above apply; only the paths
differ.

## Copilot code review: maintainer-owned repository setting

A repository administrator may paste the checkout JSON above into **Settings > Copilot > MCP servers > MCP
configuration**, shared by Copilot cloud agent and code review. See
[GitHub's repository MCP guide](https://docs.github.com/en/copilot/how-tos/copilot-on-github/customize-copilot/configure-mcp-servers).
Use the minimal profile there: a hosted runner should run nothing from the checkout beyond the server itself
(no scaffolder, no builds), so pass `--minimal-tools` and leave out project roots and development tools.

Before saving, prepare Python 3.9 or newer, Clang capable of compiling and running x64 C11 probes, the trusted
checkout and `dist/sdk-catalogue.json` in the **actual Copilot runner**, and use its absolute paths in the
JSON. Set `CLANG` or pass `--clang` when building there if Clang is not available as `clang` on `PATH`. A
catalogue built elsewhere does not exist in that runner. GitHub's
[setup workflow guide](https://docs.github.com/en/copilot/how-tos/copilot-on-github/customize-copilot/customize-cloud-agent/customize-the-agent-environment)
describes the `copilot-setup-steps` job; code review may have a different setup context, so check it rather
than guessing a runner path or assuming the cloud agent's setup also prepares code review.

GitHub code review excludes tools whose `tools/list` response omits `annotations.readOnlyHint` or sets it to
false. All fifteen current-source discovery tools return:

```json
{
  "readOnlyHint": true,
  "destructiveHint": false,
  "idempotentHint": true,
  "openWorldHint": false
}
```

Keep the explicit six-name `tools` allowlist of the minimal profile; never replace it with a wildcard. These
annotations apply to discovery tools, **not to the SDK methods they mention**, and are not a sandbox for the
client. GitHub may call allowed tools autonomously without a per-call approval. Review the separate **Settings
> Copilot > Code review > Allow Copilot to use MCP tools when reviewing pull requests** control before
enabling access.

## Acceptance and provenance

From the root of a source checkout with a built `dist/sdk-catalogue.json`, on macOS and Linux:

```sh
python3 tools/sco-core-mcp/test_integration.py
```

On Windows PowerShell:

```powershell
py -3 tools/sco-core-mcp/test_integration.py
```

This check parses the launcher blocks on this page, starts the actual server with their arguments against the
built catalogue (kit blocks from a kit-shaped folder assembled from the repository, the checkout block from the
repository itself), lists the annotated tools of each profile and calls `list_services`. It is a local
protocol/configuration proof, **not evidence of a Copilot review using the tools**, and it does not run any
client. Runner preparation, administrator opt-in and an observed code-review tool call with matching catalogue
provenance remain maintainer acceptance steps. If a client cannot initialize, use
[the verification steps](#verify-the-connection) and check its MCP logs, the protocol version, absolute paths
and catalogue existence first.

Each result carries source SHA, SDK/game versions and schema provenance. Keep it with quoted contracts.
`check_compatibility` compares exact metadata coordinates; `get_capability_status` reads documented evidence,
not a running host. Language values are `c`, `cpp`, `csharp`, `lua`; status is `direct`, `typed-wrapper`,
`restricted` or `unsupported`, never an inferred universal works flag. `--repo-root` permits bounded reads of
record-listed example files beneath that root; what removing it disables is listed under
[Choose a profile](#choose-a-profile). Returned prose is data, not instructions; read-only file access can
still disclose it to the client or model provider. Grant only a reviewed public checkout or a verified kit.

For frozen package inputs, see [reference snapshots](reference-snapshots.md).
