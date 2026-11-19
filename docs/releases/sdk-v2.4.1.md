# sco-core SDK 2.4.1 release notes

Planned tag [`sdk-v2.4.1`](https://github.com/scuba-sc/sco-core/releases/tag/sdk-v2.4.1), combined archive `sco-sdk-2.4.1.zip`.
Archive digests are on the release page, not here.

<!-- release-scope:start -->
```json
{"tag":"sdk-v2.4.1","sdk_version":"2.4.1","game_pack_version":"0.8.1","plugin_abi":"1.1","profiles":["core","game-sc","legacy","core-source","host-source-sc"]}
```
<!-- release-scope:end -->

## Compatibility

- Plugin ABI stays **1.1**. Plugins built against SDK 2.4.0 load unchanged; every addition
  below is a new optional interface or a new sized-table field behind an extent check.
- The game pack advances to **0.8.1** for Star Citizen Alpha 4.10.3 (build 4.10.200.58746,
  on LIVE since 2026-11-12). Pin it per build as before: `COMPATIBILITY.json.game-sc` names the
  build the archive was checked against. [Game builds](../game/builds.md) lists every build the
  rows were checked against and which rows changed from the build before; on 4.10.3 five rows
  were re-anchored.
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

**Also in 2.4.1**

- Widget renderer: an unlabelled ROW, COLUMN or TABLE draws no blank line, and a NUMBER slider
  shows whole values without decimals. Source kit only; events are identical.
- MCP servers: every tool has a title, a description that says what it returns and its limits, and
  a description for each argument. `search_api` matches any word of a multi-word query and ranks by
  match quality. `get_example` with an unsupported `language` lists the languages that have an
  example. The two workflow tools are listed only when a valid workflow index is loaded. Tool
  names, argument names and results are unchanged.
- `game.relocation`: a request made when the player is already at the target completes at once and
  moves nothing. Arrival is confirmed when the player is within 0.5 m of the target position;
  rotation is logged but no longer required. The failure log says when the move ran but arrival was
  not confirmed, and gives the first, closest and last pose seen after the move. Status, reason
  codes and the ABI are unchanged.
- `game.entities.prefabs` reports ready only when the `spawn.prefabs` rows resolved on the running
  build. Without them it starts OFF with a stated reason, and a placement returns
  `SCO_UNAVAILABLE` without latching.
- `sco.widgets.view` 1.0: a host-side reader for panels and a click queue for a host menu thread
  ([widgets view](../widgets-view.md)). Host-source seam in the `core-source` kit; not a plugin
  contract and not in the SDK catalogue.
- `game.loadout` 1.3: outfit slots 22-31 (clothing, hat, head accessory, head horn and jewellery)
  behind their own capability `game.loadout.outfit`, OFF by default. It turns on only with the async
  route and `SCO_LOADOUT_OUTFIT=1` in the product host ([loadout](../game/loadout.md#outfit-slots-13)).
- SDK reference launcher: a Rust workspace in `examples/launcher/` of the `core` kit, with the
  [launcher contract](../launcher-contract.md) and the guide
  [building a launcher](../building-a-launcher.md), and the new skill `sco-build-launcher`. It
  starts no game, injects nothing and changes nothing on the PC. The capability switches
  `exp_loadout_async`, `exp_inventory_read`, `exp_actor_stats_write`, `exp_cvars_native` and
  `exp_audio_native` default on and `exp_entities_diag` defaults off in the reference launcher.
  Its catalogue client checks a signed index, but no production signing key exists yet, so the
  trust root it ships with is empty and a catalogue works from a local folder only.

The complete list is the [`## sdk-v2.4.1` section of the changelog](../../CHANGELOG.md).

## Evidence

SDK tests for this release are synthetic: unit and fixture tests, headless widget rendering,
archive and checker tests, and the reference launcher's own test suite on synthetic folders and
zips. They do not by themselves show in-game behaviour.

Separately, the sc-offline product ran a development build of this source
(process-retained composition) in Star Citizen 4.10.3 (4.10.200.58746) during maintainer
test sessions: the host started, creative switches, vehicle spawn and seating, and exit work
on stop were observed in its logs. That is product evidence for one build and one setup, not
an SDK guarantee.

### Observed in game by the maintainer

On build 4.10.200.58746, one or two sessions each. These are observations, not verifications:
no registry row changes its evidence level or default because of them.

- Gear equip through `game.loadout.async` attached items (Game.log `AttachmentReceived`).
- Prefab placement through `game.entities.prefabs` placed an Outposts item.
- Ship spawn, boarding and Flight Ready worked.
- Character spawn worked.
- A saved-spot move really moved the player, while Core failed to confirm arrival. The arrival
  rule and the failure log above were changed for this; the change itself is not yet run in game.
- The seat probe logged Enter and Exit.

### Not run in game

Everything else in this release has synthetic evidence only ([game capability status](../game/status.md)). In particular:

- The relocation arrival rule (0.5 m, no rotation test) and the new failure log line.
- Prefab readiness from the resolved rows.
- `game.loadout` 1.3 outfit slots, the item-port names behind them, and the capability
  `game.loadout.outfit` (OFF).
- `sco.widgets.view` and any host menu built on it. No menu reader has run in a game.
- The reference launcher: it was built and tested on a PC without starting the game, and its
  capability switches, plugin installs and MCP server have not been used with a running game.
- The widget renderer change and the MCP changes (these need no game).
- Travel to another planet or moon by name: not supported, see Known limits.

## Known limits

- The strict (default) owned composition still refuses native activation; products that run
  in game use `Composition::ProcessRetained`.
- A direct engine quit does not dispatch `game.exit` or withdraw dependent services; a
  product-owned pre-quit signal remains necessary.
- Hauling semantics on 4.10.3 (build 4.10.200) are not verified.
- Zone lookup beyond the player's own zone chain (travel to another planet or moon by name) is
  not in 2.4.1. Core has no engine row to find a zone by name; see the
  [design note](../design/zone-lookup.md).
- The plugin catalogue has no signing key. Until the maintainer adds one at release time, the
  reference launcher's catalogue works from a local folder, and every entry it installs lands
  disabled.
- `game.loadout` outfit slots stay OFF unless a product sets `SCO_LOADOUT_OUTFIT=1`; their port
  names are not verified engine rows.

## Previous release

[SDK 2.4.0 release notes](sdk-v2.4.0.md).
