# Language and build routes

Set SDK, WORK and later CHECKER to verified absolute paths for the selected plugin kit and isolated workspace. Native code runs with the user's rights; the checker is not a sandbox.

```sh
python3 "$SDK/tools/sco-new-plugin.py" --id pilot_probe --lang c --with-menu --out "$WORK/pilot_probe"
cmake -S "$WORK/pilot_probe" -B "$WORK/pilot_probe/build" -DSCO_SDK="$SDK" -DCMAKE_BUILD_TYPE=Release
cmake --build "$WORK/pilot_probe/build" --config Release --target check
cmake --install "$WORK/pilot_probe/build" --config Release --prefix "$WORK/stage"
```

Check each exit code before proceeding. Use `cpp` for C++20; C is C11. On Windows use an x64 developer toolchain and add `-A x64` when selecting a fresh Visual Studio generator. Locate the checker under the build tree (possibly Release/), set CHECKER to that actual executable, and check final staged bytes:

```sh
"$CHECKER" "$WORK/stage/data/plugins/pilot_probe" --no-load
"$CHECKER" "$WORK/stage/data/plugins/pilot_probe" --invoke pilot_probe.ping
"$CHECKER" "$WORK/stage/data/plugins/pilot_probe" --short-tables --invoke pilot_probe.ping
```

The first command performs a folder-level static preflight without loading or executing plugin code. It checks that the plugin folder, manifest and entry file are regular non-symlink paths, validates manifest rules and folder identity, and inventories recognized content for data packs. Data scans skip symlinks/non-regular files, stop at 16 script-directory levels, cap at 4,096 indexed files and 16,384 filesystem entries, and refuse paths beyond platform limits. It does not inspect content syntax. A pass does not establish code safety, binary ABI compatibility, provider availability or runtime compatibility. `--no-load` cannot be combined with `--cap`, `--short-tables` or `--invoke`. The following commands use the normal dynamic checker path and load native plugin code. Assert exact `pong` replies. The generated native manifest and exported info both declare API 1.1. The generated check target exercises full and truncated root-table modes; it does not simulate an actual API 1.0 loader admission, a real service provider or game behavior. Re-run after the last source/manifest edit and after archive extraction.

| Language/kind | Route and limits |
| --- | --- |
| C / C++ | Generator `c` / `cpp`; public headers and SDK CMake helpers; check/install as above. |
| C# | Generator `csharp`; .NET 8 NativeAOT; follow generated publish commands with the actual target RID, then check the native module. CMake development MCP does not run dotnet publish. Preserve ABI layout checks and unmanaged lifetime rules. |
| Rust (experimental) | Use a kit whose inventory contains `rust/sco-sdk` and `examples/rust_hello`; stable SDK 2.4.0 does not. The current source supplies them in unpublished development kits. Follow the kit's `rust/README.md` with Rust 1.99 or newer and the Windows MSVC x64 toolchain for the product target. The crate exposes raw root-ABI bindings, not safe wrappers or bindings for other service headers. The CLI/MCP scaffolder and MCP Rust language filters are not available; adapt the example manually and check the staged native module in full and short-table modes. |
| Lua | Generator `lua`; no arbitrary `--services`. Native checker checks manifest/entry, not script execution. Run Lua 5.4 syntax plus kit `tools/lua-check.lua` stand-in checks, then an actual host for real behavior. Public Lua bindings do not expose arbitrary C service calls or game mutation. |
| Data | Use a matching canonical example; `kind = data`, no entry/settings. Validate the actual feature's content format, including dedicated DataCore schema/lint checks where applicable. Content indexing is not import proof. |

MCP `generate_plugin_scaffold(id, lang, with_menu, services)` takes the advertised arguments (`services` optional); require `state: available`. CLI and MCP share the kit's generator. Operator-enabled development tools can build/check CMake projects within their configured roots. `test_plugin_project` requires registered CTest tests; don't invent passing tests when none exist.
