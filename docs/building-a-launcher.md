# Building a launcher

How to build a launcher for a product that runs the sco-core host kit, from the SDK download to a running reference launcher that installs a plugin from a zip and talks to an AI agent. It is written so a person who did not write it can follow it on a clean machine.

Status: source level and synthetic tests only. **Not run in game.** The reference launcher starts no game, injects nothing, changes nothing on the PC outside its own folder, uses no network and does not update itself. The contract it follows is in [launcher contract](launcher-contract.md).

## 1. Download and verify the kit

Take the kit from the sco-core release page, `sco-sdk-core-<version>.zip` for the game-neutral kit, plus `sco-sdk-core-source-<version>.zip` if you will build a host, or `sco-sdk-host-source-sc-<version>.zip` for the Star Citizen host. Also download `SHA256SUMS` from the same release. The SDK 2.4.1 line carries the reference launcher.

Check every archive against `SHA256SUMS` before you unpack it:

```sh
# Linux, macOS, Git Bash
sha256sum --check --ignore-missing SHA256SUMS
```

```powershell
# Windows PowerShell: compare each value with its line in SHA256SUMS
Get-FileHash .\sco-sdk-core-2.4.1.zip -Algorithm SHA256
```

Unpack each archive into its own new, empty folder. The reference launcher is in `examples/launcher/` of the core kit (`sdk/examples/launcher/` in the repository).

A matching hash says the bytes are the release bytes. It is not a signature on the publisher and it says nothing about behaviour in game.

## 2. What the SDK gives you

### A launcher

| Piece | Where | What it does |
|---|---|---|
| The contract | [launcher contract](launcher-contract.md) | Variables, folder layout, markers, settings keys, events, package rules |
| Reference launcher | `examples/launcher/` | Rust crates and a small CLI you can copy and rename |
| Plugin verifier | `tools/sco-plugin-verify.py`, crate `scl-plugins` | Checks a plugin zip without extracting it |
| `plugin.ini` rules | `docs/plugin-ini.md` | The manifest your launcher lists and your players edit |
| Capability table | crate `scl-contract` | The six switches, their keys and their evidence level |
| Skills | `skills/sco-build-launcher`, `skills/sco-package-plugin` | Step lists for an AI agent doing this work |

### A host

The host is what runs inside the game process. It is C++ and comes from the source kits.

| Piece | What it does |
|---|---|
| `sco::app::Start`, `Tick`, `Stop` | One startup and shutdown sequence: log, signatures, capabilities, `sco_api`, built-in plugins, discovered plugins, content index, `game.ready`, then `game.exit` |
| `sco::app::Platform` | The few things only the product knows: version text, data root, whether plugins are on, built-ins, script runtime, image and resolver owners, the quiescence attestation |
| Plugin loader and discovery | Reads `data/plugins/<id>/plugin.ini`, honours the `disabled` marker, checks api, `requires` and entry |
| Services and storage | `sco.storage`, `sco.settings`, `sco.ui` and the game services behind the capability switches |
| `sco-host-sim` | The real host kit outside the game, for tests and for trying a plugin folder |

## What a product brings itself

The SDK deliberately does not provide these. This list is the **launcher seam**: everything on it is the product's work and none of it is in the reference launcher.

| Duty | Why it stays with the product |
|---|---|
| **Injection** | Getting your `dinput8.dll` (or another loader) into the game process, and keeping its export table correct, is specific to the game and to your risk decisions. sco-core ships no loader and no forwarder |
| **Offline patches** | Any change that makes the game run without its services is a product decision with legal and account consequences. sco-core has no such patch |
| **Admission** | Deciding that the process is in a state where reading or writing game memory is allowed (offline, anti-cheat absent, build recognised) and supplying the admission inspector the host asks for |
| **Quiescence attestation** | `Platform::gameReadersQuiesced` must be non-null and truthful before the host installs native hooks. Only the product knows when the engine is quiet |
| **Menu renderer** | The host builds a frontend model (tabs, panels, controls). Drawing it, with any UI library, is the product's. No UI library crosses the plugin ABI |
| **Starting the game** | Process creation, command line, elevation, working folder, anti-cheat handling |
| **PC changes** | Firewall rules, hosts file lines, file renames. The reference launcher makes none and says so |
| **Updating** | Where updates come from, how they are signed, how a swap is rolled back |
| **Branding, locale, default pack set, release packaging** | Product identity |

Anything you add from this list changes the PC or the game process. Write down what it changes, ask for the owner's go-ahead before you run it, and record what you observed in game separately from what you built.

## 3. Build and run the reference launcher

Requirements: Rust 1.99.0 (`rust-toolchain.toml` pins it) and, on Windows, the MSVC build tools. The crates come from crates.io; run the first build with network access, then `--offline` works from the Cargo cache.

```sh
cd examples/launcher            # sdk/examples/launcher in the repository
cargo build --release
cargo test
```

`cargo build --release --locked --offline` repeats the build exactly from `Cargo.lock` once the cache is filled. The program is `target/release/sco-reference-launcher` (`.exe` on Windows). It is a static Windows build; on Linux and macOS it builds and runs for development.

Make a launcher folder and look at it:

```sh
mkdir my-launcher
target/release/sco-reference-launcher --root my-launcher status
target/release/sco-reference-launcher --root my-launcher capabilities
target/release/sco-reference-launcher --root my-launcher capabilities env
```

`status` writes nothing. A missing settings file or data folder is reported as a state, and the defaults apply: plugins on, five capability switches on, `exp_entities_diag` off.

Change a switch, which edits one line of `my-launcher/reference-launcher.ini` and keeps the rest of the file:

```sh
target/release/sco-reference-launcher --root my-launcher capabilities disable exp_audio_native
```

### Make it yours

Everything a product chooses is one `ProductConfig` in `crates/scl-contract/src/lib.rs`: name, slug, environment prefix, settings file name, data folder, default packs, update source and version. Edit `REFERENCE_PRODUCT` (or build your own and call `scl_cli::run`), rename the binary in `crates/scl-cli/Cargo.toml`, and keep the slug short enough for the event name limit in the contract. Do not change the switch names, the plugin folder rules or the settings key format: the host kit fixes them.

## 4. Add a plugin from a zip

Package the plugin as one zip with the plugin folder at the root (`pilot/plugin.ini`, `pilot/pilot.dll`). To make one by hand from a staged folder:

```sh
cd staging                      # holds the folder pilot/
zip -r ../pilot.zip pilot       # or: python -m zipfile -c ../pilot.zip pilot
```

Verify with the SDK's own tool first, from the unpacked kit root, using the checker from the same kit:

```sh
python3 tools/sco-plugin-verify.py /path/to/pilot.zip --checker out/bin/sco-plugin-check
```

Then install with the launcher, which runs its own verifier again before it writes anything:

```sh
target/release/sco-reference-launcher --root my-launcher plugins install-from-zip /path/to/pilot.zip
target/release/sco-reference-launcher --root my-launcher plugins list
target/release/sco-reference-launcher --root my-launcher plugins disable pilot
target/release/sco-reference-launcher --root my-launcher plugins enable pilot
```

What install does, in order: verify the archive (structure, names, sizes, hashes, `plugin.ini`), check the id and the plugin api against this launcher, read every file and compare it with the verifier's report, write the files into `data/plugins/.install-<id>`, then rename the folder into place. A refused zip leaves nothing behind. `--replace` replaces an installed copy and keeps its `disabled` choice. `--disabled` installs the plugin switched off.

A passing install means the package is well formed and fits this launcher. It does not say the plugin works with the host, and native plugins are code that will run inside the game process: install only what you trust. Whether a plugin loads is decided by the host at the next game start.

## 5. Connect an AI agent through `mcp`

`mcp` is a read-only MCP server on standard input and output. It offers four tools and nothing else:

| Tool | Returns |
|---|---|
| `get_status` | Launcher version, folder, whether the settings file and data folder exist, plugin counts, which capability switches are on |
| `list_plugins` | Plugin folders with enabled state and the manifest fields |
| `read_log` | The last lines of `mod`, `launcher` or `launcher_previous`, redacted |
| `get_settings` | The settings file as section, key and value entries |

It cannot start the game, install, update, enable or disable a plugin, or change a setting, and it opens no network connection. The game's own log is not a choice. Paths under the user's profile are shown as `%USERPROFILE%`, e-mail addresses, account names and identity values are redacted, and a person should still read a log before sharing it.

Register it with an MCP client by command and arguments. A JSON configuration looks like this:

```json
{
  "mcpServers": {
    "my-launcher": {
      "command": "C:\\tools\\sco-reference-launcher.exe",
      "args": ["--root", "C:\\tools\\my-launcher", "mcp"]
    }
  }
}
```

With Claude Code: `claude mcp add my-launcher -- C:\tools\sco-reference-launcher.exe --root C:\tools\my-launcher mcp`.

Try it without a client: `printf '{"jsonrpc":"2.0","id":1,"method":"tools/list"}\n' | sco-reference-launcher --root my-launcher mcp`. Diagnostics go to standard error; standard output carries only protocol lines.

To write plugins with an agent, use the SDK's own MCP server (`tools/sco-core-mcp`, see `docs/agent-integration.md` in the repository). The launcher server only reads the launcher folder.

## 6. Test the pieces

| Check | Command |
|---|---|
| Reference launcher | `cargo test` in `examples/launcher` |
| A plugin folder in the real host kit, outside the game | `sco-host-sim <plugin root> --ticks 3` from the host source kit build |
| A plugin zip | `tools/sco-plugin-verify.py` |
| Capability behaviour | Not covered by anything here. Each switch is synthetic evidence only until the owner records a session |

## 7. Checklist for a new product

1. Pick a `ProductConfig` and rename the binary.
2. Decide how your `dinput8.dll` receives the launcher's variables, and read the capability switches from the process environment as the contract says.
3. Fill `sco::app::Platform` from your own startup code and call `Start` on the game thread after your patches, `Tick` from your main-thread hook and `Stop` from the game's quit path.
4. Build the admission inspector and the quiescence attestation, and write down what each one checks.
5. Build the menu renderer over the host's frontend model.
6. Add starting, PC changes and updating only with the owner's approval, and document each change to the PC.
7. Keep a line in your release notes that says what was run in game and what was not.
