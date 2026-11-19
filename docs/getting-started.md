# Start your first plugin

This page takes you from a published SDK archive to a checked, renamed C plugin
and a Windows deployment. You do not need a sco-core checkout.

<!-- release-block:current-release-brief -->
SDK **2.4.1** uses Star Citizen game pack **0.8.1** (checked against Star Citizen
Alpha 4.10.3, build 4.10.200.58746; see [game builds](../docs/game/builds.md)) and plugin ABI **1.1**. Archives and their digests are on the
[`sdk-v2.4.1` release page](https://github.com/scuba-sc/sco-core/releases/tag/sdk-v2.4.1); the [release notes](https://github.com/scuba-sc/sco-core/blob/sdk-v2.4.1/docs/releases/sdk-v2.4.1.md) list what changed.
<!-- /release-block:current-release-brief -->

Verify the archive against that release's `SHA256SUMS` (section 3). Older
releases have their own notes in the [`docs/releases`](releases/) folder.
What has and has not run in game is on the [game capability status](game/status.md) page.

## 1. Choose a language and archive

| Route | Start from | Additional requirements / next guide |
| --- | --- | --- |
| C (the walkthrough below) | `template/` | C11 compiler; [plugin ABI](api-v1.md) |
| C++ | `examples/cpp_hello/` | C++20 compiler; [C++ guide](sdk-cpp.md) |
| C# | `examples/cs_hello/` | .NET 8 SDK and NativeAOT toolchain; [C# setup and publishing](sdk-csharp.md). CMake does not build C# plugins. |
| Lua | `examples/greeter/` | Lua **5.4** for the standalone Lua checker; the product supplies its sandboxed runtime. [Lua guide](../sdk/docs/lua.md) |
| Data pack | `examples/travel_pack/` or `examples/quantum_pack/` (game extension) | No compiler; copy and edit the manifest and content. [Data-pack guide](../sdk/docs/data-packs.md) |

For C++, C#, Lua or data, download and verify the archive here, build the
checker (section 4), then follow that route's guide. Come back for deployment
and diagnostics (sections 6 and 7).

To start from a generated folder instead of a copied example, run the archive's
`tools/sco-new-plugin.py`. It writes a plugin with a `<id>.ping` command, an
optional `sco.widgets` panel (`--with-menu`) and optional service lookups
(`--services`). `--lang` takes `c`, `cpp`, `csharp` or `lua`; `--services` is
refused for Lua. The [SDK README](../sdk/README.md#generate-a-plugin-folder)
lists what each folder holds and how to check it.

Choose a profile from the archives on the release page. This walkthrough uses
the core archive. Use the combined archive if you need game services or the MCP
catalogue.

<!-- release-block:archives -->
| Archive | Choose it when |
| --- | --- |
| `sco-sdk-core-2.4.1.zip` | You need the neutral ABI, host services, native template and generic examples. |
| `sco-sdk-2.4.1.zip` | You also need Star Citizen service headers, DataCore or game data-pack examples: core plus the game extension in one archive, with the MCP discovery catalogue. |
| `sco-sdk-game-sc-0.8.1.zip` | Optional extension over its **exact matching** core archive (`sco-sdk-core-2.4.1.zip`), not a standalone SDK. Check `COMPATIBILITY.json.game-sc` and the required base digest before composing; equal version strings are insufficient. See [archive composition](../sdk/README.md#zips). |
| `sco-sdk-core-source-2.4.1.zip` | You are embedding or building the host kernel, not following the plugin template path. See [source-kit instructions](../sdk/README.md#build-the-kernel-source-kit). Do not overlay it with a plugin SDK. |
| `sco-sdk-host-source-sc-2.4.1.zip` | You are building the Star Citizen host source standalone, not a plugin. See the [host-source guide](../docs/host-source-sc.md). Do not overlay it with a plugin SDK. |
<!-- /release-block:archives -->

Unzipping does not check that a game overlay matches its core archive. If you
do not need to manage the two separately, use the combined archive.

To use the SDK with an AI agent through MCP, download the combined archive. The
core archive has no catalogue, guides or server. Then follow
[Set up the discovery server from the SDK kit](agent-integration.md#set-up-the-discovery-server-from-the-sdk-kit).

## 2. Install prerequisites

- **All command-line steps below:** Python 3 (`python3`; on Windows the
  launcher may be named `python`), CMake 3.20+, a C11 and C++20 toolchain (the
  SDK build configures both) and the GitHub CLI (`gh`) for the download. Or
  download the same two files in a browser. The archives hold headers and
  sources, not prebuilt game DLLs.
- **macOS:** Xcode Command Line Tools (Apple clang) and CMake. The x64 and
  ARM64 binaries are for local checking only.
- **Linux:** GCC 11+ or Clang 14+ with a C++20 standard library, and CMake. The
  binaries are for local checking only.
- **Windows game target:** Windows x64, Visual Studio 2019 16.11 or newer with
  **Desktop development with C++**, and CMake. Use the x64 Developer
  environment. With Visual Studio 2019, add `-A x64` to every `cmake -S` line
  below: the SDK refuses a 32-bit configure. See the
  [Windows build recipe](../sdk/README.md#build-the-examples). Do not deploy a
  macOS or Linux binary.
- **C# only:** .NET 8 SDK and the NativeAOT prerequisites. Publish for
  `win-x64`, as in the C# guide.
- **Lua only:** Lua **5.4** for the script checker. Lua and data routes need no
  plugin compilation, but building `sco-plugin-check` needs the native
  toolchain.

## 3. Download, verify, extract

Use an empty working directory. Run the commands in a shell that supports these
blocks (Git Bash works on Windows), or download and extract in a browser and
use the Windows CMake recipe above. The commands in sections 3 to 5 were last
run on 2026-11-11 on Windows x64 (Git Bash, Python 3, CMake, Visual Studio 2022)
against the SDK 2.4.0 core archive. They are the same for the 2.4.1 core archive.
Only the version in the archive name changes.

The script compares the archive with the `SHA256SUMS` file from the same
release, then extracts it.
In a browser, download both files from the stable release page for the SDK version you chose.

<!-- release-block:download-core-verify -->
```sh
env -u GITHUB_TOKEN -u GH_TOKEN gh release download sdk-v2.4.1 -R scuba-sc/sco-core -p 'sco-sdk-core-2.4.1.zip' -p SHA256SUMS
python3 - <<'PY'
import hashlib
from pathlib import Path
import zipfile
archive = Path("sco-sdk-core-2.4.1.zip")
entries = dict(line.split(None, 1)[::-1] for line in Path("SHA256SUMS").read_text().splitlines() if line.strip())
if hashlib.sha256(archive.read_bytes()).hexdigest() != entries[archive.name]:
    raise SystemExit("SHA256 mismatch: stop; do not extract or build")
if Path(archive.stem).exists():
    raise SystemExit("Extraction destination already exists; choose an empty directory")
with zipfile.ZipFile(archive) as sdk:
    sdk.extractall(".")
print(archive.name + ": SHA256 OK")
PY
```
<!-- /release-block:download-core-verify -->

Expected: the archive name followed by `: SHA256 OK`.

In a browser, download both files from the release page linked at the top. The
release-level `SHA256SUMS` covers the archive bytes. The file of the same name
inside the SDK covers the extracted files. A checksum downloaded next to an
archive detects corruption; it does not authenticate who built the archive. The
[release checklist](legal/release-checklist.md) describes how releases are
verified. Extract archives only from the project's release page.

## 4. Build the checker, copy and rename the template

From the same working directory, build the SDK, install it to get
`sco-plugin-check`, then copy and rename the template:

<!-- release-block:build-core-kit -->
```sh
SDK_DIR=sco-sdk-core-2.4.1.zip
SDK_DIR=${SDK_DIR%.zip}   # the folder the archive extracted
cmake -S "$SDK_DIR" -B sdk-build -DCMAKE_BUILD_TYPE=Release
cmake --build sdk-build --config Release
cmake --install sdk-build --config Release --prefix sdk-out
python3 - <<'PY'
from pathlib import Path
import shutil
shutil.copytree(Path('sco-sdk-core-2.4.1.zip').stem + '/template', 'first_plugin')
for name in ('plugin.c', 'plugin.ini', 'CMakeLists.txt'):
    path = Path('first_plugin') / name
    path.write_text(path.read_text().replace('my_plugin', 'first_plugin'))
PY
cmake -S first_plugin -B first-plugin-build -DSCO_SDK="$PWD/$SDK_DIR" -DCMAKE_BUILD_TYPE=Release
cmake --build first-plugin-build --config Release
cmake --install first-plugin-build --config Release --prefix first-plugin-out
```
<!-- /release-block:build-core-kit -->

### Name your plugin

The commands above rename the template to `first_plugin`. Pick your own id
later: 1 to 31 characters from `a-z`, `0-9` and `_`, not `sco`, `host`, `menu` or
`game`, and not a host built-in id. Keep the folder name, the manifest `id`,
`sco_plugin_info.name` and the command prefix the same. Renaming `my_plugin` in
`plugin.c`, `plugin.ini` and `CMakeLists.txt` changes all of them and the module
name.

The result is `first-plugin-out/data/plugins/first_plugin/`, holding `plugin.ini`
and `first_plugin.dll`, with the command `first_plugin.ping`. The module is
always named `<id>.dll`, also on macOS and Linux, where it is a native binary for
that system and not a Windows DLL.

## 5. Check and invoke it without the game

```sh
sdk-out/bin/sco-plugin-check first-plugin-out/data/plugins/first_plugin --invoke first_plugin.ping
```

The output includes:

```text
invoke  first_plugin.ping -> ok "pong"
OK: 0 failure(s)
```

Exit code 0 means every check passed. The checker reads `plugin.ini`, loads the
module, runs its commands and unloads it against a stand-in host. By default
every capability reads as missing; `--cap NAME` fakes one. The stand-in has no
game, sandbox or crash guard, so a pass says nothing about gameplay. Not run in
game.

## 6. Deploy on Windows

Host startup and in-game behavior depend on the exact product build. sc-offline
0.8.0 runs the process-retained composition ([game host](game/host.md)). With it,
the game services start, native plugins load, and every loaded plugin stays
mapped until the game process exits. On Stop the host closes admission and
sends `game.exit` once. It unloads nothing, so a plugin you rebuild loads again
only at the next game start. A player sees no refusal of native plugins. The
strict composition, which refuses native plugin activation, is the SDK default
and is not what sc-offline 0.8.0 uses. If you build sc-offline against an older
Core, native startup is refused and no plugin loads; the log says so. Beyond
startup runs, none of this has been run in a game session.

1. Use a plugin built and checked on **Windows x64**. If you worked on macOS or
   Linux, repeat sections 4 and 5 on Windows with your renamed template.
2. With the game stopped, copy the whole `first_plugin` folder from the install
   output into sc-offline's `data\plugins\`. Confirm that
   `data\plugins\first_plugin\plugin.ini` and its module are together.
3. Set `plugins = on` in `sc-offline.ini` (the product default). An owner can
   turn loading off.
4. If the product starts its plugin host, look in `data\mod.log` for the
   plugin's load or refusal. A missing log does not mean the plugin loaded. The
   [loader rules](plugins.md) say what each state means.
5. The template registers `first_plugin.ping` when it loads. The `pong` in
   section 5 came from the checker's own caller. Whether your sc-offline build
   lets a player enter a command depends on that build, so give the plugin its
   own control, such as a hotkey or a `sco.widgets` panel. Commands run on the
   game thread. Record the product build and the game build with any result you
   report.

Install only plugins you trust: native code has the game's full rights, and
there is no recovery guarantee after a native fault. Follow the [plugin rules](../sdk/docs/plugin-rules.md),
including offline-only use and no anti-cheat bypass. A `faulted` provider needs
a game restart; do not keep calling its cached service tables. To disable a
plugin, put an empty file named `disabled` in its folder.

## 7. Diagnose common failures

| Symptom | Check next |
| --- | --- |
| Checksum mismatch | Stop. Download both files again from the same release. Do not extract or build mismatched bytes. |
| CMake cannot find the SDK include or helper | Pass the absolute path of the extracted SDK folder as `SCO_SDK`. Do not point at the zip or at a core-source kit. |
| "64-bit only" configure error | On Windows with Visual Studio 2019, add `-A x64` to the configure line. |
| C++ toolchain or configure failure | Install the C++20 toolchain even for the C walkthrough: the SDK build configures C and C++. |
| `entry` missing, refused id or manifest error | Check the installed folder (not a source-only copy), the exact id and module name, and the [manifest rules](../sdk/docs/plugin-ini.md). Built-in ids cannot be reused. |
| Native module cannot load | Match OS and architecture. Rebuild for Windows x64 before game use. Check exports and dependencies. |
| `unavailable`, or an absent capability or service | Query presence, version, the complete member extent and non-null callbacks. See [ABI compatibility](api-v1.md#compatibility) and [contracts](contracts.md). |
| `off`, `disabled` or `refused` | Check `plugins = on`, the `disabled` marker, dependencies and the logged reason. |
| `crashed`, `faulted` or a reload refusal | Read [fault and hot-reload rules](plugins.md#crash-containment). Restart the game when it says so. |
| Lua checker or version trouble | Use Lua 5.4 and the dedicated checker from the [Lua guide](../sdk/docs/lua.md). The native checker does not run scripts. |

## 8. Where next

- [Designing modular plugins](plugin-design.md): one feature per plugin, sharing
  through service tables, `requires` and clean unload.
- [Plugin ABI reference](api-v1.md) and [contract catalogue](contracts.md):
  declarations, service presence, buffers, threads and lifetimes.
- [C++](sdk-cpp.md), [C#](sdk-csharp.md), [Lua](../sdk/docs/lua.md) and
  [data packs](../sdk/docs/data-packs.md).
- [Game services](game-services.md) and [game capability status](game/status.md).
- [Agent and IDE integration](agent-integration.md): read-only MCP discovery. The
  default tools do not compile, run plugins or connect to the game; opt-in tools
  build and check on your machine.
- [Documentation index](README.md).
