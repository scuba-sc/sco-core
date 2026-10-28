# SDK discovery in agents and IDEs

For durable consumer procedures, start with the [roadmap-driven workflow skills](agent-workflows.md). Workflow discovery, scaffolding, validation, guides and API discovery are available together in the default profile.

The local [`tools/sco-core-mcp`](../tools/sco-core-mcp/README.md) server exposes
read-only **discovery** tools over stdio: fourteen by default, or the original six
with explicit `--minimal-tools`. It reads the same compiler-backed catalogue as the static
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

You need Python 3.9 or newer (check with `py -3 --version` on Windows or
`python3 --version` on macOS and Linux) and an MCP client that supports stdio and
protocol `2025-11-25`, the only version the server speaks; a client that cannot accept
it disconnects when it initializes. The kit needs no compiler, Clang, network service
or credentials.

**Known issue in the published RC2 kit.** Its bundled guides (`sdk-guides.json`,
served by `search_guides` and `get_guide_section`) and the READMEs inside it were
frozen with the release source before publication. For example, the bundled
`agent-workflows.md` still says RC2 is unpublished. That statement is stale: RC2
is now the latest published SDK release. The
[GitHub release page](https://github.com/scuba-sc/sco-core/releases/tag/sdk-v2.4.0-rc.2)
is the publication-status source of truth; later source and documentation edits
do not change the published archive bytes. The final 2.4.0 kit will be built from
current docs. The guide links are out of date
too: for a `docs/` page, the `url` in a `search_guides` result names the previous
docs-site address (`scubamount.github.io`), which now returns 404. Replace that host
with `scuba-sc.github.io` and keep the path. The docs site publishes release notes for
2.4.0 only, so a result for the 2.1.0, 2.2.0 or 2.3.0 notes still gives a 404 on the new
host: read those under `docs/releases/` in the repository. For a guide
under `sdk/docs` the `url` is a repository path, not a link; in the kit that guide is under
`docs/` (see [Find the SDK folder](#find-the-sdk-folder)). See the
[release notes](releases/sdk-v2.4.0.md#known-issues-in-release-candidate-1).

**Published RC2 and historical RC1.** RC2 was published on 2026-10-28 and is the
latest release. Its tag points to source commit
identified in the [release notes](releases/sdk-v2.4.0.md); later `main` commits
do not change the release archive bytes. RC1 is historical. Keep each selected kit's source
and archive identity distinct.

**Published RC2 and historical RC1 behavior.** The published RC2 kit has the
fourteen-tool default and matching workflow bundle documented on this page. The
older RC1 archive keeps its original six-tool default and twelve tools with
`--extended-tools`; it has no workflow index or workflow tools. When using that
unchanged historical archive, append `--extended-tools` to the developer launchers
below and expect twelve tools. Current `main` can contain later source-only changes;
do not imply they are in the published RC2 bytes.

### Optional separately supplied source build

For a combined `legacy` plugin kit built outside the published release, obtain the archive, its expected
SHA-256 and source revision from the person or process supplying that kit. This is
a separate option from the published RC2 download below; a locally built archive is
not a release asset. Verify the digest before extracting into an empty directory.
For example, on macOS or Linux, set `ARCHIVE` to its absolute path and `EXPECTED`
to the separately recorded digest, then run:

```sh
python3 - "$ARCHIVE" "$EXPECTED" <<'PY'
import hashlib, pathlib, sys
actual = hashlib.sha256(pathlib.Path(sys.argv[1]).read_bytes()).hexdigest()
if actual.lower() != sys.argv[2].lower():
    raise SystemExit('SHA-256 mismatch; stop')
print('SHA-256 OK:', actual)
PY
```

On PowerShell, compare `(Get-FileHash $Archive -Algorithm SHA256).Hash` with the
separately recorded digest and stop if they differ. A matching digest identifies
the bytes; it does not establish their approval or origin. Extract the verified
archive into an empty directory, set `SDK` (or `$SDK` in PowerShell) to the absolute
path of the extracted folder containing `sdk-catalogue.json`, and check
`COMPATIBILITY.json`, `sdk-catalogue.json`, and `sdk-workflows.json` against the
supplied source revision and each other. Record the archive, catalogue and workflow
index digests. For separate core and game-sc archives, verify each archive and
follow the exact-base and metadata checks in [archive composition](../sdk/README.md#zips)
before composing them; the single-archive example above is not that procedure.
Then skip the RC1-specific download and extraction below and continue at
[Choose a profile](#choose-a-profile). The server checks catalogue/index coordinates
and indexed content on startup; negotiate the available tools after connecting.

### Published RC2 release (current)

Download the combined kit and its `SHA256SUMS` from the latest published
[RC2 release](https://github.com/scuba-sc/sco-core/releases/tag/sdk-v2.4.0-rc.2)
into an empty directory. Set `EXPECTED_SHA256` (or `$ExpectedSha256` in
PowerShell) to the independently recorded combined-archive digest in the
[RC2 release notes](releases/sdk-v2.4.0.md#published-rc2-archive-identity).
Compare that value with the downloaded archive and checksum file before extraction.

macOS and Linux (on macOS use `shasum -a 256 -c -`):

```sh
set -e
: "${EXPECTED_SHA256:?Set EXPECTED_SHA256 from the RC2 release notes}"
gh release download sdk-v2.4.0-rc.2 -R scuba-sc/sco-core -p 'sco-sdk-2.4.0.zip' -p SHA256SUMS
grep ' sco-sdk-2.4.0.zip$' SHA256SUMS | sha256sum -c -
python3 - "$EXPECTED_SHA256" <<'PY'
import hashlib, pathlib, sys
if hashlib.sha256(pathlib.Path('sco-sdk-2.4.0.zip').read_bytes()).hexdigest().lower() != sys.argv[1].lower():
    raise SystemExit('Expected SHA-256 mismatch; stop')
PY
mkdir sdk-kit && unzip -q sco-sdk-2.4.0.zip -d sdk-kit
SDK="$PWD/sdk-kit/sco-sdk-2.4.0"
test -f "$SDK/sdk-catalogue.json"
```

Windows PowerShell:

```powershell
if (!$ExpectedSha256) { throw 'Set ExpectedSha256 from the RC2 release notes' }
gh release download sdk-v2.4.0-rc.2 -R scuba-sc/sco-core -p 'sco-sdk-2.4.0.zip' -p SHA256SUMS
if ($LASTEXITCODE -ne 0) { throw 'RC2 download failed' }
$expected = ((Get-Content SHA256SUMS | Where-Object { $_ -match ' sco-sdk-2\.4\.0\.zip$' }) -split '\s+')[0]
if (!$expected -or !$ExpectedSha256 -or (Get-FileHash sco-sdk-2.4.0.zip -Algorithm SHA256).Hash -ine $expected -or $expected -ine $ExpectedSha256) { throw 'SHA256 mismatch; stop' }
Expand-Archive sco-sdk-2.4.0.zip -DestinationPath sdk-kit
$SDK = (Join-Path (Resolve-Path sdk-kit).Path 'sco-sdk-2.4.0') -replace '\\', '/'
if (!(Test-Path "$SDK/sdk-catalogue.json")) { throw 'Not the SDK folder' }
```

Then continue at [Choose a profile](#choose-a-profile). Confirm the extracted
compatibility, catalogue and workflow index identities as described above.

### Historical RC1 option: download and verify the kit

Download `sco-sdk-2.4.0.zip` and `SHA256SUMS` from the
[`sdk-v2.4.0-rc.1`](https://github.com/scuba-sc/sco-core/releases/tag/sdk-v2.4.0-rc.1)
release into a new, empty folder that you will keep, such as `~/sco-sdk` or
`C:/sco-sdk`, and run every command below from that folder. Use this combined kit, not
`sco-sdk-core-2.4.0.zip`: the core archive has no catalogue, no guides and no server.
(The `game-sc` overlay composed over its exact core archive also carries them; see
[archive composition](../sdk/README.md#zips).)

The `gh release download` lines need the GitHub CLI (`gh`), signed in with
`gh auth login`. Without it, download both files in a browser from the release page, or
from
`https://github.com/scuba-sc/sco-core/releases/download/sdk-v2.4.0-rc.1/sco-sdk-2.4.0.zip`
and `.../SHA256SUMS`, then run the lines after the `gh` line.

macOS and Linux (on macOS use `shasum -a 256 -c -` for the check):

```sh
gh release download sdk-v2.4.0-rc.1 -R scuba-sc/sco-core -p 'sco-sdk-2.4.0.zip' -p SHA256SUMS
grep ' sco-sdk-2.4.0.zip$' SHA256SUMS | sha256sum -c -
```

Windows PowerShell:

```powershell
gh release download sdk-v2.4.0-rc.1 -R scuba-sc/sco-core -p 'sco-sdk-2.4.0.zip' -p SHA256SUMS
$expected = ((Get-Content SHA256SUMS | Where-Object { $_ -match ' sco-sdk-2\.4\.0\.zip$' }) -split '\s+')[0]
$actual = (Get-FileHash sco-sdk-2.4.0.zip -Algorithm SHA256).Hash.ToLower()
if ($actual -ieq $expected) { "SHA256 OK: $actual" } else { "SHA256 MISMATCH: stop ($actual)" }
```

Expect `sco-sdk-2.4.0.zip: OK` from `sha256sum` (or `shasum`) and `SHA256 OK` from
PowerShell. (`Get-FileHash` prints upper-case hex; the script lowers it to match
`SHA256SUMS` and the asset table, and the comparison ignores case anyway.) Stop on a
mismatch and download both files again. A checksum file
downloaded beside its archive only catches a damaged download, so also compare the
digest with the one in the
[historical RC1 asset table](getting-started.md#historical-sdk-240-release-candidate-1-assets): PowerShell prints
it, and `sha256sum sco-sdk-2.4.0.zip` (`shasum -a 256` on macOS) prints it for a shell.
What a checksum downloaded beside its archive does and does not establish is described
in [Download, verify, extract](getting-started.md#3-download-verify-extract).

### Find the SDK folder

The **SDK folder** is the folder that directly contains `sdk-catalogue.json`. For the
combined kit that is `<extract folder>/sco-sdk-2.4.0`. This page writes its path as
`SDK`, and as `$SDK` in a shell. Extract next to the zip, in the folder you will keep:
the client stores the absolute path, so if you move or delete the folder later, replace
the registration (see "Replacing a registration" under
[Register the server in your client](#register-the-server-in-your-client)). Then check the
path. Some extraction tools add one more folder level,
so check rather than assume.

macOS and Linux:

```sh
mkdir sdk-kit && unzip -q sco-sdk-2.4.0.zip -d sdk-kit
SDK="$PWD/sdk-kit/sco-sdk-2.4.0"
test -f "$SDK/sdk-catalogue.json" && echo "SDK ok: $SDK" || echo "not the SDK folder: $SDK"
```

Windows PowerShell:

```powershell
Expand-Archive sco-sdk-2.4.0.zip -DestinationPath sdk-kit
$SDK = (Join-Path (Resolve-Path sdk-kit).Path 'sco-sdk-2.4.0') -replace '\\', '/'
if (Test-Path "$SDK/sdk-catalogue.json") { "SDK ok: $SDK" } else { "not the SDK folder: $SDK" }
```

`Expand-Archive` stops with `Failed to create file ... already exists` when `sdk-kit`
already holds the kit's files, for example if you run this step a second time. An existing
empty folder is fine. Extract into a new folder name instead, or add `-Force`, which
overwrites files of the same name with the zip's copies (keep nothing you edited in that
folder) and leaves any other files alone.

If it says `not the SDK folder`, find the real one. On macOS and Linux run
`find sdk-kit -name sdk-catalogue.json`; in PowerShell run
`Get-ChildItem sdk-kit -Recurse -Filter sdk-catalogue.json`. The folder that holds the
file is the SDK folder: set `$SDK` to its absolute path (forward slashes on Windows) and
run the check again.

The SDK folder holds `sdk-catalogue.json`, `sdk-guides.json`, `native_index.json`,
`status_registry.json`, `COMPATIBILITY.json`, `VERSION` and
`tools/sco-core-mcp/server.py`. The kit has no `sdk/` and no `dist/` folder, so the
paths that tools return, which use the source-checkout layout and start with `sdk/`,
lose that prefix here: `sdk/docs/plugin-ini.md` is `docs/plugin-ini.md` and
`sdk/examples/widgets/widgets.c` is `examples/widgets/widgets.c`, both under the SDK
folder.
Client configuration files do not expand `$SDK`, so write the absolute path out there;
the `claude mcp add` and `codex mcp add` commands below can use `$SDK` because your
shell expands it. The configuration examples use `/absolute/path/to/sco-sdk-2.4.0`
(macOS and Linux) and `C:/absolute/path/to/sco-sdk-2.4.0` (Windows) for it.

### Choose a profile

Current source offers these profiles:

| Profile | Server flag | Tools |
| --- | --- | --- |
| **SDK developer profile (recommended default)** | none | 14 read-only tools |
| **Minimal profile** | `--minimal-tools` | 6 read-only catalogue tools |

The original six are `search_api`, `get_symbol`, `list_services`, `get_example`,
`get_capability_status` and `check_compatibility`. The default also offers
`list_engine_natives`, `get_engine_native`, `validate_manifest_and_widgets`,
`generate_plugin_scaffold`, `search_guides`, `get_guide_section`, `list_workflows`
and `get_workflow`. The legacy `--extended-tools` and `--workflow-tools` flags are
accepted compatibility aliases; neither is needed for current-source defaults.
Do not combine them with `--minimal-tools`.

The [SDK tutorial](sdk-mcp-tutorial.md) uses the comprehensive default. Call
`list_workflows {}` and start with `sco-plan-sdk-work` to connect the task to the
current roadmap and public SDK before choosing an implementation workflow.

- Both profiles run the kit's own `tools/sco-core-mcp/server.py`, a Python program, on
  your machine, and both read data files under `--repo-root`. What differs is which tools
  the server offers. The developer profile adds the scaffold tool, which runs the kit's
  `tools/sco-new-plugin.py` inside the server process on the first scaffold call; the
  minimal profile runs no other code from the SDK folder. The guide tools only read the
  kit's `sdk-guides.json`; a checkout's `tools/sdk_guides.py` is not in the kit, so a kit
  without `sdk-guides.json` gets a tool error from them. Use the developer profile on a
  kit you downloaded from the release and verified. Neither profile compiles or builds
  anything; running builds and checks requires operator-configured project roots for the
  [development tools](#opt-in-development-tools).
- Where your client has a tool allowlist, list the exact names of the profile you
  chose. Never use a wildcard. A name left out of an allowlist hides that tool from
  the agent.
- `--repo-root` is optional. Without it `get_example` returns the listed example
  paths only (`paths_only: true`, no file contents); engine natives, the scaffold,
  status evidence and release-approval records report `unavailable`; and stability
  labels are `unknown`. The guide tools still work when `sdk-guides.json` sits beside
  the catalogue. Leave `--repo-root` out only when you do not want example file
  contents to reach your model provider.
- Three build-and-check tools are offered automatically when the operator configures
  a `--project-root` ([development tools](#opt-in-development-tools)). Without a root
  they are absent. Keep project-root execution configuration local.

### Register the server in your client

These rules apply to every client below.

- **Name.** Use `sco-core-sdk`. Some clients put the name in tool names; in Claude
  Code a permission rule reads `mcp__sco-core-sdk__search_api`.
- **Python launcher.** On macOS and Linux use `python3`, or an absolute
  interpreter path when a GUI client does not inherit your shell `PATH`. On
  Windows use `py` with `-3` as the first argument, or an absolute path to
  `python.exe`, which is the safer choice for a GUI client. To find that path, run
  `py -3 -c "import sys; print(sys.executable)"` in PowerShell (`python3 -c ...` on
  macOS and Linux). It prints something like
  `C:\Users\<you>\AppData\Local\Programs\Python\Python314\python.exe`; in a
  configuration file write it with forward slashes, as `command`, with no `-3`
  argument. A standard Windows
  Python install provides `py` (and usually `python`) but not `python3`, and a
  `python3` alias that does exist can be the Microsoft Store stub, which opens the
  Store instead of starting Python.
- **Arguments.** Every flag and every value is its own array element:
  `"--catalogue"`, then the path, never one `"--catalogue /path"` string. Every
  path is absolute, with forward slashes on Windows. In a configuration file, do not
  rely on the client's working directory, `~`, environment variables or shell quoting.
  In a shell command, quote each path (see [Claude Code](#claude-code)).
- **The arguments in order.** `SDK/tools/sco-core-mcp/server.py`, `--catalogue`,
  `SDK/sdk-catalogue.json`, `--repo-root`, `SDK` for the default developer
  profile (`SDK` is the SDK folder's absolute path). Append `--minimal-tools` for
  the six-tool minimal profile.
- **Replacing a registration.** After you move the SDK folder or change a path, remove the
  old entry and add it again; `claude mcp add` refuses a name that is already registered
  (`already exists in user config`). Claude Code:
  `claude mcp remove --scope user sco-core-sdk`, then the `claude mcp add` command. Codex:
  `codex mcp remove sco-core-sdk`, then the `codex mcp add` command. Any other client:
  remove the `sco-core-sdk` entry in its configuration file or MCP panel, then add it
  again with the new paths.

**Historical RC1 verification.** The RC1 server command was run on Windows only,
with `py -3`: `--extended-tools` served twelve tools, and no flag served six. The
tool calls tried returned results. That observation does not verify the new
source defaults or workflow index. Not run in game.
`tools/sco-core-mcp/test_integration.py` starts the server with the arguments of
the JSON blocks on this page, using a kit-shaped folder it assembles from this
repository; it has been run on Windows only. On Windows, with a throwaway
configuration folder, `claude mcp add --scope user` registered the server (Claude Code
2.1.306), `claude mcp list` showed it connected, and `claude mcp remove` removed it;
`codex mcp add` and `codex mcp remove` (Codex CLI 0.118.0) wrote and removed the entry in
Codex's `config.toml`, but Codex did not start the server or call a tool. No agent called a
tool through any client, and none of the other client files below was run in its client:
each follows that client's documented format. The `python3` forms, and Ctrl+D as the way
to stop the server, were not run on macOS or Linux.

#### Claude Code

User scope, so every project sees the server. Windows PowerShell:

```powershell
claude mcp add --scope user sco-core-sdk -- py -3 "$SDK/tools/sco-core-mcp/server.py" --catalogue "$SDK/sdk-catalogue.json" --repo-root "$SDK"
```

macOS and Linux:

```sh
claude mcp add --scope user sco-core-sdk -- python3 "$SDK/tools/sco-core-mcp/server.py" --catalogue "$SDK/sdk-catalogue.json" --repo-root "$SDK"
```

These commands use the `$SDK` you set in [Find the SDK folder](#find-the-sdk-folder), so
run them in that terminal session. Your shell expands it, and Claude Code stores the
absolute path. Keep the double quotes. In a POSIX shell, an unquoted `$SDK` whose
path contains a space (such as `.../Jane Doe/...`) breaks into two arguments, and the
server fails with `can't open file`; PowerShell keeps `$SDK` whole, but quote it
anyway, and always quote a path you type out. In a new terminal, set `$SDK` again
first (the line that sets it in the check block above, run from the folder that holds
`sdk-kit`), or write the absolute path out inside the quotes.

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
        "/absolute/path/to/sco-sdk-2.4.0"
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
        "C:/absolute/path/to/sco-sdk-2.4.0"
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
codex mcp add sco-core-sdk -- python3 "$SDK/tools/sco-core-mcp/server.py" --catalogue "$SDK/sdk-catalogue.json" --repo-root "$SDK"
```

On Windows PowerShell:

```powershell
codex mcp add sco-core-sdk -- py -3 "$SDK/tools/sco-core-mcp/server.py" --catalogue "$SDK/sdk-catalogue.json" --repo-root "$SDK"
```

Both use the same `$SDK` and the same quotes as the Claude Code commands.

The equivalent `config.toml` table:

```toml
[mcp_servers.sco-core-sdk]
command = "python3"
args = [
  "/absolute/path/to/sco-sdk-2.4.0/tools/sco-core-mcp/server.py",
  "--catalogue",
  "/absolute/path/to/sco-sdk-2.4.0/sdk-catalogue.json",
  "--repo-root",
  "/absolute/path/to/sco-sdk-2.4.0"
]
```

On Windows set `command = "py"` and put `"-3"` first in `args`, with `C:/` paths.

#### Cursor

Cursor reads the same `mcpServers` object as above, from `.cursor/mcp.json` in a
project or `~/.cursor/mcp.json` for every project. Cursor is a GUI client, so on
Windows use the `py` form above, or an absolute `python.exe` as `command` with no
`-3` argument (`py -3 -c "import sys; print(sys.executable)"` prints its path).

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
        "/absolute/path/to/sco-sdk-2.4.0"
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
        "/absolute/path/to/sco-sdk-2.4.0"
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
        "get_guide_section",
        "list_workflows",
        "get_workflow"
      ]
    }
  }
}
```

For the minimal profile, append `--minimal-tools` and retain only the original
six names in `tools`. On Windows use `"command": "py"` and `"-3"` first in `args`.

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
3. Check `tools/list`: fourteen tools for the current-source default developer
   profile, six for explicit `--minimal-tools`, all with `readOnlyHint: true`. The
   unchanged published RC1 developer profile has twelve instead. Ask the agent to list the server's tools
   if your client has no tools view.
4. Ask the agent: "Call the `sco-core-sdk` tool `list_services` with no arguments and
   show me the raw `provenance` and `stability_context`." Most clients have no box for
   calling a tool by hand, and an agent tends to summarise away exactly these fields,
   so ask for the raw result. Every result carries `provenance`. Compare it with
   `COMPATIBILITY.json` in the SDK folder: `sdk_version` must equal `core_sdk_version`
   (2.4.0 for this kit), `game_pack_version` must equal `game_overlay_version`
   (0.8.0), and `source_sha` must equal the `core` and `game-sc` values under
   `source_commits` (the same commit in this kit). A different value means the server
   is reading another catalogue, such as the site's changing mainline file.
5. Check `--repo-root` in the same result, because steps 1 to 4 pass even when it is
   wrong or missing: the server starts, lists its tools and reports the right
   provenance. `stability_context.game_state` must be `read`, and no service may show
   `stability` `unknown` (the `game.*` services are `stable` or `experimental`).
   `game_state` `unavailable` with every `game.*` service `unknown` means `--repo-root`
   is missing or is not the SDK folder; set it to the SDK folder. With the developer
   profile, also ask: "Call `list_engine_natives` with `limit` 1 and show me the raw
   result." Its `state` must be `available`.
6. If the client only says that the server failed, run the exact command in a
   terminal. A healthy server prints nothing and waits for MCP messages on stdin.
   A wrong path prints one line and exits; a malformed argument prints a usage
   block (see the table below). To stop a healthy server, end its input: on Windows
   press Ctrl+Z and then Enter (it exits with code 0 and no message), on macOS and
   Linux press Ctrl+D, or close the terminal window. On Windows, Ctrl+C alone does not
   stop it: the server is waiting in a console read that Ctrl+C does not interrupt, and
   it exits only after you also press Enter, with a `KeyboardInterrupt` traceback.

Common failures:

| What you see | Cause and fix |
| --- | --- |
| `python.exe: can't open file '.../tools/sco-core-mcp/server.py': [Errno 2] No such file or directory` (on macOS and Linux, `python3: can't open file ...`), exit code 2, and the client shows the server as failed | Python could not find `server.py`, so the path to it is wrong. The path is the extract folder instead of `sco-sdk-2.4.0`, a path has a typo, the `/absolute/path/to/...` placeholder was left in, or a path with a space was not quoted in a shell command. Run the existence check from [Find the SDK folder](#find-the-sdk-folder). |
| `sco-core-mcp: catalogue refused: FileNotFoundError`, exit code 1, and the client shows the server as failed or disconnected | `server.py` started, but the `--catalogue` file does not exist: for example a checkout's `dist/sdk-catalogue.json` path in a kit, or the extract folder in place of `sco-sdk-2.4.0`. Run the same existence check. |
| The server connects, but `get_example` returns `Example unavailable: requires a contained regular UTF-8 file without symlinks`; `list_engine_natives` or `generate_plugin_scaffold` return `unavailable` with a `reason` such as `sdk/native_index.json unavailable beneath --repo-root`; `list_services` shows `stability_context.game_state` `unavailable` | `--repo-root` is not the SDK folder: it is one level too high, another folder, or left out (left out, `get_example` instead returns paths only with `paths_only: true`). The server starts anyway. The messages use checkout paths (`sdk/...`); a kit has no `sdk/` folder, and those files sit at its root (`native_index.json`, `tools/sco-new-plugin.py`), so nothing is missing from the kit. Set `--repo-root` to the SDK folder and restart; step 5 above checks it. |
| A usage block that ends with `server.py: error: unrecognized arguments: ...`, `server.py: error: --catalogue or SCO_CORE_CATALOGUE is required` or `server.py: error: argument --repo-root: expected one argument`, exit code 2 | An argument is malformed: a flag and its value were joined in one `args` element (`"--catalogue C:/..."`), `--catalogue` is missing, or a flag has no value. Make every flag and every value its own element, in the order under [Register the server in your client](#register-the-server-in-your-client). |
| `Unknown tool` (JSON-RPC `-32602`), or only six tools | Check whether `--minimal-tools` or the client allowlist restricts discovery, and whether this is the older published RC1 server. Current source defaults to fourteen; grant the fourteen exact names for that profile. RC1 needs `--extended-tools` for its twelve supported names and has no workflow tools. |
| The server does not start on Windows, or the Microsoft Store opens | `python3` is not a standard Windows command. Use `py` with `-3`, or an absolute `python.exe` (`py -3 -c "import sys; print(sys.executable)"` prints its path). |
| The client disconnects while initializing | The client does not support protocol `2025-11-25`. Update the client. |
| `check_compatibility` says a `tag` does not match the advertised pattern | The current historical-record lookup accepts `sdk-vMAJOR.MINOR.PATCH` only, so RC1 and RC2 suffixes are refused. Compare `sdk_version`, `game_pack_version` and `source_sha`, and verify the archive digest separately; these checks do not prove publication. |
| `search_guides` text says RC2 is not published | The immutable published RC2 kit contains a prepublication guide snapshot. This source guide corrects that text; the [GitHub release page](https://github.com/scuba-sc/sco-core/releases/tag/sdk-v2.4.0-rc.2) is the publication-status source of truth. |

### Workflow discovery and missing indexes

`list_workflows` takes no arguments. `get_workflow` requires `id` and accepts an
optional `path` returned for that workflow; with no path it reads its SKILL.md.
Read only the relevant references from the returned resource list. Results carry
catalogue provenance and separate workflow bundle/index/file hashes. The index
must match the catalogue's SDK/source coordinates. Tool arguments cannot read
arbitrary files.

Current source lists these two tools even when no default index is present;
calling them then returns the precise `Workflow index unavailable` tool error.
Place the matching `sdk-workflows.json` beside the catalogue or select it with
operator flag `--workflows`. A selected missing file, or a present malformed,
corrupted or source-mismatched index, refuses startup. This includes an invalid
index found beside the catalogue; it is not silently bypassed.

### The default developer tools

`search_guides` answers "how do I ..." questions from the SDK guides, not only
the contract catalogue. It takes a `query` and optionally one guide path as
`doc`, a `limit` and a `cursor`, and returns ranked sections with a bounded
snippet, the guide path, heading, GitHub-style `anchor` and `url` (a docs-site link, or
a repository path for a guide under `sdk/docs`).
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
[server README](../tools/sco-core-mcp/README.md#extended-tools-default) gives the
schemas and limits.

### Local development tools

<a id="opt-in-development-tools"></a>

On a developer's own machine, configure one or more
`--project-root /absolute/path/to/projects` entries in the server's `args`; it then
automatically offers three more tools for the build-and-check loop: `run_plugin_check`,
`build_plugin_project` and `test_plugin_project`. Where your client has an
allowlist, add each name you want to `tools` explicitly. In `args`, every flag and
every value is its own array element; `"--project-root /abs"` as one element is
refused at start. Append these elements to the existing `args` and `tools` of the
`sco-core-sdk` entry above; do not add a second server:

```json
{
  "args": ["--project-root", "/absolute/path/to/projects",
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

`--enable-dev-tools` remains a compatibility flag and still requires at least one
project root; current source needs no separate enable flag once a root is configured.
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

The default guide tools need no generated file in a checkout: without `sdk-guides.json`
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

For the comprehensive default in your own checkout, remove `"--minimal-tools"`
from `args` and list all fourteen names in `tools`. Build the workflow index beside
the catalogue with `python3 tools/sdk_workflows.py --root . --out dist/sdk-workflows.json`.
A missing default index leaves the two workflow names listed but calls report
`Workflow index unavailable`; an explicit missing index, or any malformed/hash-
inconsistent/source-mismatched index that is present, refuses startup. The
arguments and client-file rules above apply; only the paths differ.

## Copilot code review: maintainer-owned repository setting

A repository administrator may prepare the checkout JSON above in **Settings >
Copilot > MCP servers > MCP configuration**. That setting is shared by Copilot
cloud agent and code review. This document is a configuration snippet, **not an
applied repository setting**. No settings or setup workflows are changed here.
See [GitHub's repository MCP guide](https://docs.github.com/en/copilot/how-tos/copilot-on-github/customize-copilot/configure-mcp-servers).
Use the minimal profile there: a hosted runner should run nothing from the
checkout beyond the server itself (no scaffolder, no builds), so pass
`--minimal-tools` and leave out project roots and development tools.

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
`annotations.readOnlyHint` or sets it to false. All fourteen discovery tools return:

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

From the root of a source checkout with a built `dist/sdk-catalogue.json`, on macOS and
Linux:

```sh
python3 tools/sco-core-mcp/test_integration.py
```

On Windows PowerShell:

```powershell
py -3 tools/sco-core-mcp/test_integration.py
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
