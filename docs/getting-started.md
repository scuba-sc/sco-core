# Start your first plugin

This is one complete path from a published SDK archive to a checked, renamed C
plugin, then Windows game deployment. You do not need a sco-core checkout.

SDK **2.4.0 RC2** uses Star Citizen game pack **0.8.0** and plugin ABI **1.1**
(unchanged since 2.3.0). The [RC2 release page](https://github.com/scuba-sc/sco-core/releases/tag/sdk-v2.4.0-rc.2)
is the latest published release. Its tag points to source commit
`844e136ccb280ad8cb43f73bc43c9447e8f6799d`; later `main` commits do not change the
released archive bytes. RC1 examples and checksums below are historical and apply
only to RC1. Verify the selected archive against that release's `SHA256SUMS` and
check the kit and host identities separately. Nothing new in 2.4.0 has been run
in game.

## 1. Choose a language and archive

| Route | Start from | Additional requirements / next guide |
| --- | --- | --- |
| C (the walkthrough below) | `template/` | C11 compiler; [plugin ABI](api-v1.md) |
| C++ | `examples/cpp_hello/` | C++20 compiler; [C++ guide](sdk-cpp.md) |
| C# | `examples/cs_hello/` | .NET 8 SDK and NativeAOT toolchain; [C# setup and publishing](sdk-csharp.md). CMake does not build C# plugins. |
| Lua | `examples/greeter/` | Lua **5.4** for the standalone Lua checker; the product supplies its sandboxed runtime. [Lua guide](../sdk/docs/lua.md) |
| Data pack | `examples/travel_pack/` or `examples/quantum_pack/` (game extension) | No compiler; copy/edit the manifest and content. [Data-pack guide](../sdk/docs/data-packs.md) |

For C++, C#, Lua or data, follow that route's canonical guide after downloading
and verifying the archive; return here for deployment and diagnostics. The
copy/build/check commands below deliberately choose **C**, not a universal
recipe for every language.

To start from a generated folder instead of a copied example, the archive's
`tools/sco-new-plugin.py` writes one with a `<id>.ping` command, an optional
`sco.widgets` panel (`--with-menu`) and optional service lookups
(`--services`): `--lang c` or `cpp` since SDK 2.2.0, and since SDK 2.4.0 also
`csharp` (a NativeAOT project whose `README.md` gives the `dotnet publish`
line) and `lua` (`plugin.ini` and `main.lua`; Lua has no service lookup, so
`--services` is refused there). The
[SDK README](../sdk/README.md#generate-a-plugin-folder) lists what each
language's folder holds and how to check it. A generated plugin that passes the
checkers has not been run in game.

<a id="sdk-240-release-candidate-1-assets"></a>

### Historical SDK 2.4.0 release candidate 1 assets

Every archive below is attached to the `sdk-v2.4.0-rc.1` release, together with
`SHA256SUMS` and one `<archive>.spdx.json` SBOM per archive. The SHA-256 column
repeats that release's `SHA256SUMS`.

| Archive | Choose it when | SHA-256 |
| --- | --- | --- |
| `sco-sdk-core-2.4.0.zip` | You need the neutral ABI, host services, native template and generic examples. Used below. | `68ca5ef213fe9be162b974dcb798a6da2402583daf369e26b26634b23d869f7d` |
| `sco-sdk-2.4.0.zip` | You also need Star Citizen service headers, DataCore, or game data-pack examples. Simplest game-specific choice: combined core + game extension; it also bundles the MCP discovery catalogue. | `124751534277b89f0516af1897919c3485923811596e159a1f683bb80b1c243b` |
| `sco-sdk-game-sc-0.8.0.zip` | Optional extension over its **exact matching** core archive (`sco-sdk-core-2.4.0.zip`), not a standalone SDK. Check `COMPATIBILITY.json.game-sc` and the required base digest before composing; equal version strings are insufficient. See [archive composition](../sdk/README.md#zips). | `fafd2d2d4d86cf479c25a4623e94c7004b45e527e94dceb8354293c6a6b5a8f9` |
| `sco-sdk-core-source-2.4.0.zip` | You are embedding/building the host kernel, not following the plugin template path. See [source-kit instructions](../sdk/README.md#build-the-kernel-source-kit). Do not overlay it with a plugin SDK. | `f2f7c35591ed96090240466910e3db408953a9a4f6e0a16a36da83bc2a34ee62` |
| `sco-sdk-host-source-sc-2.4.0.zip` | You are building the Star Citizen host source standalone, not a plugin. See the [host-source guide](host-source-sc.md). Do not overlay it with a plugin SDK. | `5ca76ff9dac6bbaa183c6f308d610e08d9c1b0daf6ebda6458e222277406dfc6` |

Raw unzip does not validate an overlay's compatibility. If you do not need to
manage the core/overlay descriptors separately, use the combined archive.

To use the SDK with an AI agent through MCP, download `sco-sdk-2.4.0.zip` instead of
the core archive, which has no catalogue, guides or server, and follow
[Set up the discovery server from the SDK kit](agent-integration.md#set-up-the-discovery-server-from-the-sdk-kit).

## 2. Install prerequisites

- **All command-line steps below:** Python 3, CMake 3.20+, a C11/C++20
  toolchain (the SDK example build enables both), and GitHub CLI (`gh`) for
  release download. Alternatively download the same two files in the browser.
  The published archives contain headers/sources, not prebuilt game DLLs.
- **macOS:** Xcode Command Line Tools (Apple clang) and CMake. x64 and ARM64
  builds are for local development/checking only.
- **Linux:** GCC 11+ or Clang 14+ with a C++20 standard library, plus CMake.
  These binaries are likewise local development/checker binaries.
- **Windows game target:** Windows x64, Visual Studio 2019 16.11+ (or newer)
  with **Desktop development with C++**, and CMake. Use the x64 Developer
  environment. See the [Windows build recipe](../sdk/README.md#build-the-examples)
  for generator/configuration selection; do not deploy a macOS/Linux binary.
- **C# only:** .NET 8 SDK plus platform NativeAOT prerequisites; Windows
  game publication must target `win-x64`, as in the C# guide.
- **Lua only:** the standalone script checker requires Lua **5.4**, not an
  arbitrary installed Lua version. Lua and data routes need no native plugin
  compilation, but building `sco-plugin-check` still needs the native toolchain.

## 3. Download, verify, extract

Use an empty working directory. The verify, extract, build and check commands
in sections 3 to 5, including the `gh release download` line, were executed on
2026-10-21 on Windows x64 (Git Bash, Python 3, CMake, Visual Studio 2022)
against the release candidate 1 core archive and its release `SHA256SUMS`.
Earlier revisions of this page ran the same steps on macOS. On Windows, use a
shell that supports these shell blocks (Git Bash works), or perform the
download/extraction in the browser and use the linked Windows CMake recipe.

### Download the published SDK 2.4.0 release candidate 2

Run the following in an empty directory. The release API records
`sco-sdk-core-2.4.0.zip` with SHA-256
`052189fde7d3f1f7720e634a1480e6a8b86638b8f61607b45025ac317d0186ea`;
the script compares that value with the checksum file from the same release.

```sh
EXPECTED_SHA256=052189fde7d3f1f7720e634a1480e6a8b86638b8f61607b45025ac317d0186ea
env -u GITHUB_TOKEN -u GH_TOKEN gh release download sdk-v2.4.0-rc.2 -R scuba-sc/sco-core -p 'sco-sdk-core-2.4.0.zip' -p SHA256SUMS
python3 - "$EXPECTED_SHA256" <<'PY'
import hashlib
import sys
from pathlib import Path
import zipfile
archive = Path("sco-sdk-core-2.4.0.zip")
entries = dict(line.split(None, 1)[::-1] for line in Path("SHA256SUMS").read_text().splitlines() if line.strip())
if hashlib.sha256(archive.read_bytes()).hexdigest() != entries[archive.name] or entries[archive.name].lower() != sys.argv[1].lower():
    raise SystemExit("SHA256 mismatch: stop; do not extract or build")
if Path("sco-sdk-core-2.4.0").exists():
    raise SystemExit("Extraction destination already exists; choose an empty directory")
with zipfile.ZipFile(archive) as sdk:
    sdk.extractall(".")
print(archive.name + ": SHA256 OK")
PY
```

### Historical RC1 download

The following example and digest apply only to RC1. Choose this instead of the RC2
block above when intentionally reproducing the historical RC1 path.

```sh
env -u GITHUB_TOKEN -u GH_TOKEN gh release download sdk-v2.4.0-rc.1 -R scuba-sc/sco-core -p 'sco-sdk-core-2.4.0.zip' -p SHA256SUMS
python3 - <<'PY'
import hashlib
from pathlib import Path
import zipfile
archive = Path('sco-sdk-core-2.4.0.zip')
entries = dict(line.split(None, 1)[::-1] for line in Path('SHA256SUMS').read_text().splitlines() if line.strip())
expected = entries[archive.name]
actual = hashlib.sha256(archive.read_bytes()).hexdigest()
if actual != expected:
    raise SystemExit('SHA256 mismatch: stop; do not extract or build')
print(archive.name + ': SHA256 OK')
if Path('sco-sdk-core-2.4.0').exists():
    raise SystemExit('Extraction destination already exists; choose an empty directory')
with zipfile.ZipFile(archive) as sdk:
    sdk.extractall('.')
PY
```

Expected: `sco-sdk-core-2.4.0.zip: SHA256 OK`, for the digest
`68ca5ef213fe9be162b974dcb798a6da2402583daf369e26b26634b23d869f7d` listed above.
In a browser, download both files from
`https://github.com/scuba-sc/sco-core/releases/download/sdk-v2.4.0-rc.1/`
(`SHA256SUMS` and `sco-sdk-core-2.4.0.zip`). The release-level `SHA256SUMS`
checks archive bytes; the similarly named file **inside** the SDK covers
extracted files. A checksum downloaded alongside an archive is an integrity
check, not independent signer authentication. The release verification process is described in
the [release checklist](legal/release-checklist.md).
Only extract archives from the trusted project release.

## 4. Build the checker, copy and rename the template

From the same working directory:

### Build the selected SDK 2.4.0 kit

```sh
cmake -S sco-sdk-core-2.4.0 -B sdk-build -DCMAKE_BUILD_TYPE=Release
cmake --build sdk-build --config Release
cmake --install sdk-build --config Release --prefix sdk-out
python3 - <<'PY'
from pathlib import Path
import shutil
shutil.copytree('sco-sdk-core-2.4.0/template', 'first_plugin')
for name in ('plugin.c', 'plugin.ini', 'CMakeLists.txt'):
    path = Path('first_plugin') / name
    path.write_text(path.read_text().replace('my_plugin', 'first_plugin'))
PY
cmake -S first_plugin -B first-plugin-build -DSCO_SDK="$PWD/sco-sdk-core-2.4.0" -DCMAKE_BUILD_TYPE=Release
cmake --build first-plugin-build --config Release
cmake --install first-plugin-build --config Release --prefix first-plugin-out
```

### Name your plugin

Pick your own unique id later: 1–31 characters from `a-z`, `0-9` and `_`, not
`sco`, `host`, `menu` or `game`, and not a host built-in id. Keep the folder,
manifest `id`, `sco_plugin_info.name` and command prefix consistent. Renaming
all three template files above changes the module/install target too. The
result is `first-plugin-out/data/plugins/first_plugin/`, containing the manifest
and module, with the command `first_plugin.ping`.

## 5. Check and invoke it without the game

```sh
sdk-out/bin/sco-plugin-check first-plugin-out/data/plugins/first_plugin --invoke first_plugin.ping
```

The output includes:

```text
invoke  first_plugin.ping -> ok "pong"
OK: 0 failure(s)
```

Exit **0** means checker acceptance. The checker reads `plugin.ini`, loads the
native module, exercises its exports/commands and unloads it against a stand-in
host. Missing capabilities are the default; synthetic `--cap` flags do not
prove game functionality. It is not the game, not a sandbox or a crash guard,
and passing it does not certify service behavior or gameplay. A module named
`.dll` on macOS/Linux is still a local-platform binary, **not** a Windows DLL.

## 6. Deploy on Windows and test in game (not executed here)

These are **Windows/in-game steps**, not part of the verification above:

1. Rebuild and check the plugin on **Windows x64** using the Windows SDK
   recipe; carry over your renamed template, not the macOS build products.
2. With the game stopped, copy the entire Windows-built `first_plugin` folder
   from the install output into sc-offline's `data\plugins\`. Confirm that
   `data\plugins\first_plugin\plugin.ini` and its declared module are together.
3. Check `plugins = on` in `sc-offline.ini` (the current product default); an owner can explicitly disable loading.
4. Start the offline product and inspect `data\mod.log` for this plugin's state.
   Invoke `first_plugin.ping` in the product's command UI; verify its `pong`
   reply. Record the product/game build and result before claiming gameplay
   acceptance. See [loader/status rules](plugins.md).

Install only trusted plugins: native code has the game's full rights and no
native-fault recovery guarantee. Follow [plugin rules](../sdk/docs/plugin-rules.md),
including offline-only use and no official servers/anti-cheat bypass. A
`faulted` provider requires restart; do not keep calling cached service tables.
To disable a plugin, place an empty file named `disabled` inside its folder.

## 7. Diagnose common failures

| Symptom | Check next |
| --- | --- |
| Checksum mismatch | Stop; re-download both files from the same release. Do not build/extract mismatched bytes. |
| CMake cannot find the SDK include/helper | Use the absolute extracted SDK path for `SCO_SDK`; do not point at the zip or a core-source kit. |
| C++ toolchain/configure failure | Install the C++20 toolchain even for the C walkthrough: the SDK example build configures C and C++. |
| `entry` missing, refused id or manifest error | Check the installed folder (not a source-only copy), exact id/module name and [manifest rules](../sdk/docs/plugin-ini.md). Built-in ids cannot be reused. |
| Native module cannot load | Match OS and architecture. Rebuild for Windows x64 before game use; check exports and dependencies. |
| `unavailable` / absent capability or service | Query presence, version, complete member extent and non-null callbacks. [ABI compatibility](api-v1.md#compatibility) and [contracts](contracts.md) govern safe calls; a game signature report is not gameplay proof. |
| `off` / `disabled` / `refused` | Check `plugins = on`, the `disabled` marker, dependencies and the exact logged reason. |
| `crashed` / `faulted` / reload refusal | Read [fault and hot-reload rules](plugins.md#crash-containment). Restart when required; native containment is best-effort on guarded Windows paths only. |
| Lua checker/version trouble | Use Lua 5.4 and the dedicated checker in the [Lua guide](../sdk/docs/lua.md); the native checker is not a substitute for script execution. |

## 8. Where next

- [Designing modular plugins](plugin-design.md): one feature per plugin, sharing through
  versioned service tables, `requires` and clean unload, with the `counter_service` /
  `counter_panel` example pair.
- [Plugin ABI reference](api-v1.md) and [contract catalogue / reading rules](contracts.md):
  authoritative declarations/semantics, service presence, buffers, threads and lifetimes.
- [C++](sdk-cpp.md), [C#](sdk-csharp.md), [Lua](../sdk/docs/lua.md),
  [data packs](../sdk/docs/data-packs.md): language-specific examples and limitations.
- [Game services](game-services.md) and [evidence status](game/status.md): optional
  game-pack capabilities, distinct from the neutral core SDK.
- [Agent and IDE integration](agent-integration.md): read-only MCP discovery for
  agent-assisted development, with a kit-first setup. By default it does not compile,
  run plugins or connect to the game; opt-in tools can build and check on your machine.
- [Curated audience index](README.md): host integration, game-pack work and maintenance.
