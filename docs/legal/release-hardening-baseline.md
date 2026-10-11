# Release-hardening baseline

The fixed starting point for the SDK independence and IP release-hardening work (plan v1.0,
`docs/legal/` from here on). Nothing in this file changes how sco-core builds or packages. It records
what exists, so every later change can be compared against it.

This is an engineering record, not a legal opinion.

## Revisions

| | Commit | Notes |
|---|---|---|
| sco-core baseline | `0eb00ac4aa66a3aa9c940022c1ded41c404726aa` | main on 2026-10-11 |
| sc-offline baseline | `9fde7fa4ad2cb5bbc1aa02b0b68d673fd63fdc2f` | main, release v0.7.5 |
| sc-offline's sco-core pin | `31cf53aa99817eb8f992c8b90555b5125bbd647d` | `external/sco-core` at sc-offline's baseline; one commit behind sco-core's |
| Last published SDK | `sdk-v1.1.0` at `5e3d46340b93e8f24caea356e1dee66ffd512f74` | published 2026-10-09; asset `sco-sdk-1.1.0.zip` sha256 `61ea6d42281d99b8c53b0fa3d6b95258864022043bccb183f95ec92f53754fb1` |
| Working tree SDK version | `sdk/VERSION` = `1.2.0` | **not released**; no `sdk-v1.2.0` tag exists |
| Plugin ABI | `sco_api.h` 1.1 (`SCO_API_MAJOR 1`, `SCO_API_MINOR 1`) | layout pinned by `tests/abi_v1.c` |

## Tracked files

464 files (`git ls-files`). Their SHA-256s are in [`baseline-files.sha256`](baseline-files.sha256).
At the baseline commit this file and the two lists below do not exist yet, so they are not in it.

| Top-level path | Files |
|---|---|
| `tests/` | 111 |
| `include/` | 82 |
| `src/` | 81 |
| `plugins/` | 66 |
| `sdk/` | 65 |
| `docs/` | 36 |
| `tools/` | 6 |
| `third_party/` | 6 |
| `.github/` | 5 |
| root files (`README.md`, `LICENSE`, `CONTRIBUTING.md`, `CMakeLists.txt`, `CHANGELOG.md`, `.gitignore`) | 6 |

## The SDK zip at the baseline

`python3 sdk/package.py` at the baseline commit writes `sco-sdk-1.2.0.zip`:

- 111 entries, listed in [`baseline-sdk-zip-entries.txt`](baseline-sdk-zip-entries.txt)
- sha256 `c0e23ce428f0d4c47e17262e3ab0c2eaa37594148219ea5015b9558950cf88bd`
- reproducible: two runs from the same tree gave the same hash

There is one profile. The package mixes the game-neutral ABI with the Star Citizen game services, and
it reads `include/sc_game_pack.h` for its version lines. See findings F-04 and F-05 below.

## Tests at the baseline

CI on `0eb00ac` (the canonical record):

| Workflow | Run | Jobs |
|---|---|---|
| `test` | 38094023687 | `test`: success |
| `cmake` | 38094023629 | `linux`, `windows`, `kernel-only` (`SCO_GAME_SC=OFF`): success |
| `sdk` | 38094023661 | `csharp-host-sim`, `package`, `windows`: success |

Test sources: 18 ABI pins (`tests/abi_*.c`: v1, storage, ui, settings, ipc, net, sc_net, titanlink,
voxel_bridge, datacore, spatial, spawn, and game actors, vehicles, creative, world, entities, events)
and 21 host suites (`tests/test_*.cpp`: app, core, datacore, datacore_pack, datacore_patch,
datacore_service, hook, host, ipc, lua, net, net_service, pak, plugins, runtime, sdk, settings,
spatial, storage, ui, vfs). C# tests: `sdk/csharp/Sco.Sdk.Tests`.

`tools/test.sh` does not run on a current macOS host. Two separate causes:

1. macOS now ships a BSD `/sbin/sha256sum` that rejects `--quiet`. The toml++ check and
   `sdk/test-zip.sh` take that path and stop. This PR fixes it: both scripts use GNU `sha256sum`
   only when it is GNU, else `shasum -a 256`.
2. Apple's libc++ has no `std::atomic<std::shared_ptr<T>>` (`include/sco/vfs.h:198`), so
   `test_core.cpp` doesn't compile with Apple clang. Linux CI (clang + libstdc++) is unaffected.
   Not fixed here: it is a toolchain limit, and a code change would alter `sco::vfs`.

## Third-party code

| Component | Where | Version | Licence | Provenance recorded |
|---|---|---|---|---|
| Lua | `plugins/lua/third_party/lua/` | 5.4.8 | MIT (`LICENSE`) | `VERSION`: tarball URL and sha256. **Modified**: loops patched to call `SCO_LUA_STEP` (`plugins/lua/README.md`) |
| SQLite | `third_party/sqlite/` | 3.53.4 | public domain | `README.md`: download URL, zip SHA3-256 and SHA-256, file hashes; unedited |
| toml++ | `third_party/tomlplusplus/` | 3.4.0 | MIT (`LICENSE`) | `README.md`; file hashes checked by `tools/test.sh`; unedited |
| Dear ImGui (headers only) | `sdk/third_party/imgui/` | 1.92.8 WIP (19277) | MIT (`LICENSE.txt`) | copied from sc-offline `src/third_party/imgui/` at `1ee0727`; sc-offline CI compares the copies |

## Project licence statements

- `LICENSE`: GNU GPL version 3 text.
- Four headers carry a per-file MIT grant: `include/sc_ipc.h`, `include/sc_net.h`,
  `include/sc_titanlink.h`, `include/sc_voxel_bridge.h`.
- Four documents say plugins built against `sco_api.h` are GPL-3.0 with no linking exception:
  `README.md:148`, `docs/api-v1.md:7`, `sdk/README.md:134`, `sdk/docs/plugin-rules.md:47` (F-13).
- Whether the grant is GPL-3.0-only or GPL-3.0-or-later is not stated anywhere in the repository.

## Authors in history

Two commit identities: `yerts` (167 commits) and `scubamount` (87 commits, two e-mail forms). The
maintainer has stated that both are the same person. That statement is the basis for the
contributor-rights record. Commit author names alone are not.

## Findings re-checked at the baseline

| ID | Result |
|---|---|
| F-01 | Kernel-only CI job exists (`cmake.yml`, `-DSCO_GAME_SC=OFF`). The CMake option **defaults to ON** (`CMakeLists.txt:26`) |
| F-02 | `plugins/lua/sco_lua.c` includes `sc_actors.h`, `sc_game_events.h`, `sc_world.h`, `sco_datacore.h`, `sc_entities.h`, `sc_vehicles.h`, `sc_creative.h` |
| F-03 | `tests/test_lua.cpp`: the `SCO_KERNEL_ONLY` block ends at line 571; `sco.game.actors` tests follow it |
| F-04 / F-05 | `sdk/package.py`: one `CONTENT` list; `sc_game_pack.h` read for version metadata |
| F-06 | C# `Sco.Sdk/Interop.cs` plus `Sco.Sdk/Game/*.cs` in one project |
| F-07 | `include/scosdk/game/` and `scosdk/datacore.hpp` packaged with the generic wrappers |
| F-08 / F-09 | `sdk/examples/quantum_pack/datacore/eos.toml` holds 3 GUIDs; `travel_pack` ships game identifiers |
| F-10 | sc-offline `tools/sdk-headers.py` regex-parses `CONTENT` |
| F-12 | sc-offline pins `31cf53a`, one commit behind |

## Open provenance questions

1. The basis for shipping the record names and GUIDs in `quantum_pack` and the identifiers in `travel_pack`.
2. The basis for the game pack's signature rows. That pack is outside the core SDK scope but needs its own assessment.
3. The plugin licence policy (F-13): a decision for the copyright holder and counsel.
4. GPL-3.0-only versus -or-later.
