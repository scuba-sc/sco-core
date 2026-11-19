# Star Citizen host-source kit

`host-source-sc` is a source archive for building the Star Citizen host
standalone. It is not a plugin SDK or an overlay. It holds the neutral source
kit, the game implementation, DataCore, private host composition headers, the
Lua adapter, vendored dependencies and synthetic fixtures. Every file is
classified in `sdk/components.json` with provenance, SBOM and checksum records.
The plugin ABI is 1.1. Plugin authors use `core` and `game-sc` instead.

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

The test checks the archive's file list, checksums, SBOM and provenance,
compiles the extracted tree and runs five host tests. It also checks that no
game file is in `core-source`. See [host composition](game/host.md) for the
lifecycle and its limits.

## Release archive

The host adapter sources, private headers, tests and docs are in this profile
only. None is a public plugin header (`public_header: false` in
`sdk/components.json`).

The kit of a release is its `sco-sdk-host-source-sc-<version>.zip` archive:

<!-- release-block:current-release-brief -->
SDK **2.4.1** uses Star Citizen game pack **0.8.1** (checked against Star Citizen
Alpha 4.10.3, build 4.10.200.58746; see [game builds](../docs/game/builds.md)) and plugin ABI **1.1**. Archives and their digests are on the
[`sdk-v2.4.1` release page](https://github.com/scuba-sc/sco-core/releases/tag/sdk-v2.4.1); the [release notes](https://github.com/scuba-sc/sco-core/blob/sdk-v2.4.1/docs/releases/sdk-v2.4.1.md) list what changed.
<!-- /release-block:current-release-brief -->

Verify the archive against the release's `SHA256SUMS`. The profile was first
published with SDK 2.1.0 ([scope decision](legal/release-decisions/2026-10-13-sdk-v2.1.0-host-source.md)).
The SDK workflow builds the archive and tests the extracted copy on Linux and
Windows (MSVC). To test an archive you already have, set
`SCO_HOST_SOURCE_ARCHIVE` when you run the standalone archive test above. A
local build with the same version is not byte-identical to the published
archive. No game, CIG server, anti-cheat, network runtime or GPU was exercised.
Products may keep their tagged submodule until they move to the published
archive.

## Adopting a new archive

Record the archive name, its SHA-256 and the plugin kit it pairs with. Extract
each into its own empty directory. Pin the host separately: an extracted plugin
kit does not upgrade a host.
