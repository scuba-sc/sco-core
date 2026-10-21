# Star Citizen host-source kit (G16)

`host-source-sc` is a standalone source-development archive, not a plugin SDK
or overlay. It contains the neutral source kit, game implementation, DataCore,
private host composition headers, Lua adapter, vendored dependencies and synthetic
fixtures. Every file is explicitly classified in `sdk/components.json` with
provenance, existing SPDX SBOM/checksum records and source-commit metadata.
The frozen plugin ABI remains 1.1. Plugin authors still use `core` + `game-sc`.

## Local build

```sh
python3 sdk/package.py --profile host-source-sc --out sdk/out
python3 sdk/test_host_source.py HostSourceArchive.test_standalone_game_source_archive -v
```

Extract the generated archive into an empty directory (no submodule or overlay):

```sh
cmake -S . -B build -DSCO_GAME_SC=ON -DSCO_BUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build build --config Debug --parallel 4 --target test_game_host test_game_host_data
ctest --test-dir build -C Debug --output-on-failure -R '^game_host(_loss|_quit(_missing)?|_data)?$'
```

The packaging regression checks exact inventory, checksums/SBOM/provenance,
private adapter membership, fresh extracted compilation and five host fixtures.
It also verifies the game inventory is physically absent from `core-source`;
no kernel-only rebuild/full suite is required for this targeted proof.
See [host composition](game/host.md) for lifecycle and migration limitations.

## Integration and release limits

All six adapter implementation/header/test/doc files are included in this profile
only, with `public_header: false`. `sdk/add_host_adapter_components.py` validates
real files/provenance before adding their exact membership; it is idempotent and
refuses any conflicting plugin profile membership. No placeholder files are used.

The kit is published as `sco-sdk-host-source-sc-2.4.0.zip` in
[SDK 2.4.0 release candidate 1](https://github.com/scubamount/sco-core/releases/tag/sdk-v2.4.0-rc.1)
(tag `sdk-v2.4.0-rc.1`, SHA-256
`5ca76ff9dac6bbaa183c6f308d610e08d9c1b0daf6ebda6458e222277406dfc6`; see the
[2.4.0 release notes](releases/sdk-v2.4.0.md)). The profile was first scoped by the
[SDK 2.1.0 host-source scope decision](legal/release-decisions/2026-10-13-sdk-v2.1.0-host-source.md);
the final signed `sdk-v2.4.0` release follows the candidate. The SDK workflow exercises the extracted
archive on Linux and Windows/MSVC; the candidate workflow includes its zip and
SBOM sidecar and exercises the exact candidate zip. To test an existing archive,
set `SCO_HOST_SOURCE_ARCHIVE` when invoking the standalone archive test above.
The published archive bytes are immutable; a local build with the same version is
not proof of byte equality. No game,
CIG server, anti-cheat interaction, network runtime or GPU/renderer was exercised.
Products may retain their tagged submodule until they migrate to the published asset.
