# Building and testing

sco-core has two equivalent builds: the shell scripts in `tools/` (the reference, with every sanitizer run) and a root `CMakeLists.txt` (any CMake toolchain, MSVC included, and the way to consume sco-core from another CMake project). CI runs both.

## Requirements

| For | You need |
|---|---|
| `tools/test.sh` | bash, clang or gcc with C11 and C++20, and the sanitizer runtimes (ASan, UBSan, TSan). `clang` must be on `PATH` for the `x86_64-pc-windows-msvc` ABI pin |
| CMake | CMake 3.20 or newer and a 64-bit C/C++20 toolchain: clang, gcc, or Visual Studio 2019 or newer (MSVC x64) |
| `tools/test-win.sh` | `x86_64-w64-mingw32-g++`, clang with the `x86_64-w64-mingw32` target, and Wine |
| The SDK zip | Python 3, CMake, a C and C++20 compiler; Lua 5.4 for the Lua example check |

The runtime supports C++20 standard libraries without `atomic<shared_ptr>` specialization by using the standard atomic shared-pointer operations.

Windows x64 is the supported product platform. Linux x64 and macOS x64/ARM64 are
development platforms for the neutral code and synthetic host tests, not product
runtimes. On macOS ARM64 use `-DSCO_GAME_SC=OFF`; executable x86-64 detours are
unsupported there (`InstallDetour` returns `Error::Unsupported` without modifying
memory). Generic scanner/decoder, VFS, loader and host tests still run.
Nothing in the synthetic build needs the game, and only the Windows build needs Windows.

## `tools/test.sh`

```sh
tools/test.sh                          # picks clang/clang++, else gcc/g++
CC=clang CXX=clang++ tools/test.sh     # what CI runs
```

In order, it:

1. compiles the ABI pins `tests/abi_v1.c`, `tests/abi_storage.c`, `tests/abi_ui.c` and `tests/abi_settings.c` with `-Werror` as C11 and C++20, again with `-fshort-enums`, and for `x86_64-pc-windows-msvc` (compile-only),
2. compiles the SDK template, the `hello` and `cpp_hello` examples, each `include/scosdk/` header and `sco-plugin-check` against `sco_api.h`,
3. builds and runs `test_core` and `test_hook` (ASan+UBSan), `test_runtime`, `test_host`, `test_spatial` and `test_sdk` (each under ASan+UBSan and again under ThreadSanitizer),
   then builds vendored SQLite once per sanitizer set and runs `test_storage` under both (it also spawns and kills copies of itself for the crash tests),
4. builds the fake plugins from `tests/fixtures/plugins/native/fake_plugin.c` into `tests/out/plugins/` and runs `test_plugins` (ASan+UBSan), then `test_ui` under ASan+UBSan and ThreadSanitizer,
5. builds Lua and sco-lua and runs `test_lua` (ASan+UBSan), which also loads `sdk/examples/greeter` through the real loader,
6. runs `test_app` (ASan+UBSan): the host kit with built-in plugins, two fake plugins, `greeter` and `travel_pack`,
7. builds `hello` as a shared library into `tests/out/sim/plugins/` next to copies of `greeter` and `travel_pack`, builds `sco-host-sim` (ASan+UBSan) and runs `sco-host-sim tests/out/sim/plugins --ticks 3 --invoke hello.wave "Pilot One"`, which must exit 0 and answer `Hello, Pilot One`. It also builds `counter_service` and `counter_panel` there and runs the sim again with `--invoke counter_panel.bump`, which must answer `counter is 1` through `counter_service`'s table.

Each test binary ends with `N passed, M failed` and exits non-zero on any failure; the script stops at the first failure. Output goes to `tests/out/` (ignored by git).

## CMake

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

Windows, from a Developer PowerShell for VS (the Visual Studio generator is multi-config, so name the config):

```powershell
cmake -S . -B build -A x64
cmake --build build --config RelWithDebInfo
ctest --test-dir build -C RelWithDebInfo --output-on-failure
```

With `SCO_BUILD_TESTS=ON`, configuration requires Python 3 and creates a fresh `<build>/sco-fixtures-*` directory for the plugin discovery fixtures. The generated CTest commands and library output paths point to it, and the configuration log prints its path. Reconfiguring creates another directory and leaves the old ones, so tests never pick up stale plugins. The cost is disk space and a rebuild of the fixture libraries.

Multi-config generators share one fixture namespace across configurations. Use a separate build tree for each configuration: switching or running configurations concurrently in one tree can reuse or overwrite the other configuration's fixture libraries.

### Options

| Option | Default | Does |
|---|---|---|
| `SCO_BUILD_TESTS` | `ON` when sco-core is the top-level project, else `OFF` | Builds the tests and the fake plugins and registers them with CTest |
| `SCO_WERROR` | `ON` | Warnings in sco-core's own code are errors (`-Wall -Wextra -Werror`; MSVC `/W4 /WX /utf-8`). Vendored Lua and SQLite are always built with warnings off |
| `SCO_SANITIZE` | empty | Sanitizers for sco-core's code and tests, for example `address,undefined` (not MSVC) |
| `SCO_GAME_SC` | `ON` | Builds the Star Citizen game pack: the signature rows in `src/game` (`sco_game_sc`), the CryPak adapter (`sco_pak`), DataCore (`sco_datacore`, `sco_datacore_service`), `sco-sigcheck` and `sco-dcb`. `OFF` builds and tests the kernel alone with `SCO_KERNEL_ONLY` defined: no game rows, `Platform::dataCore` logs `sco.datacore not started: this build has no game pack`. CI's `kernel-only` job builds it that way ([architecture](architecture.md#kernel-and-game-pack)) |

The build is 64-bit only; configuring for 32 bits stops with an error.

### Targets

The table lists direct links from [`CMakeLists.txt`](../CMakeLists.txt); transitive
links follow from them. **Game-pack** targets exist only with `SCO_GAME_SC=ON`;
`sco_game_services` additionally requires Windows.

| Target | Sources | Direct links |
|---|---|---|
| `sco_core` | `src/sco_scan.cpp`, `sco_signatures.cpp`, `sco_signature_registry.cpp`, `sco_log_status.cpp`, `sco_pe_file.cpp`; on Windows also `sco_image_win.cpp`. No game rows | `Threads::Threads` |
| `sco_game_sc` (game-pack) | `src/game/*.cpp` except `pak_hooks.cpp`: signature rows and `RegisterGameSignatures` | `sco_core` |
| `sco_pak` (game-pack) | `src/game/pak_hooks.cpp`: the CryPak adapter, `sco/game/pak.h` (its rows are in `sco_game_sc`) | `sco_game_sc`, `sco_core`, `sco_hook`, `sco_vfs`, `sco_host` |
| `sco_runtime` | `src/api/sco_tasks.cpp`, `sco_events.cpp`, `sco_commands.cpp`, `sco_services.cpp`, `sco_raw.cpp` | `Threads::Threads` |
| `sco_hook` | `src/hook/sco_hook.cpp`: detours and near-code memory, `sco/hook.h` (x86-64) | `Threads::Threads` |
| `sco_engine` | `src/engine/zone.cpp`: the zone tree, `sco/engine/zone.h` (spatial math `sco/engine/types.h` is header-only) | `Threads::Threads` |
| `sco_vfs` | `src/vfs/compose.cpp`, `table.cpp`: neutral virtual-file composition and mounts | `Threads::Threads` |
| `sco_host` | `src/api/sco_caps.cpp`, `src/host/sco_host.cpp`, `sco_diagnostics.cpp`, `sco_diagnostics_publisher.cpp` | `sco_runtime`, `sco_core` |
| `sco_plugins` | `src/plugins/manifest.cpp`, `settings_ini.cpp`, `discover.cpp`, `loader.cpp`, `content.cpp`; `guard_win.cpp` on Windows only | `sco_runtime`, `sco_core`, `${CMAKE_DL_LIBS}` |
| `sco_lua_vendor` | `plugins/lua/third_party/lua/src/*.c` without `lua.c`/`luac.c` | `m` on Unix |
| `sco_lua` | `plugins/lua/sco_lua.c` | `sco_lua_vendor` |
| `sco_lua_game` (game-pack) | `plugins/lua/sco_lua_game.c`: optional game adapters | `sco_lua` |
| `sco_sqlite` | `third_party/sqlite/sqlite3.c` ([options](../third_party/sqlite/README.md)), `-std=gnu11`, warnings off | `Threads::Threads`, `m` on Unix |
| `sco_storage` | `src/storage/storage.cpp`: the `sco.storage` host service ([docs](storage.md)) | `sco_host`, `sco_sqlite` |
| `sco_ui` | `src/ui/ui.cpp`: the legacy `sco.ui` host service ([docs](ui.md)); `src/ui/surfaces.cpp`: the `sco.ui.surfaces` host service ([docs](ui.md#surfaces)) | `sco_host` |
| `sco_widgets` | `src/widgets/widgets.cpp`: renderer-neutral retained panels ([design](design/widgets.md)); `src/widgets/view.cpp`: the host-side `sco.widgets.view` reader ([page](widgets-view.md)) | `sco_host` |
| `sco_widgets_imgui` (optional) | `src/widgets/imgui_adapter.cpp`: host-only adapter (`SCO_WIDGETS_IMGUI=ON`, product links ImGui) | `sco_widgets` |
| `sco_settings` | `src/settings/settings.cpp`: typed plugin settings | `sco_host`, `sco_plugins`, `sco_storage`, `sco_ui` |
| `sco_ipc` | `src/ipc/ipc.cpp` and `shm_win.cpp` or `shm_posix.cpp` | `sco_host`; privately `advapi32` on Windows or `rt` on non-Apple POSIX |
| `sco_net` | `src/net/sha2.c`, `wire.cpp`, `reliable.cpp`, `core.cpp` | None |
| `sco_net_service` | `src/net/service.cpp`, `scope.cpp`, `control.cpp` and `udp_win.cpp` or `udp_posix.cpp` | `sco_net`, `sco_host`, `Threads::Threads`; privately `ws2_32` on Windows |
| `sco_datacore` (game-pack) | `src/datacore/datacore.cpp`, `patch.cpp`, `pack.cpp`: parser, patcher and packs | `sco_vfs` |
| `sco_content` | `src/content/content.cpp`: the read-only `sco.content` host service | `sco_host`, `sco_plugins` |
| `sco_datacore_service` (game-pack) | `src/datacore/service.cpp`: queued DataCore overrides | `sco_datacore`, `sco_host`, `sco_plugins` |
| `sco_game_services` (game-pack, Windows) | The native game services under `src/game/services/` (spatial, spawn, missions, actors, vehicles, world, entities, inventory, audio and the rest listed in `CMakeLists.txt`) | `sco_game_sc`, `sco_host`, `sco_engine`, `sco_hook`, `sco_quantum`, `sco_offline_missions`, `sco_mining`, `sco_game_admission`, `sco_pak` |
| `sco_app` | `src/app/sco_app.cpp`, `plugin_startup_snapshot.cpp`: the host kit, `sco/app.h` | `sco_host`, `sco_plugins`, `sco_core`, `sco_storage`, `sco_ui`, `sco_widgets`, `sco_settings`, `sco_content`, `sco_ipc`, `sco_net_service`, `sco_diagnostics_ipc`; with the game pack also `sco_datacore_service`, `sco_game_sc`, `sco_game_diagnostics`, `sco_private_session`, and on Windows `sco_game_services` |
| `sco-sigcheck` (game-pack) | `tools/sco-sigcheck.cpp` | `sco_game_sc` |
| `sco-host-sim` | `tools/sco-host-sim.cpp` | `sco_app`, `sco_lua`; with the game pack also `sco_datacore`, `sco_lua_game` |
| `sco-dcb` (game-pack) | `tools/sco-dcb.cpp` ([docs](datacore.md)) | `sco_datacore` |

Smaller game-pack libraries (`sco_game_admission`, `sco_game_diagnostics`, `sco_game_host`, `sco_private_session`, `sco_quantum`, `sco_mining`, `sco_loadout`, `sco_offline_missions`, `sco_relocation_provider`, `sco_asop_host`, `sco_asop_selection`) and the diagnostics libraries (`sco_diagnostics_ipc`, `sco_authority`) are declared next to their consumers in `CMakeLists.txt`.

Project libraries expose `include/` directly or through their public links;
vendored SQLite and Lua expose their own header directories.

### Using sco-core from another CMake project

```cmake
add_subdirectory(external/sco-core)            # tests stay off when not top level
target_link_libraries(my_host PRIVATE sco_app sco_lua)   # sco_app brings sco_host, sco_plugins, sco_core
```

What sc-offline does, and why (from its `CMakeLists.txt`):

```cmake
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")   # before add_subdirectory
set(SCO_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(SCO_WERROR OFF CACHE BOOL "" FORCE)
add_subdirectory(external/sco-core EXCLUDE_FROM_ALL)
```

- **Set `CMAKE_MSVC_RUNTIME_LIBRARY` before `add_subdirectory`.** sco-core's targets take the value in effect when they are created, so a static CRT set afterwards doesn't reach them, and a DLL that links them imports the dynamic CRT after all. sc-offline's CI checks that `dinput8.dll` imports no dynamic CRT.
- **`SCO_BUILD_TESTS OFF`.** It is already off when sco-core isn't the top-level project; forcing it keeps a cached `ON` from an earlier configure out. sco-core's own CI runs the tests.
- **`SCO_WERROR OFF`.** sco-core's CI builds with `/W4 /WX`; in a product build, a warning that only a newer toolset emits must not break the build. Warnings still show.
- **`EXCLUDE_FROM_ALL`** builds only the libraries the product links, not `sco-sigcheck`, `sco-host-sim` or `sco-dcb`.
- The source tree must be checked out: with a submodule, clone with `--recurse-submodules` (CI: `submodules: true`). sc-offline stops at configure with a message when `external/sco-core` is empty.

### Tests

The table lists the main tests. `ctest --test-dir build -N` lists all of them.

| CTest name | What |
|---|---|
| `abi_v1` | Rebuilds the ABI pin objects (C11, C++20 and, off MSVC, `-fshort-enums`); fails if any static assert breaks |
| `abi_storage` | The same for the `sco.storage` table pin, `tests/abi_storage.c` |
| `abi_ui` | The same for the `sco.ui` table pin, `tests/abi_ui.c` |
| `abi_widgets` | The `sco.widgets` table, node and event layout pins, `tests/abi_widgets.c` |
| `abi_widgets_view` | Layout pins of `sco/widgets_view.h` |
| `abi_ui_surfaces` | The `sco.ui.surfaces` table, desc and pose layout pins, `tests/abi_ui_surfaces.c` |
| `test_widgets` | Every neutral widget kind, events, bounds, malformed inputs, atomic replacement and owner cleanup through a headless renderer |
| `test_widgets_sdk` | C++ table extent/null/reopen guards and optional ImGui adapter API shim (no GPU) |
| `test_widgets_app` | Automatic widget publication and teardown through the real host kit across two sessions |
| `test_widgets_view` | The panel list, copy and click queue of `sco.widgets.view` with synthetic panels and threads |
| `test_host_menu`, `test_host_menu_owner` | `sco/host_menu.h` over the real host |
| `test_sig_index_equivalence` | The per-pass signature scan index against verbatim copies of the loops it replaced |
| `abi_settings` | The same for the `sco.settings` table pin, `tests/abi_settings.c` |
| `test_settings` | `sco.settings` over the real host table and storage: defaults, typed sets, `settings.changed`, persistence across a restart, stale kept values |
| `test_surfaces` | `sco.ui.surfaces`: every kind, every refused field, ownership of ids and panels, per-plugin and host limits, release through sco.ui's hook, threads registering beside snapshots, `scosdk/ui_surfaces.hpp` |
| `test_ui` | `sco.ui`: tab order, ids outside the plugin's prefix, badges, overlays, the chord grammar, conflicts and reserved chords, dispatch with arguments, unload, a fault in a draw under the loader's guard, threads registering beside frames |
| `test_storage` | Host-owned services and `sco.storage`: key-value and the size handshake, transactions, SQL, isolation (no `ATTACH`, `PRAGMA` or `VACUUM INTO` escape), the quota, unload, threads, and a child process killed mid-transaction |
| `test_core` | Scanners and the signature registry against a synthetic image |
| `test_signature_registry` | Owned definitions, atomic failures, budgets, long dependency chains, immutable snapshot publication and host sealing on synthetic images |
| `test_signature_differential` | Synthetic graph and scanner results compared with the checked-in baseline production resolver sources; not game acceptance |
| `test_runtime` | Task queue, event bus, command registry, `Release` |
| `test_host` | Capabilities and the `sco_api` table |
| `test_runtime_diagnostics_snapshot` | Synthetic copied plugin/lifecycle/signature/capability metadata, registry withdrawal/exposure, short trusted tables, redaction, keyed log gaps and generation pages; no external diagnostics channel |
| `test_hook` | Detours over small functions written into executable memory; vtable slot swaps |
| `test_pak` | `sco::game::pak` over a fake `ICryPak` (a real vtable) and a detoured fake loader: the load window, a served mount, passthrough of other files, a second `.dcb` and a second thread, an inert mount read from 0, offsets past 2 GiB, `Disable` |
| `test_spatial` | Vector, quaternion and transform math; the zone tree (chains, round trips at 1e11 m, failures, readers beside a writer) |
| `test_sdk` | The C++20 SDK layer (`include/scosdk/`): two SDK plugins over the real host table |
| `test_plugins` | `plugin.ini`, discovery, the content index and the native loader against the fake plugins in `<build>/sco-fixtures-*/plugins/` |
| `test_lua` | sco-lua through the real loader and host table, including `sdk/examples/greeter`, and `sco.store` over a real `sco.storage` with `sdk/examples/notebook` |
| `test_app` | The host kit (`sco::app`) and built-in plugins: load order, `game.ready`, tick, a faulting built-in contained, `game.exit` before unload, unload order, restart |
| `test_plugin_startup_snapshot` | Synthetic version-1 projection, privacy/bounds, starting/complete/failed/incomplete serialization, partial-temp isolation, and process-shared writer locking; no game or native plugin execution |
| `host_sim_examples` | `sco-host-sim <build>/sco-fixtures-*/sim/plugins --ticks 3 --invoke hello.wave "Pilot One"` over `hello`, `cpp_hello` (built by CMake), `greeter` and `travel_pack`: exit 0 and the reply; then `--invoke counter_panel.bump` over the same folder, where `counter_panel` calls `counter_service`'s table and must answer `counter is 1` (`tests/host_sim.cmake`) |

On Windows `test_plugins` loads real DLLs with `LoadLibraryExW` and runs the real `__try/__except` crash guard. The TSan runs and the `x86_64-pc-windows-msvc` cross-compile of the ABI pin stay in `tools/test.sh`.

## `sco-host-sim`

The real host kit, runtime, loader and sco-lua outside the game, for CI and for trying plugins:

```sh
sco-host-sim <plugin root> [--exe StarCitizen.exe] [--cap NAME]... [--ticks N] [--no-lua] [--invoke NAME [ARG]...]
```

It starts `sco::app` with plugins on and a built-in demo plugin `sim` (command `sim.ping`), runs N ticks (default 1), invokes one command with its arguments parsed against the command's arg defs (int, float, string, bool as `1`/`0`/`true`/`false`), then stops. `--exe` resolves the signature tables against a game executable on disk; `--cap` sets a capability ready; `--no-lua` leaves Lua plugins without a runtime (refused). Log lines go to stdout, as in `mod.log`. Exit 0 when every plugin ended loaded, off or disabled and the invoke answered OK; 1 otherwise; 2 for bad arguments.

## `tools/test-win.sh`

Builds `test_plugins` and the fake plugins as a Windows program with mingw (the crash guard with clang, since GCC has no `__try`) and runs it under Wine. Useful on macOS or Linux when you change the loader; on Windows, the CMake build runs the same test natively.

## The SDK

`sdk/` builds on its own (see the [SDK README](../sdk/README.md)). To build the zip modders download and check it the way CI does:

```sh
python3 sdk/package.py --out dist        # dist/sco-sdk-<version>.zip, reproducible, with SHA256SUMS
sdk/test-zip.sh dist/sco-sdk-*.zip       # builds and checks every example from the unpacked zip alone
```

The two-profile build (`core`, then the Star Citizen extension over it) keeps each zip's own
`VERSIONS.txt` and `SHA256SUMS` — the extension's are named `VERSIONS.txt.game-sc` and
`SHA256SUMS.game-sc`, so unpacking it over the core overwrites nothing:

```sh
python3 sdk/package.py --profile core --out dist-profiles
python3 sdk/package.py --profile game-sc --out dist-profiles
sdk/test-zip.sh dist-profiles/sco-sdk-core-*.zip dist-profiles/sco-sdk-game-sc-*.zip
```

### Packaging checks

`sdk/components.json` lists every SDK file with its profile, origin and licence. `sdk/package.py` builds a zip from it and nothing else (`--check` validates it, `--list-headers core` prints the public headers of a profile).

- Reproducibility: `python3 sdk/test_package.py` builds every profile in separate fresh working and output directories under different `TZ`, `LC_ALL`, Python hash seeds and umasks, then compares SHA-256 digests. It checks fixed ZIP timestamps and modes and the absence of checkout and output paths. This proves byte identity within one Python/zlib environment, not across compression-library versions. Repository document links in packaged docs are pinned to the source commit, not a moving branch.
- A locally built normal package can share the published archive's version and filename without identical bytes. Release identity is the digest in the published `SHA256SUMS`.
- Secret shapes: packaging refuses recognized PEM/OpenSSH private-key and GitHub token shapes before writing ZIP bytes. The check is bounded and does not recognize every possible secret.
- SPDX: `ip_audit.py zip` and the packaging tests use an offline stdlib structural check of the required SPDX 2.3 fields, IDs, relationships, inventories and checksums. It is not a full SPDX validator. The optional `ip_audit.py zip --sbom` sidecar also binds the complete archive hash; the embedded SBOM describes the extracted payload.
- Source-kit extraction: `package.py --profile core-source` produces the kernel source kit. `test-zip.sh <source.zip>` verifies checksums, configures and builds from the extracted files alone, then runs the focused synthetic host tests. `python3 sdk/test_package.py` also verifies the manifest-classified game exclusions and exact public-header membership (including nested includes), then configures, builds and runs every neutral CTest from a freshly extracted archive. This runs locally and in the SDK CI job. Game-only cases of shared test drivers live in separate include fragments that `SCO_GAME_SC=OFF` never includes. The `game-contract` group in `sdk/components.json` defines the exclusion inventory. The released 1.1 header in `tests/abi/released/sco_api.h` is checked against its golden SHA-256, so the frozen-plugin test needs neither Git nor a network.
- Header dependencies: external C and C++ example sources are copied outside the extraction and compiled against its installed headers. Compiler dependency output must name each SDK header as an exact approved public destination in `components.json`; directory prefixes do not approve nested files. Macro, transitive and system-marked includes are checked, and hidden checkout dependencies are refused. `test-zip.sh` runs the same check, or run `package.py --profile core --check-installed-headers <extracted-root>`.
- CI also inspects the full checkout's kernel-only target graph, compiler depfiles, compiled objects and link inputs. Symbol scans are supplementary.
- Development packages: `sdk/package.py --development --profile host-source-sc --out <directory>` (or another profile) from a clean Git checkout uses the committed `sdk/DEVELOPMENT_VERSION` as the coordinate in archive names and generated metadata, writes it to the kit `VERSION`, excludes the input file and records the exact source commit. It implies no tag or release. The game-sc overlay keeps its game-pack version and adds the development SDK identifier to its archive name.
- The `core-source` and `host-source-sc` kits exclude the runtime MCP peer. Keep `SCO_BUILD_RUNTIME_MCP=OFF` there; building that peer needs a full source checkout.

### ImGui pins for the legacy `sco.ui` route

The shipped public headers target sc-offline's ImGui snapshot at `1ee0727cc63866a95e709ae61dd5dd355b511253`: 1.92.8 WIP, `IMGUI_VERSION_NUM` 19277.

| File in `third_party/imgui/` | SHA-256 |
| --- | --- |
| `imgui.h` | `cd65b1905d5cbdaeab82f6ff180ae8767bd72c7bc99ee258c74665eb2d3ee1fa` |
| `imconfig.h` | `1353bfa59da16eec481723fd4e5d910356f0406b79e3c4142e0c9867bafffab5` |
| `LICENSE.txt` | `173506a2d6f7fb67990d257fb2507f188690eca39060c39469ae7bef43aae2a3` |

### Platform notes for the kernel source kit

Hook execution tests run on x64 Windows and Linux. Byte decoding and unsupported-host checks run on every development platform. ARM64 detour installation returns `Error::Unsupported` without changing memory.

## CI

| Workflow | Runs on | Does |
|---|---|---|
| [`test`](../.github/workflows/test.yml) | ubuntu-24.04 | `CC=clang CXX=clang++ tools/test.sh`, and checks that `sco-sigcheck` builds |
| [`cmake`](../.github/workflows/cmake.yml) | ubuntu-24.04 (clang, ASan+UBSan), windows-2025 (MSVC x64) | Configure, build and `ctest` |
| [`sdk`](../.github/workflows/sdk.yml) | ubuntu-24.04, then windows-2025 (MSVC x64) | Packages the SDK zip, builds and checks every example from it, keeps the zip as an artifact |
| [`release`](../.github/workflows/release.yml) | ubuntu-24.04, manual dispatch | Promotes an exact candidate run and artifact after checking its source, hashes and attestations; re-downloads and verifies the draft assets before publication. Never rebuilds ([release procedure](legal/release-checklist.md#releasing)) |
| [`docs-site`](../.github/workflows/docs-site.yml) | ubuntu-24.04 | PRs build read-only site artifacts; trusted `main` pushes and manual dispatches deploy the validated site to GitHub Pages |
| [`release-candidate`](../.github/workflows/release-candidate.yml) | ubuntu-24.04, after the required checks pass on `main`, or manual dispatch | Builds the five archives once on a checked `main` commit and keeps them as one artifact |
| [`ip-compliance`](../.github/workflows/ip-compliance.yml) | ubuntu-24.04 | Provenance, licence and package checks; keeps the reports |
| [`experimental-rust-qualification`](../.github/workflows/experimental-rust-qualification.yml) | pull requests and manual runs | Rust bindings qualification. Not a release gate |
| [`discord`](../.github/workflows/discord.yml) | ubuntu | Posts repository activity to the sc-offline Discord channel |

Workflow actions are pinned to commit SHAs. The release workflow defaults to `contents: read`; its verification job also has `actions: read`, and only its publication job has `contents: write`. The docs-site build remains read-only; its trusted-main deployment job has `pages: write` and `id-token: write`. PR site builds cannot deploy. A pull request needs `test`, `cmake` and `sdk` green; release candidates additionally require the exact-commit checks listed in the [release procedure](legal/release-checklist.md#releasing).

## Releases

The SDK is released as profile archives under a tag `sdk-v<version>`; a release candidate uses `sdk-v<version>-rc.<n>`. The current release:

<!-- release-block:current-release -->
SDK **2.4.1** (release candidate 1, a pre-release for testing; the final release follows) uses Star Citizen game pack **0.8.1** (checked against Star Citizen
Alpha 4.10.3, build 4.10.200.58746; see [game builds](../docs/game/builds.md)) and plugin ABI **1.1**. Download it from the
[`sdk-v2.4.1-rc.1` release page](https://github.com/scuba-sc/sco-core/releases/tag/sdk-v2.4.1-rc.1) and verify each archive against that release's
`SHA256SUMS`; the [release notes](https://github.com/scuba-sc/sco-core/blob/sdk-v2.4.1-rc.1/docs/releases/sdk-v2.4.1.md) list what changed. The previous release is
[SDK 2.4.0](https://github.com/scuba-sc/sco-core/blob/sdk-v2.4.0/docs/releases/sdk-v2.4.0.md).
<!-- /release-block:current-release -->

Each release's archives and digests are in its release notes and on its release page. SDK 2.4.0 was published on 2026-11-01. The tags `sdk-v2.0.0` to `sdk-v2.3.0` remain in git, but their downloads were withdrawn. `sdk-v2.4.0-rc.1` and `-rc.2` are historical. The version is in `sdk/VERSION`. The plugin ABI has its own version, `SCO_API_MAJOR` / `SCO_API_MINOR` in `sco_api.h`; version 1 only grows ([Compatibility](api-v1.md#compatibility)).

To release, follow these steps in order. The [release checklist](legal/release-checklist.md#releasing) has the same procedure from the maintainer's side.

1. **Merge so that `main` points at the candidate commit.** Prepare the version, the changelog section and the release notes on a branch, run `python3 tools/doc_release_gate.py --tag sdk-vX.Y.Z`, and merge. Archive bytes embed the source commit. The published hashes are therefore those of the candidate that CI builds from `main`, never those of a local build. Do not push anything else to `main` until the release is published: `release.yml` accepts a tag only when it points at the commit of `main` it runs on.
2. **Wait for the five workflows that run on a push to `main`** (`cmake`, `sdk`, `test`, `ip-compliance` and `docs-site`) to finish green on that exact commit.
3. **Run `release-candidate.yml`.** When all five succeed on a push to `main`, its `readiness` job validates them and starts the candidate for the tag named in `sdk/CANDIDATE_TAG`. You can also dispatch it by hand from `main` with `tag` (and optionally `expected_source_sha`). The run checks the documentation gate and the five workflows again on the exact commit, runs the repository audit, packages the five profiles, audits each archive with its SBOM, exercises the archives, writes `SHA256SUMS`, attests every file and uploads one immutable `release-candidate` artifact, plus a separate review bundle. Note the run ID and the artifact ID, and check the source commit, the file inventory and the hashes. A candidate publishes nothing and creates no tag.
4. **Tag that exact commit** (`sdk-vX.Y.Z`) and push the tag.
5. **Dispatch `release.yml` from `main`** with its three inputs: `tag`, `run_id` (the candidate run) and `artifact_id` (the candidate artifact).
6. **Approve the `release` environment.** Both the `verify` and the `publish` job wait for approval. `verify` checks that the tag points at the commit it runs on, downloads the candidate, and checks its inventory, checksums and attestations. `publish` creates a draft, downloads the draft's assets, verifies them again and then publishes. Nothing is rebuilt. A failed round trip leaves a draft; do not publish or change it by hand to get around the checks.
7. **After publication,** in this order:
   1. Copy the archive digests from the published release into the release notes.
   2. Change the notes' "Planned tag" wording to the published wording, with the date and source commit, as the notes of the previous release do. Make steps 1 and 2 one pull request.
   3. Let `docs-site` deploy. It also runs after a successful `release` run, and it publishes the site from the published release's metadata.
   4. The announcement is posted by the `notify` job of `release.yml`, for a final tag only (not for an `-rc.` tag). It runs right after publication. It checks that the release is complete, including that the deployed site reports this SDK and game pack version from a commit that contains the release source. If the site is not current yet, it refuses and posts nothing. Then dispatch the `discord` workflow from `main` with `release_tag` once the site is deployed; it replays the same announcement with the same receipts. It posts once per tag. A GitHub release made with the workflow's own token raises no `release` event, so the `discord` workflow's release trigger does not fire for it.

**Bumping the version.** Run `python3 tools/release_docs.py --bump X.Y.Z [--game-pack A.B.C] [--game-build B]`. It writes `sdk/VERSION`, the next development coordinate, the candidate tag, the changelog heading, a release-notes skeleton, the site allowlist and hub entry, and re-renders every release region in the docs. Then fill in the release notes and the changelog section and run `python3 tools/doc_release_gate.py --tag sdk-vX.Y.Z`. Digests are added to the release notes only after the release workflow publishes them. Never hand-type the current version, tag, game pack, game build, plugin ABI or archive names outside a generated region (a `release-block` or `rv` marker, described in the `tools/release_docs.py` docstring); `python3 tools/release_docs.py --check` fails on stale regions.

Do not manually publish a failed draft to bypass the promotion checks.

**A release candidate before the final release.** `sdk/CANDIDATE_TAG` may name `sdk-v<version>-rc.<n>` instead of the final tag. The generated release regions then say that the SDK is a release candidate and send readers to that candidate's release page, because the final page does not exist yet. The steps above are the same with the candidate tag. `release.yml` publishes an `-rc.` tag as a pre-release and posts no announcement, and its notes are the CHANGELOG section `## sdk-v<version>` with one line in front. Candidate archives have the file names of the final release, because the name comes from `sdk/VERSION`; only the tag and the digest tell them apart. To go on to the final release, set `sdk/CANDIDATE_TAG` back to `sdk-v<version>`, run `python3 tools/release_docs.py --write`, and follow the steps above again from a new `main` commit.

## Adding a source file

The two builds list sources separately. A new `.cpp` under `src/game/` is picked up by both builds' globs (`pak_hooks.cpp` is the one exclusion, for `sco_pak` and `test_pak`). Any other new `.cpp` goes into both `tools/test.sh` (`APP` for anything `sco_app` links) (and `tools/test-win.sh` if `test_plugins` links it) and the matching library in `CMakeLists.txt`; a new fake-plugin mode goes into the `for m in ...` loops of both scripts and `SCO_FAKE_PLUGIN_MODES`.
