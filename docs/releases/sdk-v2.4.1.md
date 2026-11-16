# sco-core SDK 2.4.1 release notes

Planned tag [`sdk-v2.4.1`](https://github.com/scuba-sc/sco-core/releases/tag/sdk-v2.4.1), combined archive `sco-sdk-2.4.1.zip`.
**Release candidate source.** These notes describe the source that the release-candidate
workflow builds from `main`. The five profile archives, their SBOMs, `SHA256SUMS` and
`release.json` are produced once by that workflow and promoted without rebuilding; their
digests are recorded on the release page, not here. Nothing in this file approves tagging
or publication.

<!-- release-scope:start -->
```json
{"tag":"sdk-v2.4.1","sdk_version":"2.4.1","game_pack_version":"0.8.1","plugin_abi":"1.1","profiles":["core","game-sc","legacy","core-source","host-source-sc"]}
```
<!-- release-scope:end -->

## Compatibility

- Plugin ABI stays **1.1**. Plugins built against SDK 2.4.0 load unchanged; every addition
  below is a new optional interface or a new sized-table field behind an extent check.
- The game pack advances to **0.8.1** for Star Citizen build 4.10.200.58746. Pin it per
  build as before: `COMPATIBILITY.json.game-sc` names the build the archive was checked against.
- The five profiles are unchanged: `core`, `game-sc`, `legacy`, `core-source`, `host-source-sc`.
- Stable 2.4.0 archives remain on their release page with unchanged bytes.

## What is new

**Host composition and shutdown**

- `Composition::ProcessRetained` in the game host (`sco/game/host.h`): an explicit option in
  which the shell keeps all code loaded until process exit and attests install quiescence;
  the host runs the normal app runtime, and Stop closes admission without unloading. The
  strict default composition is unchanged, and the two exclude each other per process.
  See [the game host guide](../game/host.md).
- App shutdown refuses while a managed service pin is live; once begun, it closes managed
  acquisition, service publication and plugin loader entry until the next app start.
- `OwnedShutdown::request_stop` and `game::host::RequestStopDetailed` close cooperative
  admission without draining and report the drain state.
- Host startup stops before signature registration when inspection requests shutdown.

**Signatures**

- Pattern scans anchor on the pattern's rarest fixed byte, and RIP-relative `lea` rows share
  one per-pass index (`sco/scan.h`). `tools/sigcheck.sh` output is identical to the previous
  receipt; equivalence tests compare each replaced loop with a verbatim copy.

**Interfaces**

- Widgets 1.4: copied canonical event tags with size-gated callback tails, guarded tagged
  operations, and C++/C#/Lua routes. The ImGui renderer aligns form labels in one column,
  shares row width between columns, and caps combo popups.
- `sco.storage.metadata` 2.0 (`sco_storage_v2.h`, `scosdk/storage_metadata.hpp`): a separate,
  read-only view of an owner's database schema, row counts and SQLite quota. Storage 1.1 is
  unchanged. See [storage metadata](../storage-metadata.md).
- Read-only C++ wrapper for the `game.input` action registry.
- Opt-in historical plugin startup snapshot for the qualified host path.
- Startup diagnostics in the game-pack logs: per-capability readiness, once-per-reason refusal
  lines, relocation failure detail and Prepare phase timings. Logging only.

**Tools and kits**

- Bounded plugin archive verifier and non-loading folder preflight. They do not establish trust
  or runtime compatibility.
- The discovery MCP server supports legacy and metadata-based stdio clients and source-bound
  SDK workflow discovery.
- Experimental Rust ABI bindings and a sample plugin in the `core` profile. Not a gating
  check; the crate is not on crates.io.
- Archive checks preserve Git file modes, Linux UAPI header links and the C# build environment
  on Windows.

The complete list is the [`## sdk-v2.4.1` section of the changelog](../../CHANGELOG.md).

## Evidence

SDK tests for this release are synthetic: unit and fixture tests, headless widget rendering,
archive and checker tests. They do not by themselves show in-game behaviour.

Separately, the sc-offline product ran a development build of this source
(`2.4.1-dev.6`, process-retained composition) in Star Citizen 4.10.200.58746 during owner
test sessions: the host started, creative switches, vehicle spawn and seating, and exit work
on stop were observed in its logs. That is product evidence for one build and one setup, not
an SDK guarantee. Loadout equip has no verified game route in this release and is refused.

## Known limits

- The strict (default) owned composition still refuses native activation; products that run
  in game use `Composition::ProcessRetained`.
- A direct engine quit does not dispatch `game.exit` or withdraw dependent services; a
  product-owned pre-quit signal remains necessary.
- Hauling semantics on build 4.10.200 are not verified.

## Previous release

[SDK 2.4.0 release notes](sdk-v2.4.0.md).
