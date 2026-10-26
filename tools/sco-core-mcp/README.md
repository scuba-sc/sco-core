# sco-core SDK discovery MCP server

[Consumer workflow discovery](../../docs/agent-workflows.md) is included in the default fourteen-tool profile. Start with `sco-plan-sdk-work` to tie the task to the roadmap and public SDK. It reads `sdk-workflows.json` beside the catalogue or an operator-selected `--workflows` file, requires matching SDK/source coordinates, and serves only embedded indexed text.

Development-only, read-only discovery of the public sco-core SDK contracts through
MCP over stdio. The server uses only the standard library of Python 3.9 or newer
and a generated SDK catalogue. It does **not** connect to a running host or game,
invoke SDK methods, execute examples, or modify the repository. It is bundled
development tooling, not a runtime game-service component. By default it compiles nothing; three
[development tools](#development-tools-opt-in) that build and check plugin
projects are offered automatically when the operator configures project roots.

[Separate default-off local runtime launcher](../sco-core-mcp-runtime/README.md)
implements the fixed runtime tool surface. This discovery executable remains
credentials-free, with fourteen read-only discovery tools by default. Explicit
`--minimal-tools` retains the original six catalogue tools. The default developer
profile includes [extended discovery](#extended-tools-default) and workflow retrieval.
The separate launcher supersedes the historical same-server placement in the
[runtime design](../../docs/design/runtime-mcp.md). Discovery never imports or
enables runtime inspection. No live runtime/game acceptance is claimed.

The contract authority is [`docs/contracts.md`](../../docs/contracts.md), with
per-method records in [`sdk/contracts/`](../../sdk/contracts/). The server exposes
that evidence; it does not replace the contract's reading rules or remaining gaps.

## Connect a client

To connect Claude Code, Codex, Cursor, VS Code or Copilot CLI to the SDK kit, follow
[Set up the discovery server from the SDK kit](../../docs/agent-integration.md#set-up-the-discovery-server-from-the-sdk-kit).
It downloads and verifies the kit, defines the SDK folder (the folder that directly contains
`sdk-catalogue.json`), gives each client's configuration and says how to verify the
connection. This README does not repeat those configurations. Two profiles are used
in current-source examples: **SDK developer profile (recommended default)** with
fourteen read-only tools, and **minimal profile** with explicit `--minimal-tools`
and six catalogue tools. The legacy `--extended-tools` and `--workflow-tools` flags
remain accepted aliases and are no longer needed; they cannot be combined with
`--minimal-tools`.

## Copyable SDK consumer tutorial

[SDK → MCP → tutorial-owned plugin](../../docs/sdk-mcp-tutorial.md) first walks the
standalone kit workflow with the SDK developer profile: scaffold, manifest and widget
validation, native index, guides, and an optional build and check. Its downstream
walkthrough then uses the six static tools, owned NPC/inspector copies, an isolated
archive build, the checker and a named dynamic fixture. Runtime observations remain
optional and default-off; static discovery never certifies gameplay or performs reload.

## Using an extracted SDK archive

**SDK 2.4.0 is published as release candidate 1**
([`sdk-v2.4.0-rc.1`](https://github.com/scuba-sc/sco-core/releases/tag/sdk-v2.4.0-rc.1), the latest
release; the final signed `sdk-v2.4.0` follows). It bundles this launcher, a
compiler-built `sdk-catalogue.json` and the matching guide sections
`sdk-guides.json` in the combined kit (`sco-sdk-2.4.0.zip`), the `game-sc` overlay
and the `host-source-sc` kit. The guides and READMEs inside the release candidate 1
kit predate its publication; see the known issue in the
[integration guide](../../docs/agent-integration.md#set-up-the-discovery-server-from-the-sdk-kit).

Use the combined kit directly, or compose the game overlay over the exact core
kit required by its compatibility metadata ([archive composition](../../sdk/README.md#zips)).
In this README the **SDK folder** is the folder
that directly contains `sdk-catalogue.json` (for the combined kit,
`<extract folder>/sco-sdk-2.4.0`). Set `--repo-root` to the SDK folder, not to the parent
extraction directory or an uncomposed overlay. The
`host-source-sc` archive retains repository layout. Core and core-source profiles
remain game-neutral and contain neither this game catalogue nor its launcher.

The published RC1 launcher retains its original six-tool default and twelve with
`--extended-tools`; it has no workflow tools/index. The fourteen-tool default
described here applies to updated source and independently pinned candidate kits
built from it, not to unchanged RC1 bytes. Append `--extended-tools` to a developer
launcher for RC1 compatibility and expect twelve. For an updated-source kit, this
command uses the default developer profile (append `--minimal-tools` for six):

```sh
python3 /absolute/path/to/sco-sdk-2.4.0/tools/sco-core-mcp/server.py \
  --catalogue /absolute/path/to/sco-sdk-2.4.0/sdk-catalogue.json \
  --repo-root /absolute/path/to/sco-sdk-2.4.0
```

On Windows use `py -3` instead of `python3`, with drive-qualified paths such as
`C:/absolute/path/to/sco-sdk-2.4.0`. Default discovery includes scaffold, native,
manifest/widget, guide and workflow tools. Guide tools read `sdk-guides.json`
beside the catalogue; workflow tools use matching `sdk-workflows.json`. Compiler probes run during packaging, not when the client connects.
Verify the zip against the release `SHA256SUMS`, and compare the first result's
`provenance` (`sdk_version`, `game_pack_version`, `source_sha`) with the kit's
`COMPATIBILITY.json`, before relying on its catalogue; a version match does not
establish matching source bytes, release approval, a loaded plugin or gameplay
support. The
[integration guide](../../docs/agent-integration.md#set-up-the-discovery-server-from-the-sdk-kit)
has the download, checksum and client steps.

The `core` and `core-source` archives, and archives of earlier SDK releases,
do not contain this catalogue; users of those archives obtain one in either of
two ways:

- **Public catalogue.** Download
  <https://scuba-sc.github.io/sco-core/sdk-catalogue.json> and check it before
  use. The site is redeployed from `main`, so the file's hash and coordinates
  change with it, and this README deliberately pins neither. Record the sha256 of
  the file you download, then read its coordinates:

  ```sh
  curl -fsSLO https://scuba-sc.github.io/sco-core/sdk-catalogue.json
  sha256sum sdk-catalogue.json   # macOS: shasum -a 256
  python3 -c 'import json; c = json.load(open("sdk-catalogue.json", encoding="utf-8")); print(c["sdk_version"], c["game_pack_version"], c["source_sha"], len(c["records"]), "records", len(c["tables"]), "tables")'
  ```

  In PowerShell:

  ```powershell
  Invoke-WebRequest https://scuba-sc.github.io/sco-core/sdk-catalogue.json -OutFile sdk-catalogue.json
  (Get-FileHash sdk-catalogue.json -Algorithm SHA256).Hash
  py -3 -c "import json; c = json.load(open('sdk-catalogue.json', encoding='utf-8')); print(c['sdk_version'], c['game_pack_version'], c['source_sha'])"
  ```

  Pass `encoding="utf-8"`: the catalogue is UTF-8, and Python on Windows
  otherwise decodes with the locale code page and fails. Use the file only when
  its `sdk_version` equals your archive's `VERSION`; otherwise build the matching
  catalogue from source. Its `source_sha` names the commit the catalogue was built
  from, which is not necessarily the release tag's commit; it is a supplied
  identifier, not a signature. Every server result repeats these coordinates as
  `provenance`, so keep the hash you recorded beside any result you quote.
- **Source build.** Check out the matching sco-core source and run the catalogue
  build in [Run from a source checkout](#run-from-a-source-checkout-maintainers).
  This needs the same compiler environment as any other build.

Downloading a catalogue does not install the launcher. Use the launcher from a
reviewed source checkout and point `--repo-root` at the extracted archive when
you want its example contents. The catalogue must describe the contracts being
consumed; the site's current mainline snapshot is not automatically a match for
an older release. A core archive has neither `sdk-guides.json` nor
`tools/sdk_guides.py`, so with a catalogue downloaded from the site the guide tools
return a tool error. They need the kit's `sdk-guides.json` beside the catalogue, or
a source checkout as `--repo-root`.

Pass `--repo-root` the archive's single top-level directory (for example the
extracted `sco-sdk-core-<version>`), not its parent:

```sh
python3 /absolute/path/to/sco-core/tools/sco-core-mcp/server.py \
  --catalogue /absolute/path/to/sdk-catalogue.json \
  --repo-root /absolute/path/to/sco-sdk-core-<version>
```

Catalogue example paths are repository-relative (`sdk/examples/hello/hello.c`),
while the core archive and the combined kit store `examples/hello/hello.c`. The
other files the server reads from the root follow the same rule:
`sdk/status_registry.json`, `sdk/native_index.json` and
`sdk/tools/sco-new-plugin.py` are `status_registry.json`, `native_index.json` and
`tools/sco-new-plugin.py` at the root of the combined kit. `get_example`
always tries the recorded path first. When it is absent **and** the root has no
`sdk` entry, the leading `sdk/` is dropped and the remainder is opened under the
same containment and no-symlink rules. A root that has an `sdk` entry (a
checkout, or the `core-source`/`host-source` archives, which keep repository
layout) is read only at recorded paths. A file missing there is reported as
unavailable, never served from the other layout. Results still report the
recorded path.

## Run from a source checkout (maintainers)

A kit already contains its catalogue; build one yourself only from a source checkout.

1. Use a trusted checkout with Python 3.9 or newer, Clang, and an **x64 C11 compiler
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
3. To check it by hand, start the server from the repository root:

   ```sh
   python3 tools/sco-core-mcp/server.py \
     --catalogue dist/sdk-catalogue.json \
     --repo-root .
   ```

   These relative paths work only from the repository root. A client configuration
   uses absolute paths: see the
   [checkout form in the integration guide](../../docs/agent-integration.md#maintainers-source-checkout-and-your-own-catalogue).

The process reads MCP messages on stdin and writes protocol responses on stdout;
it is not an interactive shell. Use a client that supports stdio MCP. The only
supported protocol version is **2025-11-25**. Initialization may offer that version
as a fallback when a client requests another version; the client must accept it or
disconnect. This is not support for older protocol semantics.

`--catalogue` is required unless `SCO_CORE_CATALOGUE` supplies its path. Client
configurations pass it explicitly to avoid dependence on inherited
environment variables. `--repo-root` is optional: without it, `get_example` returns
record-listed paths only, not file contents, and engine natives, the scaffold, status
evidence and release-approval records are `unavailable`. With it, examples are read
within that root under the restrictions below. Use absolute paths in client
configuration; do not depend on the client's working directory.

The catalogue is a source snapshot, not a live index. Rebuild it after changing
contracts, public headers, language mappings, or their evidence, and restart the
server before relying on the new snapshot. The server does not regenerate it.

## Tools and explicit allowlist

These are the original six catalogue tools, retained alone by explicit
`--minimal-tools`. The default developer profile also includes the six
[extended tools](#extended-tools-default) and two workflow tools below, fourteen
read-only tools in total. Three [development tools](#development-tools-opt-in)
are additionally offered when the operator configures project roots. List the
exact names of your profile explicitly, not with a wildcard.
The Arguments column gives the exact `inputSchema` property names, required ones
first; any other argument name is refused:

| Tool | Arguments | Purpose |
|---|---|---|
| `search_api` | `query`; optional: `language`, `service`, `limit` | Search catalogue contracts, with optional service and language filtering. |
| `get_symbol` | `symbol_or_id` | Retrieve a symbol's contract and its source-backed language mappings. |
| `list_services` | none | Discover public service tables, versions, sizes, member counts and [stability](#stability) labels. |
| `get_example` | `symbol_or_id`; optional: `language` | Return a selected record's listed example paths, and optionally their bounded contents. |
| `get_capability_status` | `symbol_or_id` | Read documented capability evidence, limitations, the record's [evidence state](#evidence-state) and its table's [stability](#stability); not a live availability probe. |
| `check_compatibility` | optional: `sdk_version`, `game_pack_version`, `tag`, `source_sha`, `service`, `min_version`, `member_extent` | Compare requested coordinates with catalogue metadata, read a [release approval record](#release-approval-records) and check a service's minimum version and member extent; not a runtime compatibility test. |

`symbol_or_id` takes a table-qualified symbol such as `sc_actors_v1.spawn_npc` or
the record's stable id such as `sc-actors-v1-spawn-npc`; `search_api` results
carry both. Inspect `tools/list` for each tool's full input schema. Language values
are `c`, `cpp`, `csharp`, and `lua`; use those tokens rather than display names.
`search_api` accepts at most 512 characters in `query`, a `limit` from 1 to 50
(default 10), and an optional `service`. `check_compatibility` accepts
`sdk_version` and `game_pack_version` as strings to compare with the catalogue's
own `sdk_version` and `game_pack_version` (copy them from any result's
`provenance`).
These are exact coordinate comparisons, not version-ordering claims. Service-table
versions are packed major/minor integers; SDK and game-pack coordinates are
separate strings. Do not substitute one for another.

`check_compatibility` also takes these optional arguments. Each adds a section to
the result only when supplied, so existing callers see the same fields:

| Argument | Format | Result |
|---|---|---|
| `source_sha` | full lowercase 40-hex commit | `coordinate_matches.source_sha` against the catalogue, and a `release` section |
| `tag` | `sdk-vMAJOR.MINOR.PATCH` | a `release` section for `docs/legal/release-decisions/<tag>.json` |
| `service` | a catalogue service name from `list_services` | a `service_table` section with the catalogue table, version and size |
| `min_version` | `MAJOR.MINOR`; requires `service` | `version_satisfies`: equal major and catalogue minor at least the requested minor (the `query_service` rule) |
| `member_extent` | integer bytes 1-65535; requires `service` | `extent_within_catalogue_size`: the extent fits the catalogue table size |

The `tag` pattern has no pre-release suffix, so the release candidate tag
`sdk-v2.4.0-rc.1` is refused as not matching the advertised pattern; compare
`sdk_version` and `game_pack_version` instead. The input schema advertises the
patterns and `dependentRequired`. Arguments outside it, a malformed value,
`min_version`/`member_extent` without `service`, or an unknown service are refused
as tool errors. `service_table` describes the
catalogue's compiled header snapshot: a runtime provider table can still be older
or shorter, so check `table->size` and a non-NULL pointer before calling.

### Release approval records

With `--repo-root`, a `tag` reads `docs/legal/release-decisions/<tag>.json`; a
`source_sha` alone scans the `sdk-v*.json` records there (at most 64). Without a
root, `release.state` is `unavailable`. Otherwise it is `matching_approval_record`
when a schema-2 approval's filename, `tag` and built source SHA all agree with the
request, and `no_matching_approval_record` otherwise. Each record reports its
`format` (`approval_schema_v2`, `legacy_unsigned_v1`, `unrecognized`,
`unparseable` or `unreadable`), `built_source_sha`, `distribution_status`,
`approved_at` and whether a detached `.json.sig` file is `present`.

- `built_source_sha` is the candidate's built source commit. The release tag points
  at a later, approval-only commit that adds the record. They are different
  commits, and this server does not resolve tags.
- `signature_verification` is always `not_performed`. Verifying the SSH signature,
  payload and approval-only descendant is
  [`tools/release_verify.py`](../release_verify.py)'s job, with independently
  trusted allowed signers. A present `.sig` file is not a valid signature.
- `publication` is always `not_checked`. `approved-for-publication` records a
  decision, not a published release.
- A record is committed at its release tag, so a branch without it reports no
  match. That is not proof that no release exists.

Every discovery tool (all fourteen in the default profile) advertises
these MCP annotations; the development tools advertise
[different ones](#development-tools-opt-in):

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

### Extended tools (default)

<a id="extended-tools-opt-in"></a>

These six developer tools and the two workflow tools are included by default.
`--minimal-tools` omits all eight and offers exactly the original six catalogue
tools when no project roots are configured. The legacy `--extended-tools` and
`--workflow-tools` flags remain compatibility aliases, not prerequisites. Add
the default fourteen names to client allowlists explicitly.
[The integration guide](../../docs/agent-integration.md#choose-a-profile) explains
which reviewed generator code the developer tools can run:

| Tool | Arguments | Purpose |
|---|---|---|
| `list_engine_natives` | optional: `subsystem`, `tier`, `query`, `limit`, `cursor` | Page through `sdk/native_index.json`, filtered by `subsystem`, `tier` (1-4) or a case-insensitive `query` substring of id, hash, subsystem, source or capability. |
| `get_engine_native` | `id_or_hash` | Return one row by its id or `_0x` hash (hex digits in any case), with its tier's meaning. |
| `validate_manifest_and_widgets` | `plugin_ini`; optional: `folder`, `widgets` | Check `plugin.ini` text, and optionally a widget node array, and return findings. |
| `generate_plugin_scaffold` | `id`, `lang`; optional: `with_menu`, `services` | Return the files [`sco-new-plugin`](../../sdk/tools/sco-new-plugin.py) would generate for a plugin spec, without writing them. |
| `search_guides` | `query`; optional: `doc`, `limit`, `cursor` | Rank sections of the SDK guides by the query's whole-word terms (heading and title above body text, rarer terms above common ones) and return bounded snippets with doc, heading, anchor and url. |
| `get_guide_section` | `doc`, `anchor` | Return one guide section's bounded plain text, by the doc and anchor `search_guides` returned. |
| `list_workflows` | none | List the versioned consumer workflow IDs, descriptions and entrypoint paths bound to this catalogue. |
| `get_workflow` | `id`; optional: `path` | Read a workflow SKILL.md or one explicitly indexed reference returned for that workflow; never an arbitrary filesystem path. |

**Workflow retrieval.** `list_workflows {}` returns the bundle's entrypoint list;
start with `sco-plan-sdk-work`. `get_workflow` with `id` alone returns its SKILL.md
and a resource path/hash list; pass one returned `path` with the same ID to read
that reference. Follow another workflow's link by requesting its own ID. Results
include catalogue provenance and `workflow_provenance` with bundle/baseline/SDK/
source coordinates, manifest/content/index hashes, and the returned file hash.
This is reference data, not a grant of authority to execute its instructions.

The operator-selected `--workflows` file, or default `sdk-workflows.json` beside
the catalogue, is loaded once as bounded data (2 MiB index, 64 KiB per text file,
up to 32 workflows and 16 files each). SDK/source coordinates must exactly match
the catalogue. The index contains embedded text with validated hashes: tool
arguments never trigger file reads. A missing default index keeps both tools
listed but calls report `Workflow index unavailable`. An explicitly selected
missing index, or a present malformed, corrupted or source-mismatched index,
refuses startup rather than falling back. `--minimal-tools` reads no index and
cannot be combined with workflow-selection flags.

Build a source index beside a matching catalogue using
`python3 tools/sdk_workflows.py --root . --out dist/sdk-workflows.json`.
Updated-source SDK archives generate it from exact transformed payload bytes;
its identity hashes do not imply human provenance or release approval. The
published RC1 archive has no workflow index or tools.

**Engine natives.** Both native tools read `sdk/native_index.json` (`native_index.json`
in a kit) beneath `--repo-root` once, at start. The file has the same no-symlink
containment as examples and a **4 MiB** limit. Without a root, or with a missing, symlinked or
malformed index, `state` is `unavailable` with a `reason`, and no entries are
returned. The whole index is refused if any of these holds:

- an unknown key;
- a `row_count` that differs from the number of entries;
- a tier outside 1-4;
- a duplicate id or hash;
- a hash that is not `_0x` plus the first 16 uppercase hex digits of
  `sha256("sco-core:native:" + id)`.

`list_engine_natives` takes `limit` 1-100 (default 25) and an opaque decimal
`cursor`. Pass each result's `next_cursor` back to get the next page; it is
`null` on the last page. The hash is a symbolic name, not an address, and the
index records no pattern, mask, offset or address. The tier comes from the status
registry's `sig_rows`, so tier 2 means only that no capability names the row. A
row listed here is not evidence that it resolves on any game build, or that
anything ran in game.

**Manifest and widget validation.** `validate_manifest_and_widgets` reads and
writes no files. It takes these arguments:

| Argument | Required | Content |
|---|---|---|
| `plugin_ini` | yes | The manifest text. |
| `folder` | no | A plugin folder name (no separators), checked against `id`. |
| `widgets` | no | 1-256 node objects. |

The manifest follows the reader of
[`sco-plugin-check`](../../sdk/tools/sco-plugin-check.c), which mirrors the host:

- keys `id`, `name`, `version`, `author`, `api`, `kind`, `entry` and `requires`,
  with their formats;
- reserved ids;
- the SDK's api `1.1`;
- `entry` against `kind`, and a requires cycle;
- the `[settings]` grammar: types, bounds, enum choices, quoted label and help,
  and defaults.

The reader stops at the first failure, as the host does. So
`manifest.findings` holds that failure, plus the folder mismatch if there is one.
Its messages use the checker's wording. Unknown keys and sections appear in
`notes`. The validator does not check that `entry` exists, that required
capabilities have providers, or that the plugin loads.

A widget node has these keys:

- `kind`: `1`-`10` or a name such as `"slider"`;
- `id`;
- `parent`, `flags`, `index`, `label`, `text`, `value`, `minimum`, `maximum`,
  `items` and `columns`.

`kind` and `id` are required. Omitted optional fields default to zero or null.
Unknown keys and wrong JSON types are refused as
tool errors. Each violation of the
[`sco_widgets.h`](../../include/sco_widgets.h) invariants becomes one finding,
with the node index, its id and the host result it maps to (`BAD_ARG`, or
`TOO_MANY` for text, item or column limits):

- unique nonzero ids;
- a parent that precedes the node, is a row or column, and whose subtree is still
  open, so each subtree is contiguous in preorder;
- nonempty labels on interactive nodes;
- UTF-8 text of at most 255 bytes;
- unused fields left zero or null;
- toggle, slider, combo, list and table rules;
- `PASSWORD` on `text_input` only.

The host refuses the whole panel at its first violation. These checks are not
made: the panel id and title, the stride and size fields, pointer readability, and
renderer markup in labels.

**Plugin scaffold.** `generate_plugin_scaffold` returns the plugin folder
that [`sdk/tools/sco-new-plugin.py`](../../sdk/tools/sco-new-plugin.py) would
write, as `files`: a map from relative path to text. For `c` and `cpp` that is
`plugin.ini`, `CMakeLists.txt` and `<id>.c` or `<id>.cpp`; for `csharp`,
`plugin.ini`, `<id>.csproj` (NativeAOT), `<Name>Plugin.cs` and `README.md`; for
`lua`, `plugin.ini` (`kind = lua`), `main.lua` and `CMakeLists.txt`. It takes
these arguments:

| Argument | Required | Content |
|---|---|---|
| `id` | yes | 1-31 characters from `a-z`, `0-9` and `_`; `sco`, `host`, `menu` and `game` are reserved. |
| `lang` | yes | `c`, `cpp`, `csharp` or `lua`. |
| `with_menu` | no | `true` adds a `sco.widgets` panel. Default `false`. |
| `services` | no | Up to 16 `NAME` or `NAME@MAJOR.MINOR` strings, looked up as optional services on `game.ready`. Refused for `lua` (the sandbox has no service lookup), except `sco.widgets` with `with_menu`. |

The display name and author take the generator's defaults; there is no output
path. Any other argument, or a value outside the schema, is refused as a tool
error. A spec the generator itself refuses, such as a reserved id or a service
listed twice, or services with `lua`, comes back as a tool error that carries the
generator's message.

The server writes no files and runs no build or check. To use the result, write
the files into a missing or empty folder, then run the steps the CLI prints and
the result's second conclusion names for that language: for `c`, `cpp` and `lua`,
`cmake -S DIR -B DIR/build -DSCO_SDK=<SDK folder>` and
`cmake --build DIR/build --target check` (for `lua`, also
`tools/lua-check.lua` with Lua 5.4); for `csharp`,
`dotnet publish DIR -c Release -r win-x64 -o DIR/out/<id> -p:ScoSdk=<SDK folder>`,
then `sco-plugin-check` on `DIR/out/<id>` with and without `--short-tables`.
The output is exactly what `render_plugin(spec)` returns for the same spec; the
tests compare the two.

The generator is code from `--repo-root`. On the first call the server reads
`sdk/tools/sco-new-plugin.py` (`tools/sco-new-plugin.py` in a kit), with the examples'
no-symlink containment and the 256 KiB limit, and executes those source bytes in the
existing server process. It does not
start a subprocess or write `__pycache__`. Without a root, or when the file is
missing, a symlink or fails to load, `state` is `unavailable` with a `reason` and
`files` is empty. The result is not bound to the catalogue `source_sha`.
Generated native/C++/C# plugins declare `api = 1.1`, matching their exported built-against version; Lua declares `api = 1.0`. Short-table checks do not establish API 1.0 loader admission. Nothing generated here has been loaded by
a host or run in game.

**Guide search.** `search_guides` and `get_guide_section` answer "how do I ..."
questions from the SDK guides rather than the contract records. Their data is
`sdk-guides.json`, which [`tools/sdk_guides.py`](../sdk_guides.py) builds from the
Markdown guides listed in [`site/public-inputs.json`](../../site/public-inputs.json)
(never `docs/legal/` or `docs/design/`, in any letter case) and from the SDK's own
plugin-authoring guides: every `sdk/docs/*.md` that
[`sdk/components.json`](../../sdk/components.json) lists as project-authored and
shipped in a profile (`plugin.ini`, plugin rules, Lua, widgets and data packs
today). Each heading starts one section with `doc`, `title`, `heading`,
`anchor`, `level`, `text` (plain text, at most 4000 characters), `truncated` and
`url`. `anchor` is the GitHub-style slug of the heading in the repository file;
`url` is the docs-site page for that guide, whose own heading anchors may differ,
or the repository path for an `sdk/docs` guide, which the site does not render.

The server finds the sections in this order, on the first guide call:

1. the file given with `--guides`;
2. `sdk-guides.json` beside the catalogue, as the developer kits ship it;
3. without either, an in-memory build from `--repo-root`: the server reads
   `tools/sdk_guides.py` with the examples' no-symlink containment and 256 KiB
   limit and runs those bytes in its own process, which then reads the
   allowlist, `sdk/components.json` and the guides beneath that root.

A file wins over `--repo-root`. In a checkout you edit, either leave the file
out (the sections are then built from the tree on each server's first guide call) or
rebuild it after editing a guide and restart the client.

A file that exists but is malformed is reported, not bypassed. With none of the
three, both tools return a tool error that names the missing file. Each result's
`guides` object says which source answered (`mode` `file` or `source`), its
`source_sha` (`null` for an in-memory build) and whether that equals the
catalogue's. To build the file beside a source catalogue:

```sh
python3 tools/sdk_guides.py build --out dist/sdk-guides.json --source-sha "$(git rev-parse HEAD)"
```

`search_guides` drops common question words (`how`, `do`, `i`, `the` and similar)
and single characters unless nothing else is left. Terms match whole words,
case-insensitively: `ui` does not match `build`, the parts of a dotted or
hyphenated word also count (`sco.net` matches `net`), and a term of four or more
characters also matches the words it starts (`deploy` matches `deployment`). Every
section that contains at least one term is ranked by a score that weights a term
in the heading above one in the title or doc path, above body occurrences (at most
five), each multiplied by the term's rarity across all sections, so a rare term
outweighs a common one such as `plugin`; ties go to more distinct terms matched,
then guide order. `doc` keeps one guide's sections (an unknown `doc` is refused
with the list of known ones); `limit` is 1-25 (default 5) and `cursor` pages like
`list_engine_natives`. Snippets are at most about 320 characters. Ranking is
term matching, not semantic search. Guides are prose, not contract records:
check a method with `get_symbol` before relying on it. Nothing in a guide is
evidence that code ran in game.

### Development tools (configured roots)

<a id="development-tools-opt-in"></a>

Three more tools let an agent run the developer's own loop on this machine:
check a built plugin, and configure, build and test a CMake plugin project.
The operator configures at least one `--project-root DIR`; the server then
automatically offers the tools. Without project roots they are absent and calls
to their names are refused as unknown. `--enable-dev-tools` remains a compatibility
flag and still requires a project root. Add each name to the client allowlist
explicitly; three execution tools plus the default fourteen discovery tools means
seventeen offered tools:

| Tool | Arguments | Purpose |
|---|---|---|
| `run_plugin_check` | `folder`; optional: `mode` | Run `sco-plugin-check` on a built plugin folder. `mode` is `full` (default) or `short`, which adds `--short-tables` (a truncated 1.0-sized root prefix, not API 1.0 loader admission). |
| `build_plugin_project` | `project` | Configure a CMake plugin project into its own `build` subfolder, then build it. |
| `test_plugin_project` | `project` | Run `ctest` in that `build` subfolder. Run `build_plugin_project` first. |

`folder` and `project` are paths: absolute, or relative to the first
`--project-root`. They are the only things a caller supplies. There is no
argument for a command, flag, generator, build type, target, test filter or
environment variable, and any other argument is refused as a tool error.

**Operator configuration.** Everything else is fixed when the server starts:

| Flag | Default | Effect |
|---|---|---|
| `--enable-dev-tools` | unnecessary with roots | Compatibility flag; refused at start without a `--project-root`. |
| `--project-root DIR` | none | A folder the tools may use; configuring one automatically offers all three development tools. Repeat for more folders. Each must exist. |
| `--plugin-check PATH` | `bin/` or `out/bin/` of the SDK folder | The `sco-plugin-check` program. `cmake --install <build> --prefix out` in the SDK folder puts it in `out/bin/`. |
| `--cmake PATH` | `cmake` on the server's `PATH` (`cmake.exe` on Windows) | The CMake program. |
| `--ctest PATH` | `ctest` beside `--cmake`, else on `PATH` | The CTest program. |
| `--cmake-generator NAME` | CMake's default | Passed as `-G` when configuring, for example `Visual Studio 17 2022`. |
| `--cmake-platform NAME` | none | Passed as `-A` when configuring, for example `x64` with a Visual Studio generator. |
| `--sco-sdk DIR` | `--repo-root`, or its `sdk/` | The SDK folder passed as `-DSCO_SDK`: the folder that holds `cmake/sco-plugin.cmake`. |
| `--dev-timeout SECONDS` | 600 | Time limit for one call, 1-7200. A build's configure and build steps share it. |
| `--dev-output-limit BYTES` | 32768 | Output tail returned per step, 256-262144. |

Development options without an operator-configured project root are refused at
start, as is `--enable-dev-tools` without a root or an out-of-range limit. Programs are looked up on every
call, so a checker built after the server started is found. A program path that
ends in `.py` is run with the server's own Python interpreter, which lets an
operator use a wrapper script. Batch files are refused: Windows runs a `.bat` or
`.cmd` program through `cmd.exe`, which would parse the caller's path as shell
syntax. On Windows a program must be an `.exe` or a `.py` wrapper, on every
platform a `.bat` or `.cmd` name is refused, and the `PATH` lookup asks for
`cmake.exe` and `ctest.exe` only, never a shim. An explicit flag naming one is
refused at start; a program found at call time gives `unavailable` with the
reason. Each call runs exactly these command lines, without a shell:

| Tool | Command line | Working folder |
|---|---|---|
| `run_plugin_check` | `sco-plugin-check FOLDER`, plus `--short-tables` for `mode: "short"` | `FOLDER` |
| `build_plugin_project` | `cmake -S PROJECT -B PROJECT/build [-G GENERATOR] [-A PLATFORM] -DSCO_SDK=SDK`, then `cmake --build PROJECT/build --config Release` | `PROJECT` |
| `test_plugin_project` | `ctest --test-dir PROJECT/build -C Release --output-on-failure --no-tests=error` | `PROJECT` |

The build step runs only when configuring succeeded. Both cmake steps also set
`MSBUILDDISABLENODEREUSE=1`, so no MSBuild node outlives the call, and
`VSLANG=1033`, which asks MSVC for English messages where that language is
installed; otherwise the programs inherit the server's environment. `test_plugin_project` is refused
until `PROJECT/build/CMakeCache.txt` exists, and with `--no-tests=error` a
project without tests fails rather than passing with nothing run.

A project made by `generate_plugin_scaffold` builds its plugin into
`build/check/<id>/` beside a copy of `plugin.ini`. Pass that folder to
`run_plugin_check`, once with each `mode`, to repeat what the generated `check`
target does.

**Results.** Each result has `state`: `completed`, `timed_out`, `not_started`
(the program could not be started) or `unavailable` (a program or the SDK folder
is not configured or not found, with a `reason`; nothing was run). It also has
`passed`, which is true only for a completed run with exit code 0; for a build,
both steps must pass. `program`, the file name only, appears once per result. A
run reports these fields (a build reports them for each of its `steps`):

- `arguments`, exactly as passed;
- `exit_code`, `timed_out` and `duration_seconds`;
- `output`: at most the last `output_limit` bytes of combined stdout and stderr;
- `output_bytes` and `output_truncated`;
- `output_complete`: false when a descendant still held the output open 5
  seconds after the program ended.

`run_plugin_check` adds `findings`, the checker's `FAIL` lines without the
prefix (at most 256 lines and 256 KiB, with `findings_truncated` true when more
were dropped), and `summary`, its last `OK: N failure(s)` or
`FAILED: N failure(s)` line. `test_plugin_project` adds `summary`, CTest's
`N% tests passed` line. Both are read from the whole output stream, so a
truncated tail still reports them. Results name the resolved folder or project
path. Every result carries `provenance`, like the other tools.

A result always fits the 4 MiB response cap. Output that is not UTF-8 is decoded
with replacement characters, which take more room once escaped, so if a whole
result would be too large the server shortens the output tails first and then
the findings, setting `output_truncated` and `findings_truncated`. `state`,
`exit_code`, `passed` and `summary` are always kept.

**Security model.**

- **Containment.** A path is first checked as text: joined to the first root and
  normalized, it must lie inside a configured root, before the filesystem is
  touched. It is then resolved (symlinks, junctions and `..`), and the result
  must be an existing folder inside a root. A path outside every root, a missing
  path and a file all get the same refusal, so a caller learns nothing about
  other paths. NUL characters, and on Windows a `:` after the drive, are
  refused. A project whose `build` entry resolves outside the roots is refused.
  The roots themselves are operator-supplied and trusted; a symlinked root is
  followed. These checks are not race-free against a project that changes during
  the call.
- **Fixed programs and arguments.** Programs, generator, platform, SDK folder,
  timeout and output limit come only from the operator's command line. The
  caller's path is passed as one resolved absolute argument, so it never starts
  with `-`. Processes start with stdin closed and no shell, and batch files,
  which Windows would hand to `cmd.exe`, are refused (see above).
- **Bounded time and output.** Each process runs in its own process group: a new
  process group and a job object on Windows, a new session on POSIX. On timeout
  the whole tree is killed: `taskkill /F /T` and the job object on Windows,
  `SIGKILL` to the process group on POSIX. When the program exits normally, any
  descendant still running is killed the same way, so nothing it started
  outlives the call. On Windows the job object is set to kill on close, so if
  the server itself is killed or exits mid-call, the running tree ends with it.
  On POSIX it does not: killing the server mid-call leaves that call's process
  group running, so end it yourself (for example `kill -9 -PGID`). A descendant
  that deliberately leaves the group or job can survive. Output beyond the limit
  is dropped from the front.
- **This runs code.** CMake runs the project's `CMakeLists.txt`, the build runs
  compilers, and `sco-plugin-check` loads the plugin's native code and runs it
  against a stand-in host. None of this is a sandbox. Grant only project roots
  whose code you would build and run yourself. The tools advertise
  `readOnlyHint: false`, `destructiveHint: true`, `idempotentHint: false` and
  `openWorldHint: false`. Prefer a client that asks before each call.
- **One call at a time.** The server handles requests in order, so a build
  blocks every other tool call until it ends or times out. The limit of 120
  calls per 60 seconds covers these tools too.
- **What a pass means.** A passing check is synthetic host and ABI evidence from
  a stand-in host. It is not the real host, and nothing here loads the plugin in
  the game: **not run in game**.

## MCP clients

Client configuration lives in one place, the
[integration guide](../../docs/agent-integration.md#register-the-server-in-your-client):
the kit-first launcher for Claude Code, Codex, Cursor, VS Code and Copilot CLI, in
Windows and macOS/Linux forms, and
[how to verify the connection](../../docs/agent-integration.md#verify-the-connection).
Rules that hold for every client:

- Use absolute paths, with each flag and each value as its own argument. Do not depend
  on the client's working directory.
- On Windows use `py` with `-3` as the first argument, or an absolute `python.exe`. A
  standard Windows Python install has `py` but not `python3`, and a `python3` alias can
  be the Microsoft Store stub.
- The default client allowlist names all fourteen discovery tools; explicit
  `--minimal-tools` uses only the original six. Configured project roots additionally
  offer three execution tools, which need their own exact client permissions. Never
  use a wildcard.
- Removing `--repo-root` and its path disables `get_example` contents, the engine-native
  tools, the scaffold, status and stability evidence and release-approval lookups. The
  guide tools still work when `sdk-guides.json` sits beside the catalogue. Do it only to
  keep example contents from the client or its model provider.
- No API keys, tokens, or secrets are needed.

## Copilot CLI and IDE integration

See [the engineering integration guide](../../docs/agent-integration.md) for
client-specific configuration, the literal-launcher acceptance check and the
remaining administrator/runner acceptance steps. It does not install a
`.github/mcp.json`, modify repository settings or imply a hosted server.

## GitHub Copilot repository configuration

A repository administrator configures this on GitHub under **Settings > Copilot >
MCP servers**, in the **MCP configuration** JSON field, then selects **Save MCP
configuration**. GitHub's repository configuration is shared by Copilot cloud agent
and Copilot code review; it is not a committed `.github/mcp.json` file. See
[GitHub's MCP configuration documentation](https://docs.github.com/copilot/how-tos/copilot-on-github/customize-copilot/configure-mcp-servers).

Paste the following JSON, the minimal profile with its six-name explicit allowlist
(the same checkout block as in the
[integration guide](../../docs/agent-integration.md#maintainers-source-checkout-and-your-own-catalogue)).
Replace the path placeholder with the absolute checkout path **inside Copilot's
runner**, not a path on your workstation:

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

Prepare the runner before enabling this configuration:

1. Check out the repository in Copilot's setup environment and make Python 3.9 or
   newer, Clang, and an x64 C11 compilation environment available.
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
review requires `annotations.readOnlyHint: true` on tools it uses. All fourteen
default discovery tools, including the six in this minimal allowlist, set that
annotation, but annotations and an allowlist are not a sandbox for the
client itself. Do not add a wildcard or assume this restricts the client's other
tools. Consult the linked GitHub documentation for current UI and runner behavior.

## Troubleshooting

| Symptom | First checks |
|---|---|
| Python cannot start | Python 3.9 or newer. Windows: use `py` with `-3`, or an absolute `python.exe`; a `python3` alias can be the Microsoft Store stub. macOS/Linux: use `python3` or an absolute interpreter. GUI clients may inherit a different `PATH`. |
| `can't open file '.../server.py'`, exit code 2 | Python could not find the launcher, so the path to `server.py` is wrong. Use the SDK folder, the one that directly contains `sdk-catalogue.json`, not the extract folder above it; check it with [Find the SDK folder](../../docs/agent-integration.md#find-the-sdk-folder), and quote a path that contains a space when you type it in a shell. |
| `catalogue refused: FileNotFoundError`, exit code 1 | `server.py` started, but the `--catalogue` file does not exist. In a kit it is `SDK/sdk-catalogue.json` and there is no `dist/` folder; in a checkout, build `dist/sdk-catalogue.json` first. For other refusals check absolute paths, UTF-8, schema and source/version coordinates. |
| Process waits without a prompt | Expected: it reads newline-delimited MCP JSON-RPC on stdin, not shell commands. Keep stdout protocol-only and inspect client logs. |
| Initialization fails | Negotiate `2025-11-25`, then send `notifications/initialized` before tool calls. |
| Developer/workflow tool is unknown | Remove `--minimal-tools` and grant the fourteen exact default names in the client. The older published RC1 launcher needs `--extended-tools` for twelve tools and has no workflow tools. |
| Development tool is unknown or refused | Configure an operator `--project-root`; current source offers execution tools automatically. Pass a folder inside that root; `unavailable` names the missing program or SDK folder. |
| Scaffold/native metadata unavailable | `--repo-root` must be the SDK folder, not its parent folder: the server starts either way. Check trusted root, kit composition, file layout and containment. Unavailable is not an empty successful inventory. |
| Guide search unavailable | Put `sdk-guides.json` beside the catalogue (or pass `--guides`), or pass a source checkout as `--repo-root`. A malformed file is reported, not bypassed. |
| Compatibility matches but service is absent | Catalogue equality is not runtime readiness. Check live capability, whole member extent, table size and non-null pointer in the plugin. |
| Catalogue is stale | Rebuild matching reviewed source and restart. Root metadata/generator code are separate inputs, not authenticated by catalogue SHA. |
| Guide sections are stale | A `sdk-guides.json` beside the catalogue (or from `--guides`) wins over `--repo-root`. Rebuild it with `tools/sdk_guides.py build`, or delete it to build from the tree, and restart. `guides.source_sha` says which commit the file came from. |
| Guide text says 2.4.0 is not on the release page, or `check_compatibility` refuses the tag `sdk-v2.4.0-rc.1` | Known issues in release candidate 1: its guides and READMEs predate publication, and the tag pattern has no pre-release suffix. The docs site supersedes the kit's text; compare `sdk_version` and `game_pack_version`. |

Discovery cannot invoke commands, reload plugins or diagnose live game state.
The project-root development tools build and check plugins on this machine only; they
never reach a host or the game.
Runtime inspection uses the separate default-off launcher with independent host
and security admission.

## Security boundaries and limits

- **No network, subprocess execution, or file writes by the server**, unless the
  operator configures project roots for the [development tools](#development-tools-opt-in), which run
  fixed programs on folders inside the configured project roots. Its interface
  is stdio and its discovery operations read local catalogue data and optional
  example files. The separate catalogue builder compiles/runs probes and writes
  its output; the server does neither. The only repository code it runs is
  `sdk/tools/sco-new-plugin.py` (`tools/sco-new-plugin.py` in a kit, which has no
  `sdk/` folder; see [Using an extracted SDK archive](#using-an-extracted-sdk-archive)),
  on the first `generate_plugin_scaffold` call, and `tools/sdk_guides.py` (a checkout
  only: a kit has no such file and ships `sdk-guides.json`), on the first guide call when
  no `sdk-guides.json` is found; both in the default developer profile with a `--repo-root`,
  in-process. Treat that root as code you would run yourself.
- **Not arbitrary file access.** `get_example` reads only paths listed in the
  selected catalogue record's `examples`, beneath the configured repository root.
  The only other files read under that root are `sdk/status_registry.json` (at
  most 1 MiB; at the kit root without the `sdk/` prefix, as are `sdk/native_index.json`
  and `sdk/tools/sco-new-plugin.py` below, see
  [Using an extracted SDK archive](#using-an-extracted-sdk-archive)),
  or `docs/game/status.md` when there is no registry (each read once,
  at start), the `sdk-v*.json`/`.json.sig` records in `docs/legal/release-decisions/`
  (per `check_compatibility` call) and, in the default developer profile,
  `sdk/native_index.json` (at most 4 MiB, once at start),
  `sdk/tools/sco-new-plugin.py` (at most 256 KiB, once, on the first scaffold
  call) and, for the guide tools without an `sdk-guides.json`,
  `tools/sdk_guides.py` (at most 256 KiB) with `sdk/VERSION`, once, on the first
  guide call. That builder then reads `site/public-inputs.json`,
  `sdk/components.json` and the guides they name with its own check: it refuses a
  symlink or Windows junction in any path component and any file whose real path
  is not exactly that name beneath the root (which also refuses a name in the
  wrong letter case), and it caps each guide and the allowlist at 2 MiB and the
  component manifest at 8 MiB. The `sdk-guides.json` file itself, beside the
  catalogue or from `--guides`, is operator-chosen like the catalogue and is read
  once, at most 16 MiB. The server's own root reads use the examples' no-symlink
  containment, and the default size limit applies to the rest.
  Absolute paths, traversal outside the root, and symlinks anywhere the catalogue
  names are refused, including on the extracted-archive `examples/...` fallback;
  an `sdk` symlink in the root disables that fallback. The configured root itself is operator-supplied and trusted,
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

## Evidence state

`get_symbol` and `get_capability_status` return an `evidence_state` object for the
selected record. The server computes it from the schema 1 catalogue and, for
game-pack records, from `sdk/status_registry.json` under `--repo-root`
(`status_registry.json` at the root of a kit), or from `docs/game/status.md` when there
is no registry. The catalogue
is unchanged and still `schema_version` 1, so its consumers (including the docs
site generator) are unaffected. Each state is reported only where a
machine-readable or strictly parsed source exists; otherwise it is `unknown`.

| Field | Values | Source |
|---|---|---|
| `scope` | `game-pack` or `neutral` | `source_header`: `include/sc_*.h` is game pack, the `sco_*` headers (services and entrypoints) are neutral |
| `contract_status.state` | `documented` | the record exists in a catalogue that `tools/sdk_catalogue.py build` validated against compiled headers and `docs/contracts.md` |
| `default_runtime_state.state` | `unknown` | no machine-readable per-method default exists |
| `sigcheck_receipt.state` | `not_applicable` (neutral) or `unknown` (game pack) | no per-method sigcheck receipt is in the repository |
| `gameplay_receipt.state` | `not_applicable` (neutral) or `unknown` (game pack) | no machine-readable gameplay receipt exists. The server never reports in-game verification |

Neutral `sco.*` records, `sco_api` and the entrypoints carry no game fields:
sigcheck and gameplay are `not_applicable`, and there is no `service_status`.

Game-pack records also carry `service_status`. It is **service-scoped**. When
`sdk/status_registry.json` (`status_registry.json` in a kit) exists, its rows are the
capabilities of the `service`
entry whose `service` and header file name both match the record's catalogue
table. Otherwise they are the rows of the
`| Table | Version | Stability | Capability | Default | Evidence |` table in
`docs/game/status.md` (or the five-column table without Stability that older
archives carry) whose backticked service and linked header both match.
`service_status.source` names the file used. Each row keeps `capability`, `default` (`ON`, `OFF` or
`none`) and `evidence` verbatim, plus an `evidence_level`:
`row_resolved_sigcheck` (with `game_build`), `off_pending_in_game_confirmation`,
`blocked`, or `unclassified` for any other text, including in-game claims.
`documented_version` and `version_matches_catalogue` compare the row's version with
the catalogue. `stability` is the table's label, `stable` or `experimental`, or
`null` when the registry entry or status table predates labels. The registry is read
at `schema_version` 2 (labels required on service and event-bus entries, with
`stable_since` exactly on `stable` ones, and absent on signature-row groups) or 1 (the
unlabelled registry of older archives, no `stability` or `stable_since` keys); any
other version is refused. In the status table a signature-row group's Stability cell
is `-`, read as `null`.

Registry rows also keep these registry fields where they are set:

- `registry_evidence_level`: `sigcheck_resolved`, `off_pending_probe`,
  `synthetic_only`, `blocked` or `undocumented`. An `in_game_verified` entry is
  reported as `unclassified`, like any other in-game claim.
- `name`, `methods`, `sig_rows`, `spike_ref` and `evidence_source`.

The `capability` cell is rendered exactly as
[`tools/status_registry.py`](../status_registry.py) renders it.

If the registry exists but is malformed, unreadable or a symlink, `state` is
`unavailable` with a reason naming the registry. The server does **not** fall back
to `docs/game/status.md`, so a broken registry is never hidden by the document
generated from it.

`state` is:

- `rows_found`: matching rows exist. They describe capabilities of the service, not
  this method.
- `no_rows`: the table has no row for this service. Some services are described
  only in prose.
- `unavailable`: there is no `--repo-root`, the file is missing or a symlink, or
  the registry or table is malformed. Any row with the wrong cell count, an unknown default
  or a missing separator refuses the whole table rather than half-reading it.

The registry or status document is read from the configured root when the server starts. It
is not bound to the catalogue's `source_sha`. Its sigcheck rows are dated to the
build named in each row, and none of it is evidence that an operation worked in
a game session.

## Stability

`list_services` adds `stability` to each service, and `get_capability_status` adds
`stability` for the record's table. Both also return a `stability_context` naming
[`docs/stability.md`](../../docs/stability.md), the game status source and whether it
was read. Values:

- `stable`: never removed or changed incompatibly within SDK 2.x; grows only at its
  tail behind a new minor version macro. Once a released SDK has carried the label,
  the table stays `stable` for the rest of SDK 2.x. The `sco_api` root table, plugin
  entry points and every `sco.*` host table are always `stable`.
- `experimental`: a later SDK minor may replace the table with a new table under a
  distinct service name and its own header; the old name then stays published, marked
  deprecated, for at least one further SDK minor release. The kernel publishes one
  table per name, so a replacement never reuses the old name.
- `unknown`: a game-pack table (any table declared in an `include/sc_*.h` header: the
  `game.*` tables, `teleport.spatial` and `spawn.entities`) whose label could not be
  read: no `--repo-root`, the status source is unavailable, refused or predates labels,
  or it has no entry for the table.

A game-pack label comes from the same source as `service_status` and is read at
server start, not bound to the catalogue's `source_sha`. Stability is not
availability or evidence: a `stable` table's capability can be OFF or answer
`SCO_UNAVAILABLE`, and no label says anything was run in game.
