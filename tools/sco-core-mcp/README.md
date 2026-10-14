# sco-core SDK discovery MCP server

Development-only, read-only discovery of the public sco-core SDK contracts through
MCP over stdio. The server uses Python 3's standard library and a generated SDK
catalogue. It does **not** connect to a running host or game, invoke SDK methods,
compile plugins, execute examples, or modify the repository. It is development
tooling, not an SDK runtime component.

[Separate default-off local runtime launcher](../sco-core-mcp-runtime/README.md)
implements the fixed runtime tool surface. This discovery executable remains
credentials-free and catalogue-only, with its original six tools/dependencies.
The current task supersedes the historical same-server placement in the
[runtime design](../../docs/design/runtime-mcp.md). No live runtime/game acceptance is claimed.

The contract authority is [`docs/contracts.md`](../../docs/contracts.md), with
per-method records in [`sdk/contracts/`](../../sdk/contracts/). The server exposes
that evidence; it does not replace the contract's reading rules or remaining gaps.

## Copyable SDK consumer tutorial

[SDK → MCP → tutorial-owned plugin](../../docs/sdk-mcp-tutorial.md) uses this
static six-tool launcher, owned NPC/inspector copies, an isolated archive build,
the checker and a named dynamic fixture. Runtime observations remain optional
and default-off; static discovery never certifies gameplay or performs reload.

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

## Using an extracted SDK archive

The published SDK archives do not contain a catalogue, and none will be added to
the immutable 2.1.0 release assets. An archive user obtains one in either of two
ways:

- **Public catalogue.** Download
  <https://scubamount.github.io/sco-core/sdk-catalogue.json> and check it before
  use. The snapshot current for SDK 2.1.0 has sha256
  `29bb24799dbef817a39aa0919fd456bd641f035f1768ac566cab9eec6e735a85`, with
  `sdk_version` `2.1.0`, `game_pack_version` `0.4.0`, `source_sha`
  `77c328fc088f85336f1241c0c054bc461fe8b64c`, 167 records and 30 tables:

  ```sh
  curl -fsSLO https://scubamount.github.io/sco-core/sdk-catalogue.json
  shasum -a 256 sdk-catalogue.json   # Linux: sha256sum; PowerShell: Get-FileHash
  ```

  The site is redeployed from newer sources, so a later download can carry a
  different hash and coordinates. Compare the file's `sdk_version` with your
  archive's `VERSION`. Its `source_sha` names the commit the catalogue was built
  from, which is not necessarily the release tag's commit; it is a supplied
  identifier, not a signature.
- **Source build.** Check out the matching sco-core source and run the catalogue
  build above. This needs the same compiler environment as any other build.

Then pass `--repo-root` the archive's single top-level directory (for example the
extracted `sco-sdk-core-2.1.0`), not its parent:

```sh
python3 tools/sco-core-mcp/server.py \
  --catalogue /absolute/path/to/sdk-catalogue.json \
  --repo-root /absolute/path/to/sco-sdk-core-2.1.0
```

Catalogue example paths are repository-relative (`sdk/examples/hello/hello.c`),
while the core and legacy archives store `examples/hello/hello.c`. `get_example`
always tries the recorded path first. When it is absent **and** the root has no
`sdk` entry, the leading `sdk/` is dropped and the remainder is opened under the
same containment and no-symlink rules. A root that has an `sdk` entry (a
checkout, or the `core-source`/`host-source` archives, which keep repository
layout) is read only at recorded paths. A file missing there is reported as
unavailable, never served from the other layout. Results still report the
recorded path.

## Tools and explicit allowlist

These are the complete six tool names. Enable them explicitly, not with a wildcard:

| Tool | Purpose |
|---|---|
| `search_api` | Search catalogue contracts, with optional service and language filtering. |
| `get_symbol` | Retrieve a symbol's contract and its source-backed language mappings. |
| `list_services` | Discover public service tables, versions, sizes and member counts. |
| `get_example` | Return a selected record's listed example paths, and optionally their bounded contents. |
| `get_capability_status` | Read documented capability evidence, limitations and the record's [evidence state](#evidence-state); not a live availability probe. |
| `check_compatibility` | Compare requested coordinates with catalogue metadata, read a [release approval record](#release-approval-records) and check a service's minimum version and member extent; not a runtime compatibility test. |

Inspect `tools/list` for each tool's input schema. Language values are `c`, `cpp`,
`csharp`, and `lua`; use those tokens rather than display names. `search_api` accepts
at most 512 characters in `query`, a `limit` from 1 to 50 (default 10), and an
optional `service`. `check_compatibility` accepts `sdk_version` as a string and
`game_pack_version` as a string matching the catalogue (for example, `"0.3.0"`).
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

The input schema advertises the patterns and `dependentRequired`. Arguments
outside it, a malformed value, `min_version`/`member_extent` without `service`,
or an unknown service are refused as tool errors. `service_table` describes the
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
  The only other files read under that root are `docs/game/status.md` (once, at
  start) and the `sdk-v*.json`/`.json.sig` records in `docs/legal/release-decisions/`
  (per `check_compatibility` call), using the same no-symlink containment and size limit.
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
game-pack records, from `docs/game/status.md` under `--repo-root`. The catalogue
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

Game-pack records also carry `service_status`. It is **service-scoped**: rows of the
`| Table | Version | Capability | Default | Evidence |` table in
`docs/game/status.md` whose backticked service and linked header both match the
record's catalogue table. Each row keeps `capability`, `default` (`ON`, `OFF` or
`none`) and `evidence` verbatim, plus an `evidence_level`:
`row_resolved_sigcheck` (with `game_build`), `off_pending_in_game_confirmation`,
`blocked`, or `unclassified` for any other text, including in-game claims.
`documented_version` and `version_matches_catalogue` compare the row's version with
the catalogue. `state` is:

- `rows_found`: matching rows exist. They describe capabilities of the service, not
  this method.
- `no_rows`: the table has no row for this service. Some services are described
  only in prose.
- `unavailable`: there is no `--repo-root`, the file is missing or a symlink, or
  the table is malformed. Any row with the wrong cell count, an unknown default
  or a missing separator refuses the whole table rather than half-reading it.

The status document is read from the configured root when the server starts. It
is not bound to the catalogue's `source_sha`. Its sigcheck rows are dated to the
build named in each row, and none of it is evidence that an operation worked in
a game session.
