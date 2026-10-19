# SDK → MCP → tutorial-owned plugin

## SDK 2.4 developer-kit path

Game developer distributions bundle `sdk-catalogue.json` and the stdio launcher
`tools/sco-core-mcp/server.py`. Use that catalogue with the same extracted kit;
it is compiled during packaging and covered by the archive checksum inventory.
The consumer does not need Clang or a source checkout to run discovery:

```sh
python /absolute/sdk/tools/sco-core-mcp/server.py \
  --catalogue /absolute/sdk/sdk-catalogue.json \
  --repo-root /absolute/sdk --extended-tools
```

Configure this command in an agent's stdio MCP client. Start with `list_services`,
then `search_api`, `get_symbol`, `get_capability_status` and `get_example`. Extended
tools add native-index queries, manifest/widget validation and plugin scaffolding.
`generate_plugin_scaffold` returns files; the client writes them into its own new
project, builds with `-DSCO_SDK=/absolute/sdk`, and runs the generated `check` target.
Discovery does not compile, write projects, invoke the game or certify availability.

Use scuba-engine's separate loopback MCP server to inspect and drive a plugin in
its synthetic host. Native engine availability and game-session evidence remain
separate from a successful scaffold/check/mock run. In particular stored-container
contents and live DataCore pools require native backends not supplied by this kit;
actor raw values do not define injury tiers. The existing contracts remain the
API authority; recipe/UI policies are composed by consumers rather than copied
into the frozen kernel ABI.

The longer source-based tutorial below remains useful for maintainers and prior
releases. Its evidence ledger does not certify this new archive path.

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

Prerequisites: Python 3, CMake 3.20+, C/C++20 compiler; Clang and an x64 C11
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

The archive proofs accept exactly one published SDK: the `SDK_ROOT` pinned in
sc-offline's `tools/check_inspector_sdk.py`, with its sha256. That pin can lag
the newest sco-core release, so read it from your sc-offline checkout rather than
from this page, then download that archive and its adjacent `SHA256SUMS` from the
matching human-published release on the
[releases page](https://github.com/scubamount/sco-core/releases).
Set absolute paths to the product source, downloaded archive/checksums and a new
work root **outside either checkout**. Do not use host/submodule include trees.

```sh
set -eu
PRODUCT_SOURCE=/absolute/path/to/sc-offline
SDK_NAME=$(python3 -c 'import sys; sys.path.insert(0, sys.argv[1]); import check_inspector_sdk as c; print(c.SDK_ROOT)' "$PRODUCT_SOURCE/tools")
echo "download $SDK_NAME.zip from release sdk-v${SDK_NAME#sco-sdk-}"
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

The tutorial requires **only the six static discovery tools**. Runtime MCP is an
optional independently implemented/reviewed feature under
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
separately in the product [status ledger](https://github.com/scubamount/sc-offline/blob/main/docs/status.md).
Only the owner decides gameplay acceptance and human release.
