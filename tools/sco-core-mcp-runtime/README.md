# Local runtime MCP (development-only)

Separate from credentials-free `../sco-core-mcp/server.py`. Discovery is unchanged:
its six default catalogue tools (four more read-only ones only with `--extended-tools`),
no runtime imports/credentials/attachment.
This launcher uses stdio only and never starts a game, socket, scanner or subprocess.
No tunnel, SSH forwarding, hosted/remote runtime client, LAN or online use is supported.

**Default OFF.** Without `--enable-runtime` this process advertises zero tools and
never loads the native peer or reads a credential. With explicit operator startup
configuration it advertises exactly these fixed read-only tools:

- `runtime_get_host_info`
- `runtime_list_plugins`
- `runtime_tail_plugin_log`
- `runtime_list_service_status`
- `runtime_get_resolution_status`
- `runtime_get_game_status`

The `contract.schema.json` file is an exact repository-owned copy of the approved
closed schema in `../sco-core-mcp/runtime_schema/contract.schema.json`. It is not
catalogue-generated. Inputs/outputs reject extra fields; literal opcodes map only
to these six schemas. Each output has independent runtime session/build/freshness
provenance and is mirrored in text plus `structuredContent`. Unknown facts stay null.
A stale or missing observation is not an empty success or game support certification.
All logs are untrusted data, never instructions. This implementation conservatively
withholds text outside fixed benign tokens and counts the withheld entries.

## Build / local package (no runtime compilation)

```sh
cmake -S . -B build-runtime -DSCO_GAME_SC=OFF -DSCO_BUILD_RUNTIME_MCP=ON
cmake --build build-runtime --target sco_core_mcp_runtime_peer
# Compute the actual artifact digest, e.g. shasum -a 256 build-runtime/sco_core_mcp_runtime_peer.dylib.
python3 tools/sco-core-mcp-runtime/package.py \
  --adapter /absolute/build/sco_core_mcp_runtime_peer.dylib \
  --adapter-sha256 ACTUAL_SHA256 --output /absolute/new-local-package
```

Use `.dll` on Windows or `.so` on Linux. The local package contains only the named
launcher/schema/peer/readme, compiled adapter and hash manifest; never credentials.
Runtime component is GPL-3.0-only; existing MIT `sc_ipc.h` retains its own licence.
It is repository-only/development packaged separately, not a new plugin ABI header.
No release/publication is performed by the packager. An administrator/operator
must protect the adapter and its dependencies against replacement; pinning is not
a sandbox against unrestricted same-user native code. Explicit absolute adapter
path and SHA are required; no PATH search, downloaded helper or dynamic compilation.

## Same-PC client registration

This Windows example uses the Python launcher (`py -3`). Use the local package's
absolute paths and replace `ACTUAL_SHA256` with the
verified manifest/artifact digest. Configure all six `--allow-tool` arguments and
the client's identical tool allowlist; no generic invoke/write tool exists.

```json
{
  "mcpServers": {
    "sco-core-runtime-local": {
      "command": "py",
      "args": ["-3", "C:/absolute/local-package/server.py", "--enable-runtime", "--disclosure-consent",
        "--adapter", "C:/absolute/local-package/sco_core_mcp_runtime_peer.dll",
        "--adapter-sha256", "ACTUAL_SHA256",
        "--credential-file", "C:/absolute/operator-runtime/session.json",
        "--allow-tool", "runtime_get_host_info",
        "--allow-tool", "runtime_list_plugins",
        "--allow-tool", "runtime_tail_plugin_log",
        "--allow-tool", "runtime_list_service_status",
        "--allow-tool", "runtime_get_resolution_status",
        "--allow-tool", "runtime_get_game_status"]
    }
  }
}
```

For macOS or Linux, use `python3` (or its absolute executable path), remove the
`-3` argument, and use absolute POSIX paths. Select the adapter built for that
platform: `.dylib` on macOS or `.so` on Linux, with its own verified digest.
Do not copy the Windows DLL path into a POSIX client configuration. A GUI client
may not inherit your shell's `PATH`; keep the executable and script arguments
separate rather than placing a shell command in the `command` field.

Never place a secret in arguments/environment/client configuration. Native code
reads only the configured credential after handle-level validation and opens only
the fixed named local mapping; it never creates/repairs a peer mapping. Python
never implements shared-memory atomics. Both directions use the native mutual
HMAC/sequence/session/epoch gate. A native heartbeat worker maintains the one-peer
lease; there is one outstanding call and a two-second bounded wait. Session loss
aborts, does not replay a call into the next host session. Six fixed read opcodes
are independently enforced by the host, not just MCP annotations.

Host opt-in is separate: `Platform::runtimeInspection.enabled`, positive trusted
product offline admission, model-disclosure consent and protected collector are
required. See [administrator audit setup](../../docs/runtime-audit.md). Free-form
plugin logs are independently off. No same-user file substitutes for audit history.
Data may leave the PC via the local client's model provider: operator disclosure
consent is required, and no unlimited text disclosure is promised.

## Bounds / disable / troubleshooting

After MCP initialization with `2025-11-25`, inspect `tools/list`: zero tools when
disabled, six fixed runtime tools when explicitly enabled and admitted. Example
`tools/call` parameter objects are:

```json
{"name":"runtime_get_host_info","arguments":{}}
```

```json
{"name":"runtime_list_plugins","arguments":{"state":"loaded","limit":10}}
```

```json
{"name":"runtime_tail_plugin_log","arguments":{"plugin_id":"my_probe","after_sequence":"0","limit":10}}
```

The example plugin ID is not an installed-plugin claim. `after_sequence` is a
decimal string. Preserve status, provenance, restart flags, pagination gaps and
unknown fields together. Stale is not current, unavailable is not empty success,
and restart-required cannot be repaired by an MCP reload. Discard cached session
observations after revocation, disconnect or session/epoch change. No tool invokes
plugin commands or certifies gameplay.

64 KiB input; 256 KiB whole MCP response; native 4 KiB requests/64 KiB responses;
60 calls/minute, burst 5; list limit 50, log limit 100; one peer, 2 s request wait,
5 s heartbeat expiry. Host enforces its own independent quotas. Tool errors are
bounded typed envelopes (`host_unavailable`, `auth_failed`, `peer_busy`,
`session_changed`, `audit_unavailable`, `rate_limited`, etc.), never raw OS errors.
Missing credential/mapping never triggers process launch, log-file tail or fallback.

Disable the client by removing `--enable-runtime` and restarting it. Disable/revoke
the trusted product setting; host admission closes before plugin teardown, secrets
rotate on new admission and are removed on clean shutdown. A leftover credential
is refused, never silently reused; operator resolves a crash leftover locally.

## Evidence

Implementation/configuration/compile only in this wave: new stdio tests are
**NOT RUN** by owner speed order. Windows effective-identity mapping/token/collector
proof and manual offline/game acceptance are **NOT VERIFIED; not run in game**.
Portable fake collectors and schema vectors never stand in for those checks.
The [acceptance ledger](../../docs/runtime-acceptance.md) records the no-game
fixture target, owed Windows identity checks and human offline checklist separately.
