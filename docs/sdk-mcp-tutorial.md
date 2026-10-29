# SDK → MCP → tutorial-owned plugin

## Standalone developer-kit workflow

**SDK 2.4.0 RC2** is the published release for this workflow. Download the matching
developer kit from the [RC2 release page](https://github.com/scuba-sc/sco-core/releases/tag/sdk-v2.4.0-rc.2)
and verify its digest against that release's `SHA256SUMS`; a tag or candidate artifact
alone does not establish publication.
The combined kit, composed
core-plus-game overlay and host-source kit supply the discovery launcher,
`sdk-catalogue.json` and `sdk-guides.json`. This is packaging behavior, not a
gameplay acceptance claim. Core-only profiles do not contain the game catalogue.

For the published RC1 route, verify the combined kit (`sco-sdk-2.4.0.zip`)
against RC1 `SHA256SUMS` and retain its compatibility inventory. RC1 provides six
default discovery tools, or twelve with `--extended-tools`; it does not provide
the workflow bundle described below.

The complete workflow uses the published RC2 combined kit, which includes the
matching catalogue and workflow index. Verify the archive against RC2 `SHA256SUMS`
and extract it to an empty directory. For a source checkout, build matching
metadata as described in the
[discovery README](../tools/sco-core-mcp/README.md) and
[workflow guide](agent-workflows.md#archive-and-source-discovery). A local build
is not the published release asset or permission to distribute it. Do not pair
RC1 archive bytes with the site's changing mainline catalogue or a different index.

Prerequisites for discovery: Python 3.9 or newer and a stdio MCP client supporting
protocol `2025-11-25`. Building a scaffold additionally needs CMake 3.20+ and a
64-bit C/C++ compiler (for a `csharp` scaffold, the .NET 8 SDK and the NativeAOT
toolchain; a `lua` scaffold builds only `sco-plugin-check`). Clang is required to produce a source catalogue, not to
consume a bundled one.

### Connect the client

Follow [Set up the discovery server from the SDK kit](agent-integration.md#set-up-the-discovery-server-from-the-sdk-kit),
using the published RC2 combined kit for the steps below. Set
`SDK_ROOT` to the folder that directly contains its `sdk-catalogue.json` and
`sdk-workflows.json`; register `sco-core-sdk` in your client and verify the
connection. Current source uses the **SDK developer profile**: fifteen read-only
tools by default. Published RC2 has fourteen; it predates `get_service_status` and
catalogue schema 2. Explicit `--minimal-tools` retains the six catalogue tools and
leaves out scaffolding, validation, native/guide and workflow discovery. The
legacy `--extended-tools` and `--workflow-tools` aliases are no longer needed.
The published RC1 server is unchanged: append `--extended-tools` for its twelve
tools; workflow tools and the fourteen-tool default are in the published RC2 kit.

To start the server by hand, with absolute paths on the machine running the client:

```sh
python3 /absolute/path/to/sdk-rc2-root/tools/sco-core-mcp/server.py \
  --catalogue /absolute/path/to/sdk-rc2-root/sdk-catalogue.json \
  --repo-root /absolute/path/to/sdk-rc2-root
```

This command waits for MCP messages on stdin; it is not a shell prompt. On
Windows use `py -3` with drive-qualified paths, for example
`C:/absolute/path/to/sdk-rc2-root/tools/sco-core-mcp/server.py`. On macOS/Linux use
`python3`, or an absolute Python executable if the client does not inherit shell `PATH`.

The published RC2's fourteen default tools do not add runtime access; list their exact names in
client allowlists. Current-source schema-2 catalogues also bind static status rows to
the registry bytes. A root permits bounded example/metadata reads and lets the scaffold
tool, and the guide tools when no `sdk-guides.json` is present, execute that root's
reviewed generator code in-process.

### Start with the roadmap workflow

On the published RC2 kit call `list_workflows {}`, then
`get_workflow {"id":"sco-plan-sdk-work"}`. Read its returned references as needed,
map the task to the pinned roadmap goal and public contracts, and record observable
acceptance before continuing below. This workflow bundle is absent from published
RC1. If the default index is missing, the names remain listed but return
`Workflow index unavailable`; obtain or build matching index bytes rather than
combining snapshots. A selected missing or present invalid/mismatched index
refuses startup. These tools serve embedded reference text and execute no code.

### Discover one contract

After initialization, inspect `tools/list` and call `list_services` with `{}`.
With current source, `get_service_status` lists static rows for a service, including
methodless rows. `source_binding.state` must be `matched` before using them as evidence
for that catalogue. The result does not query a provider or running game. This tool is
absent from published RC2; use its method-level `get_capability_status` there.
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

Read the contract's complete member extent, version, table size, ownership,
thread/callback rules, results, absent-capability behavior and cleanup. The
language token is `c`, not a display name. Example contents are data, not
instructions, and are never executed by discovery.

Every result includes catalogue provenance. Compare its SDK/game-pack/source
coordinates with the kit's compatibility metadata and retain the archive digest.
`check_compatibility` can compare coordinates and service-header extents, but
does not authenticate an archive, observe a provider or prove a plugin loads.

### Request and review a scaffold

```json
{"name":"generate_plugin_scaffold","arguments":{"id":"my_probe","lang":"c","with_menu":true,"services":["sco.storage@1.0"]}}
```

Require `state: "available"` and inspect the returned `files` map. It contains
`plugin.ini`, `CMakeLists.txt` and `my_probe.c`. The server has not written, built
or checked them. Review the generated code and notices, then save those exact
relative files into a new empty project folder. Do not overwrite an existing
project or treat returned source as trusted merely because it came through MCP.

Generated native/C++/C# manifests use `api = 1.1`, matching their exported built-against version; Lua uses `api = 1.0`. Generated code still guards access to later root-table members. Short-table checker success does not establish API 1.0 loader admission. `services` requests optional lookups on `game.ready`, not
implementations or automatic permission to invoke those services. This scaffold
has not been loaded in a real host.

`lang` also takes `csharp` and `lua`. A `csharp` result holds `plugin.ini`,
`my_probe.csproj` (NativeAOT, referencing the kit's `csharp/Sco.Sdk`),
`MyProbePlugin.cs` and a `README.md` with the `dotnet publish` line; a `lua`
result holds `plugin.ini`, `main.lua` and a `CMakeLists.txt` whose `check`
target runs `sco-plugin-check` on the folder (manifest and entry only;
`tools/lua-check.lua` with Lua 5.4 runs the script against a stand-in). Lua scripts have no service lookup, so a `lua` request with
`services` other than `sco.widgets` (with `with_menu`) is refused with the
generator's reason.

### Validate manifest text and widget invariants

Validate the returned manifest text with `folder: "my_probe"` before building.
Here is a complete minimal manifest/widget example using the same ID:

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

Require `valid: true` and inspect both manifest and widget findings. A valid
manifest is not evidence that its entry exists, its requirements have providers
or its module is safe. Widget validation does not inspect native pointers,
panel stride/size or renderer behavior. To exercise a negative, reuse the same
node ID twice and confirm validation findings rather than treating the result
as valid.

Native-index discovery is optional:

```json
{"name":"list_engine_natives","arguments":{"limit":1}}
```

If available, pass one returned entry's `id` as `get_engine_native.id_or_hash`;
preserve its evidence tier. Follow `next_cursor` without inventing cursor values.
These are symbolic signature-row names, not addresses, resolved runtime
functions or proof of in-game execution.

### Ask the guides how to do something

The contract records say what a method guarantees; the guides say how to use
it. Search the guide sections with a plain question:

```json
{"name":"search_guides","arguments":{"query":"How do I deploy a plugin on Windows?","limit":3}}
```

Each hit names its guide `doc`, `heading`, `anchor` and `url` (a docs-site link, or a
repository path for a guide under `sdk/docs`), with a short snippet. Read one hit in
full by passing its `doc` and `anchor` back:

```json
{"name":"get_guide_section","arguments":{"doc":"docs/getting-started.md","anchor":"start-your-first-plugin"}}
```

Add `"doc"` to a search to stay within one guide, and follow `next_cursor` for
more hits. The kit's `sdk-guides.json` beside its catalogue answers these calls;
its `guides.source_sha` should equal the catalogue's. A section's text stops at
4000 characters (`truncated: true`); read the rest at its `url`. Guide prose is
not a contract record: confirm a method with `get_symbol` before relying on it.
Nothing in a guide is evidence that code ran in game. The guides inside the release
candidate 1 kit predate its publication and can say that 2.4.0 is not on the release
page, and their `url` values name a previous docs-site address that no longer opens
(use `scuba-sc.github.io` with the same path). The docs site supersedes them (see the
known issue in
[the setup section](agent-integration.md#set-up-the-discovery-server-from-the-sdk-kit)).

### Build and check outside MCP

With the returned files saved under `/absolute/project/my_probe`:

```sh
cmake -S /absolute/project/my_probe -B /absolute/project/my_probe/build \
  -DSCO_SDK=/absolute/path/to/updated-sdk-root
cmake --build /absolute/project/my_probe/build --config Debug --target check
```

On Windows use a compiler developer shell, drive-qualified paths and `-A x64`
for a fresh Visual Studio configuration. Check the exit status after every
command. For source-checkout SDK helpers, set `SCO_SDK` to the checkout's `sdk`
directory; source-layout kit helpers are likewise under `sdk`.

The generated `check` target builds and invokes `sco-plugin-check` with both
full and short root tables. Native code is executed by that checker; it is not
a sandbox. The SDK helper intentionally names development modules `.dll` even
on macOS/Linux, but only a Windows-built module is suitable for the Windows
game host. A filename extension does not convert a POSIX binary into a game DLL.

A successful check is synthetic host/ABI evidence, not game-service or gameplay
proof. Discovery never compiles, writes files, invokes commands or reloads
plugins. Runtime metadata inspection, if independently authorized, uses the
[separate default-off launcher](../tools/sco-core-mcp-runtime/README.md).

### Optional: let the agent build and check

The same loop can run through MCP on your own machine. Three development tools
are offered automatically when operator project roots are configured. First build the checker once, outside your project root (the
`check` target builds its own copy inside the project's build tree, but
`build_plugin_project` builds only the default targets):

```sh
cmake -S /absolute/path/to/updated-sdk-root -B /absolute/checker-build
cmake --build /absolute/checker-build --config Release --target sco-plugin-check
```

The program is `/absolute/checker-build/sco-plugin-check`, or
`/absolute/checker-build/Release/sco-plugin-check.exe` with a Visual Studio
generator. Then start the server with a `--project-root` that holds your project
folder and that checker. No separate enable flag is needed in current source;
`--enable-dev-tools` remains compatible and still requires a root. On Windows with Visual Studio 2022,
add a generator and platform as well:

```sh
python3 /absolute/path/to/updated-sdk-root/tools/sco-core-mcp/server.py \
  --catalogue /absolute/path/to/updated-sdk-root/sdk-catalogue.json \
  --repo-root /absolute/path/to/updated-sdk-root \
  --project-root /absolute/project \
  --plugin-check /absolute/checker-build/sco-plugin-check \
  --cmake-generator "Visual Studio 17 2022" --cmake-platform x64
```

In a client configuration each flag and each value is its own `args` element, and
`Visual Studio 17 2022` is one element without the shell quotes. On Windows use
`py` with `-3` first and an `.exe` checker, for example
`C:/absolute/checker-build/Release/sco-plugin-check.exe`. The
[integration guide](agent-integration.md#opt-in-development-tools) shows the elements.

The SDK folder comes from `--repo-root` (or set `--sco-sdk`). Without
`--plugin-check`, the server looks for `sco-plugin-check` in that SDK folder's
`bin/` or `out/bin/`. Only a full SDK build followed by
`cmake --install <build> --config Release --prefix out` in the SDK folder puts it
there, as the [SDK README](../sdk/README.md) describes. Name an `.exe` or a `.py`
wrapper; batch files are refused. Then send:

```json
{"name":"build_plugin_project","arguments":{"project":"my_probe"}}
```

```json
{"name":"run_plugin_check","arguments":{"folder":"my_probe/build/check/my_probe"}}
```

```json
{"name":"run_plugin_check","arguments":{"folder":"my_probe/build/check/my_probe","mode":"short"}}
```

Relative paths start at the first project root. Require `passed: true` on each
result. The build result has a `configure` and a `build` step, each with its exit
code and the tail of its output. A check result lists the checker's `FAIL` lines
as `findings` and its last line as `summary`. Those two checks repeat what the
generated `check` target does. If your project registers CTest tests,
`test_plugin_project` with the same `project` runs them; with no tests it fails
on purpose.

The caller supplies only the folder and the mode; the operator's command line
fixes everything else, and paths outside the project roots are refused. The
build and the checker run your project's code on this machine, and a timeout
kills the whole process tree. The
[server README](../tools/sco-core-mcp/README.md#development-tools-opt-in)
describes the security model. A passing check here is the same synthetic
evidence as above: **not run in game**.

### Installation and lifecycle limits

Installation remains an explicit owner action through the product's documented
plugin folder/manifest flow. No MCP tool performs it. Do not replace a mapped
Windows DLL; unload through a supported host route or close the game first.

Reload is not universal: it can cascade to loaded dependents, refuses built-in
or data members and handed-out service providers, and is unavailable for a
restart-required faulted provider. Manifest changes such as `requires`, `entry`
and settings declarations need restart because reload does not rediscover the
manifest. DataCore changes can also require restart. See [plugin lifecycle and
hot reload](plugins.md#hot-reload); native plugins are not sandboxed and a fault
has no promised safe in-process recovery. **Not run in game.**

## Historical downstream NPC/inspector walkthrough

The following workflow uses a separate sc-offline checkout and downstream proof
helpers. It is not a prerequisite for the standalone kit workflow above. Its
commands and external fixtures require verification against that downstream
revision; the evidence ledger below does not certify the 2.4.0 release candidate archive path.

This command-first tutorial creates **your own** `tutorial_npc` and
`tutorial_inspector` folders from sc-offline's public-SDK consumers. These IDs do
not replace the shipped `npc`/`inspector`, add production built-ins, or alter the
13 baseline feature units (plus two separately counted optional bridges).

**Evidence ledger:** an earlier revision compiled the two tutorial-owned native
modules on macOS from an isolated, checksum-verified **local development SDK**
that was never a published release. Building them from the published archive that
sc-offline's proofs pin, the checker, dynamic fixtures, static MCP transcript,
stale/session negatives and Windows/MSVC/in-game install/reload are **NOT RUN**
for this tutorial revision. The commands below are reproducible acceptance steps,
not claimed results.

Prerequisites: Python 3.9 or newer, CMake 3.20+, C/C++20 compiler; Clang and an x64 C11
compilation environment for catalogue generation. Windows x64/MSVC builds the
actual game DLL; macOS/Linux modules are development artifacts only. Native
plugins execute trusted code; the checker is not a sandbox.

## 1. Discover the actual service contract (static, read-only)

Use a trusted sco-core checkout whose headers/contracts match the source SHA:

```sh
set -eu
CORE=/absolute/path/to/sco-core
cd "$CORE"
SHA=$(git rev-parse HEAD)
python3 tools/sdk_catalogue.py build --out dist/sdk-catalogue.json --source-sha "$SHA"
python3 tools/sco-core-mcp/tutorial.py --catalogue dist/sdk-catalogue.json \
  --source-sha "$SHA" --transcript /absolute/new/path/discovery.json
```

The transcript contains real requests/responses to the existing `server.py`
launcher over stdio, using protocol `2025-11-25`, without a runtime flag or
repository-content sharing. Pass condition: `static MCP tutorial: PASS`.
`check_compatibility` compares the catalogue with the checkout's own `sdk/VERSION`
and `SCO_GAME_PACK_VERSION` by default. To check the catalogue against the archive
you will build with (section 2), pass that archive's `VERSION` as `--sdk-version`
and its game-pack version as `--game-pack-version`; a mismatch means you should
build the catalogue from the checkout at the archive's release tag instead.
On Windows run the same two commands with `py -3` in place of `python3` and
`(git rev-parse HEAD)` in place of `"$SHA"`.
The supplied SHA must describe the catalogue's actual source, not unrelated
modified contracts. Rebuild the catalogue and restart your MCP client after
contract/header changes; the catalogue never tracks a running host.

The sequence is `tools/list` → `list_services` →
`search_api {"query":"spawn_npc","service":"game.actors","language":"cpp"}` →
`get_symbol` → `get_example` → `get_capability_status` → `check_compatibility`.
It reads these exact symbols:

- `sc_actors_v1.spawn_npc` and `.despawn`: owned NPC creation/removal;
- `sc_actors_v1.health` and `sc_entities_v1.get_transform`: inspector reads;
- `sc_spatial_v1.player_pose`: a fresh NPC placement pose.

Read the returned **signature, version macro, complete member extent, ownership,
thread affinity, result/absence rules, cleanup and language routes** before
writing a call. C++ route support does not imply unrestricted Lua mutation.
`get_example` lists shipped record-backed examples; it does not execute them.
The SDK version, plugin ABI, game-pack version and packed service versions are
separate coordinates; every result's `provenance` names the catalogue's SDK and
game-pack versions. Matching strings, resolved signatures and ON capabilities
are not game-session proof. See [contracts](contracts.md) and
[game-service status](game/status.md).

## 2. Obtain SDK and build your own copies in isolation

By default, the archive proofs use the SDK and digest pinned by the selected
sc-offline checkout. That pin can lag sco-core, so obtain its matching published
archive and `SHA256SUMS` from the
[releases page](https://github.com/scuba-sc/sco-core/releases); as of 2026-10-28
the product pin is RC1. Newer product checkers also accept an explicitly supplied
candidate with its source, profile and archive/checksum identities; inspect the
selected checker's `--help` and verify those inputs. Candidate preflight does not
change the product pin or establish a published release. The tutorial wrapper and
`verify_extract` calls below use the default published pin and do not accept those
candidate overrides. For candidate preflight, invoke a supported individual feature
checker separately with all its required identity flags and a matching host-source
fixture where required. Earlier SDK release downloads were withdrawn; their tags
alone do not supply archives.
Set absolute paths to the product source, downloaded archive/checksums and a new
work root **outside either checkout**. Do not use host/submodule include trees.

```sh
set -eu
PRODUCT_SOURCE=/absolute/path/to/sc-offline
SDK_NAME=$(python3 -c 'import sys; sys.path.insert(0, sys.argv[1]); import check_inspector_sdk as c; print(c.SDK_ROOT)' "$PRODUCT_SOURCE/tools")
echo "obtain $SDK_NAME.zip and SHA256SUMS for the selected product pin"
ARCHIVE=/absolute/path/to/$SDK_NAME.zip
SUMS=/absolute/path/to/SHA256SUMS
WORK=/absolute/new/path/tutorial-dev
python3 "$PRODUCT_SOURCE/tools/check_sdk_tutorial.py" --archive "$ARCHIVE" \
  --sums "$SUMS" --work "$WORK" --prepare-only
# Verify archive and extracted inventory using the existing SDK proof helper.
python3 -c 'import sys; from pathlib import Path; sys.path.insert(0,sys.argv[1]); from check_inspector_sdk import verify_extract; verify_extract(Path(sys.argv[2]),Path(sys.argv[3]),Path(sys.argv[4]))' \
  "$PRODUCT_SOURCE/tools" "$ARCHIVE" "$SUMS" "$WORK/sdk"
SDK="$WORK/sdk/$SDK_NAME"
cmake -S "$WORK/owned-source/plugins" -B "$WORK/plugin-build" \
  -DSCO_SDK_INCLUDE_DIR="$SDK/include" -DSCO_ADDITIONAL_FEATURES=OFF \
  -DSCO_WORKFLOW_TESTS=ON
cmake --build "$WORK/plugin-build" --config Release \
  --target sc-offline-tutorial_npc sc-offline-tutorial_inspector
```

Stop on a nonzero exit. Pass conditions: SHA256/extracted inventory verified,
then both named module targets built. The generated, editable sources are under
`owned-source/plugins/{tutorial_npc,tutorial_inspector}/`, with independent
manifest IDs, metadata, entries and command prefixes. The copy preserves
`spawn_npc` and other public SDK identifiers, license and capability strings;
it changes feature IDs, not ABI names. Only the extracted SDK supplies headers.

For Visual Studio use Developer PowerShell, translate `$WORK` etc. to PowerShell
variables, add `-A x64` to fresh CMake configurations, and check `$LASTEXITCODE`
after **every** Python/CMake/CTest/checker call. Both outputs are
`plugin-build/test-plugins/<id>/<id>.dll` on Windows; never rename a POSIX `.so`
to `.dll` and install it into the game.

### Checker + one named dynamic fixture (acceptance, NOT RUN here)

```sh
cmake --build "$WORK/plugin-build" --config Release --target tutorial_npc-module-test
ctest --test-dir "$WORK/plugin-build" -C Release \
  -R '^tutorial_npc-module-workflow$' --output-on-failure
cmake -S "$SDK" -B "$WORK/checker-build"
cmake --build "$WORK/checker-build" --config Release --target sco-plugin-check
mkdir -p "$WORK/check-plugins"
cp -R "$WORK/plugin-build/test-plugins/tutorial_npc" "$WORK/check-plugins/tutorial_npc"
# POSIX only: change entry in this separate checker copy, not the Windows manifest.
python3 -c 'import pathlib,re,sys; p=pathlib.Path(sys.argv[1]); m=[x for x in p.iterdir() if x.suffix in (".so",".dylib")]; assert len(m)==1; i=p/"plugin.ini"; i.write_text(re.sub(r"(?m)^entry[ \t]*=.*$","entry = "+m[0].name,i.read_text()))' "$WORK/check-plugins/tutorial_npc"
"$WORK/checker-build/sco-plugin-check" "$WORK/check-plugins/tutorial_npc" --invoke tutorial_npc.observe
```

On Windows skip POSIX manifest rewriting and use
`checker-build/Release/sco-plugin-check.exe`. Pass conditions: exactly the named
CTest passes, and the checker ends `OK: 0 failure(s)` with
`tutorial_npc.observe -> unavailable` under missing services. The fixture loads
the actual independent native module against **synthetic public providers**, not
the engine. Inspector's equivalent is target `tutorial_inspector-module-test`,
regex `^tutorial_inspector-module-workflow$`, folder `tutorial_inspector` and
command `tutorial_inspector.read` (missing readings remain unavailable/unknown).
These tests do not establish actual host owner-release behavior or game cleanup.

## 3. Owner installation and supported lifecycle (Windows, NOT VERIFIED)

With the game closed, back up any existing tutorial folders and copy only the
Windows module and its `plugin.ini` into
`data/plugins/{tutorial_npc,tutorial_inspector}/`. Keep `plugins = on` in
`sc-offline.ini` (the product default); remove a tutorial folder's `disabled`
marker only by explicit owner choice. Tutorial folders are not shipped defaults.
Use only the existing offline launcher, never official connectivity or EAC
manipulation. Unknown offline/admission state means no mutation.

Through the host's game-thread command registry, the owner invokes
`tutorial_npc.spawn <owner-chosen-archetype> 1`, `.observe`, `.remove <owned-id>`
and `.clear`; `tutorial_inspector.read` is read-only. `synthetic_actor` belongs
only to fixtures, not the game. Removal success means initiation, not disappearance;
`alive=0` is not death, removal or authority. Only actors-owned IDs may be removed
through `game.actors`. Missing services have no private fallback; maximum health
zero stays unknown.

For a normal command-only external consumer, `sco.reload tutorial_inspector` or
`sco.reload tutorial_npc` unloads/reloads through the existing host route. Check
command withdrawal/re-registration and NPC owner cleanup with the exact host/game
build. Rebuild/replace files only with the DLL unloaded or game closed. Folder
disable takes effect at next start; `plugins = off` blocks external features.
Queried native **providers** can be session-lifetime/restart-only; DataCore changes
need restart; built-ins cannot be hot-reloaded. A native fault is restart-required,
not a promise of safe reload or corruption recovery. No MCP tool performs reload,
command invocation or gameplay mutation.

## Optional runtime observations — default OFF, separate acceptance

The historical walkthrough above uses **only the six static discovery tools** (the
minimal profile when explicitly launched with `--minimal-tools`); current source offers
fifteen default discovery tools and published RC2 offers fourteen. Runtime MCP is an optional independently implemented/reviewed
feature under
[runtime MCP design](design/runtime-mcp.md); do not enable it to make the example
compile. The default launcher advertises no `runtime_*` names. Consult `tools/list`
and the separate charter before granting a runtime allowlist; never use `*`.

The tutorial script requests unsupported `runtime_get_host_info`, `invoke_command`
and `sco.reload`; default static stdio must refuse each with JSON-RPC `-32602`.
Its stale-age (>1000 ms), stale-status, changed-session and changed-epoch controls
consume the **existing synthetic schema vector**, not live runtime data. Those
negatives test the tutorial's refusal to call evidence current; they are not
runtime-server, authentication, audit or Windows acceptance. They are **NOT RUN**
in this revision. Future observations require independently authenticated current
session/epoch, fresh runtime provenance and no restart requirement. Catalogue SHA
is never a session token. Discard cached data on session change/unavailability;
unknown is not false, stale is not current. Never publish identifiers, addresses,
secrets, raw pointers or file paths in runtime snapshots.

Record archive digest, source SHA, host/plugin/game versions and acceptance results
separately in the product [status ledger](https://github.com/scuba-sc/sc-offline/blob/main/docs/status.md).
Only the owner decides gameplay acceptance and human release.
