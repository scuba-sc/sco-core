<!-- SPDX-License-Identifier: GPL-3.0-only -->
# SDK reference launcher

A small Rust launcher that shows what a launcher does with the sco-core SDK and where the SDK stops.
It reads and edits a launcher folder: status, plugin list, enable and disable, verified install of a
plugin from a local zip, the capability switches, and a read-only MCP server for AI agents.

It does **not** start the game, inject anything, change the PC (firewall, hosts file, registry,
anti-cheat files), use the network or update itself. Those are product duties, listed in
`docs/building-a-launcher.md`. Not run in game. Every check here is a source-level or synthetic
test. See [game capability status](../../../docs/game/status.md).

The contract between a launcher and the host kit is in
`docs/launcher-contract.md`.

## Build and run

```sh
cargo build --release          # Rust 1.99.0 (rust-toolchain.toml); crates come from crates.io
cargo test
target/release/sco-reference-launcher --root ./my-folder status
```

Use `cargo build --release --locked --offline` once the dependencies are in your Cargo cache. The
binary is `sco-reference-launcher` (`.exe` on Windows).

| Command | What it does |
|---|---|
| `status` | Folder, settings file, plugin counts, capability count. Read-only. |
| `plugins list` | Plugin folders and whether each is enabled. |
| `plugins enable <id>`, `plugins disable <id>` | Remove or create the plugin's `disabled` marker. |
| `plugins install-from-zip <zip> [--replace] [--disabled]` | Verify the zip, then install it. |
| `capabilities` | The six switches, their state and evidence level. |
| `capabilities env` | The environment block a host would get. |
| `capabilities enable <key>`, `capabilities disable <key>` | Edit an `exp_*` key in the settings file. |
| `mcp` | MCP server on stdin and stdout: status, plugins, logs, settings. |

`--root <folder>` picks the launcher folder; otherwise `SCO_REF_ROOT`, otherwise the folder of the
program.

## Make it your product

Everything a product chooses is one `ProductConfig`. See
[building a launcher](../../../docs/building-a-launcher.md#make-it-yours).

## Crates

| Crate | Holds |
|---|---|
| `scl-contract` | `ProductConfig`, switch table, event names, file names, settings key format. |
| `scl-config` | Settings ini reader and in-place editor, capability switches, environment block, `plugin.ini` reader. |
| `scl-plugins` | Zip verifier and `plugin.ini` manifest parser (same rules as the SDK Python verifier). |
| `scl-install` | Folder layout, plugin list, enable and disable, verified install, status. |
| `scl-catalogue` | Signed plugin catalogue client: index verification, policy, install plan. No network code; the CLI does not use it yet. |
| `scl-redact` | Log redactor used by the MCP server. |
| `scl-mcp` | Read-only MCP server. |
| `scl-cli` | The command line. |
