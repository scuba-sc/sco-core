# sco-core SDK 2.4.0 release notes

Tag [`sdk-v2.4.0`](https://github.com/scuba-sc/sco-core/releases/tag/sdk-v2.4.0), combined archive `sco-sdk-2.4.0.zip`.
**Published.** Stable SDK 2.4.0 was published on 2026-11-01 from `main` source commit
`6106fa3639cbcc28f78a1ded8de2707de019f4d3`. The release includes `SHA256SUMS` and `release.json`.
Not run in game. See [game capability status](../game/status.md).

<a id="stable-archive-identity"></a>

## Stable SDK 2.4.0 archive identities

These five ZIPs are the assets on the [`sdk-v2.4.0` release page](https://github.com/scuba-sc/sco-core/releases/tag/sdk-v2.4.0). The digests match that release's `SHA256SUMS`. Select the profile from `COMPATIBILITY.json` and verify the downloaded asset before extraction.

| Profile | Release asset | SHA-256 |
|---|---|---|
| `core` | `sco-sdk-core-2.4.0.zip` | `d2a8313ec5e3a7ae9ff95c253841b40e75454bec9799dccba5cbab9346432a7b` |
| `game-sc` | `sco-sdk-game-sc-0.8.0.zip` | `a5881e321bb456a3a17894413a07ece9ce6d3787bd6d1ad7dcc34fc288951f6b` |
| `legacy` | `sco-sdk-2.4.0.zip` | `0df4995624e9b094d24e9c5534b1ea7357f517c83a0a8c8ed6ad7d4d475afcbc` |
| `core-source` | `sco-sdk-core-source-2.4.0.zip` | `df756719da0de54ead18973258d3bc35640e26d44088b3b6ffcf8672ebb1ee51` |
| `host-source-sc` | `sco-sdk-host-source-sc-2.4.0.zip` | `a0aaaba07e3d4eb0d8c264912cd6f2ab23f0d237a99fa07816005790a76efc4c` |

`sdk-v2.4.0-rc.1` and `sdk-v2.4.0-rc.2` are historical. Their archives are on their own release pages. The release workflow publishes the CHANGELOG section [`## sdk-v2.4.0`](../../CHANGELOG.md) as the release's notes; this page is the longer reference behind it.

<!-- release-scope:start -->
```json
{"tag":"sdk-v2.4.0","sdk_version":"2.4.0","game_pack_version":"0.8.0","plugin_abi":"1.1","profiles":["core","game-sc","legacy","core-source","host-source-sc"]}
```
<!-- release-scope:end -->

## Published RC2 archive identity

The release API records the RC2 combined `sco-sdk-2.4.0.zip` archive with SHA-256
`51a7ce266d087bcbeac39c92f26a6f905b4473883dd7ed6f0f5ca823fa88c88e`.
RC2 was built from source commit `844e136ccb280ad8cb43f73bc43c9447e8f6799d`. Verify a downloaded RC2
archive against that release's `SHA256SUMS`.

## What this release does

This release adds read-only views of the game for plugins and gives two existing features a real
implementation. The plugin interface stays exactly as it was. "Checked against build 4.10.197.2878"
means the game's data is found in the game file on disk, not that a feature has been seen working.

- Plugins built for 2.3.0 keep working: the plugin interface (ABI 1.1) is byte-identical to 2.3.0.
- ABI 1.1 plugins can run on older hosts when their required services and manifest
  requirements are satisfied. Optional lookups must handle NOT_FOUND or UNAVAILABLE.
  New widget and storage features need their own version and complete-member checks.
- New: read an actor's hunger, thirst, temperature, oxygen, stun, distortion and radiation
  (`game.actor_stats`); change one stat of your own player, off by default (`game.actor_stats_write`);
  read the server's quantum-travel settings and list the 15 parameter definitions
  (`game.gamerules` 1.1, read off, no backend); look up any of 1,297 named input actions by id or name
  (`game.input` 1.0, metadata only; ids are scoped to build 4.10.197.2878); ask which game
  build is running (`game.build`); an event when an accepted mission changes
  (`game.mission.changed`, publishes nothing until a missions backend exists); an event when a loadout
  respawn completes (`game.loadout.respawn_completed`, default off, native hook unresolved); and the
  inventory snapshot with its 192 item categories (`game.inventory` 1.1, read held off).
- Changed: teleport (`game.relocation`) and quantum boost (`game.quantum`) have real implementations
  behind the same tables. Relocation turns on only when offline play is confirmed and the game's
  data is found. Quantum control stays off.
- New, off by default: a fire-and-forget sound trigger (`game.audio` 1.1 `play_once`, no native
  backend yet); guarded native reads of the game's in-memory DataCore records (`game.datacore_live`,
  explicit opt-in, no writes); guarded native reads of four integer CVars (`game.cvars`, opt-in). A
  native CVar write is refused in production, and a regression test pins that.
- Contracts only, natively unavailable: filtered single-hit rays, exact class-name entity lookup,
  value-change GameRules reads and fire-and-forget audio. Plugins can be written and tested against
  their synthetic providers. In the game these calls answer unavailable, and each native seam installs
  nothing until its route is grounded. The [2.4.0 scope record](../legal/release-decisions/2026-10-29-sdk-v2.4.0-scope.md)
  lists them.

## Versions

| What | Version | Where |
|---|---|---|
| Kernel (the SDK) | 2.4.0 (was 2.3.0) | `sdk/VERSION`, tag `sdk-v2.4.0` |
| Plugin ABI | 1.1, unchanged | `SCO_API_MAJOR` / `SCO_API_MINOR` in `sco_api.h` (frozen; `include/sco_api.h` and `tests/abi_v1.c` byte-identical to 2.3.0) |
| `sco.widgets` | 1.2 | Stable tags in a 128-byte node; copied `const char* tag` at offset 104; typed language support is operation-specific |
| `sco.storage` | 1.1 | 144-byte table with bounded table/view and column enumeration |
| `game.actor_stats` service table | 1.1 (29 raw read IDs; original write scope unchanged) | `sc_actor_stats.h` |
| `game.inventory.contents` service table | 1.0 (copied snapshots; native adapter unavailable) | `sc_container_contents.h` |
| `game.datacore_live` service table | 1.0 (explicit verified host backend; guarded native reads, opt-in, default OFF) | `sc_datacore_live.h` |
| `game.audio` service table | 1.1 (`play_once` appended, 32 to 40 bytes; owner-bound effects; native OFF) | `sc_audio.h` |
| `game.entities` service table | 1.2 (copied lookup tail; native lookup OFF) | `sc_entities.h` |
| `game.world.control` service table | 1.1 (bounded multi-hit; native OFF) | `sc_world_control.h` |
| `game.loadout` service table | 1.2 (asynchronous submit/status/cancel; opt-in) | `sc_loadout.h` |
| `game.actor_stats_write` service table | 1.0 (new) | `sc_actor_stats_write.h` |
| `game.gamerules` service table | 1.1 (new; `list_params` appended) | `sc_gamerules.h` |
| `game.build` service table | 1.0 (new) | `sc_game_build.h` |
| `game.input` service table | 1.0 (new) | `sc_input.h` |
| `game.inventory` service table | 1.1 (new; item categories, `flags`, `total_seen`) | `sc_inventory.h` |
| `game.mission.changed`, `game.loadout.respawn_completed` bus events | new records | `sc_game_events.h` |
| `game.relocation`, `game.quantum` service tables | unchanged | production backends added behind the tables |
| Star Citizen game pack | 0.8.0 (was 0.6.0 in SDK 2.3.0) | `SCO_GAME_PACK_VERSION` in `sc_game_pack.h` |
| Verified game build | 4.10.197.2878 (was 4.10.196.36804 in 2.3.0) | `SCO_GAME_PACK_VERIFIED_BUILD`; rows verified by sigcheck, in-game verification pending |

`VERSIONS.txt` in the zip states the same lines, generated from the headers by `sdk/package.py`.

Optional contracts also add owner-bound `game.audio` 1.0 (grown to 1.1 by `play_once`),
copied registry, name and component-presence queries in `game.entities` 1.2 and scoped
bounded multi-hit in `game.world.control` 1.1. Each has a synthetic provider fixture. None
supplies a native engine adapter or turns production availability ON. Audio acceptance does not
certify audible playback. Entity lookup tails are native C/C++ only. Managed additions preserve layout
but add no callable route.

## New contracts in 2.4.0

These contracts are in this release. The table does not attest native availability or game acceptance.

| Contract | Version | Default | Evidence |
|---|---|---|---|
| `game.build` | 1.0 | ON, pure metadata | `synthetic_only` |
| `game.actor_stats` | 1.1 (with `game.actor_stats_write` 1.0) | read ON when its rows resolve; write OFF; appended read IDs are not writable | `sigcheck_resolved` (read), `off_pending_probe` (write) |
| `game.inventory` | 1.1: `flags` and `total_seen` after `ports[]` (a 1.0-sized caller is unchanged), the 192-member `sc_item_type` enumeration with `sc_inventory_item_type_name` and `sc_inventory_port_item_type`, and a production adapter with nine new signature rows | `game.inventory.read` OFF | `synthetic_only` |
| `game.input` | 1.0: read-only action registry, 1,297 named actions (ids 1..1297), lookup by id and by name; no input injection | `game.input.registry` ON, which advertises metadata availability only | `synthetic_only`; ids and names re-derived statically from the 4.10.197.2878 action registry, build-scoped |
| `game.gamerules` | 1.1: `list_params` appended, 15 name, group and type definitions whose names are verified on 4.10.197.2878 (types not verified); no live values; `read` is unchanged | `game.gamerules.read` OFF | `synthetic_only`; names build-scoped to 4.10.197.2878 |

The bus event `game.loadout.respawn_completed` is also in this release: production capability OFF, native hook unresolved. `game.inventory` 1.1 has only static evidence.

## Coordinates and distribution

- Core SDK, combined/legacy SDK, the neutral core-source kit and the Star Citizen
  host-source kit (`host-source-sc`): 2.4.0.
- Optional game-sc public overlay: 0.8.0 (was 0.6.0). The release adds and extends game service tables,
  and the [game-pack policy](../game-pack.md) raises the minor for a release that adds or grows a
  game service. The overlay ships as `sco-sdk-game-sc-0.8.0.zip`, so its compatibility descriptor,
  which binds the exact 2.4.0 core archive SHA-256, never shares a file name with an earlier
  overlay. Consumers check `COMPATIBILITY.json.game-sc` and the required base digest, never the
  version string alone.
- Frozen plugin ABI: 1.1, unchanged. Individual service versions remain independent.

The five profiles are the same as in 2.3.0 (`core`, `game-sc`, `legacy`, `core-source`,
`host-source-sc`). The assignment of files to profiles is the `sdk/components.json` classification.
The `game-sc`, `legacy` and `host-source-sc` profiles carry the native-index and status metadata,
with generated catalogues that match the source.

## Changes since 2.3.0

The [CHANGELOG `## sdk-v2.4.0` section](../../CHANGELOG.md) holds the complete entries and their
limits. In short:

| Change | Where | Profiles (as packaged today) |
|---|---|---|
| `game.cvars` native reference-integer read adapter, default OFF; native writes held for object/backing lifetime evidence and refused in production (pinned by a regression test); separate fresh host-stop cleanup admission | `src/game/services/cvars_engine.*`, `cvars_production.cpp`, [docs](../game/cvars.md) | implementation and synthetic Windows host-stop fixtures in host-source-sc |
| `game.actor_stats` 1.1: read 29 raw named stats by actor; no injury-tier or normalized-unit claim | `include/sc_actor_stats.h`, [docs](../game/actor-stats.md) | header in game-sc and host-source-sc |
| `game.inventory.contents` 1.0: bounded copied stored-item snapshots, generation and optional volume; unavailable without a grounded host provider | `include/sc_container_contents.h`, [docs](../game/container-contents.md) | public contract/example in game-sc; provider in host-source-sc |
| `game.datacore_live` 1.0: host-backed pool access, fresh offline admission and separate write opt-in; guarded native counted reads, GUID record lookup and text-blob reads, default OFF, needing explicit pre-engine quiescence; native writes unavailable | `include/sc_datacore_live.h`, [docs](../game/datacore-live.md) | public contract in game-sc; provider in host-source-sc |
| `game.audio` 1.1: `play_once` fire-and-forget trigger with its own default-OFF capability; no native backend | `include/sc_audio.h`, `include/scosdk/game/audio.hpp`, [docs](../game/audio.md) | public contract in game-sc; provider seam in host-source-sc |
| `game.loadout` 1.2: copied asynchronous submit/status/cancel with native engine path behind explicit opt-in | `include/sc_loadout.h`, [docs](../game/loadout.md) | public contract in game-sc; provider in host-source-sc |
| `game.actor_stats_write` 1.0: `adjust`, `set`, `mode` for your own player, behind a product switch and fresh offline admission | `include/sc_actor_stats_write.h`, `src/game/services/`, [docs](../game/actor-stats.md) | header in game-sc and host-source-sc; implementation in host-source-sc |
| `actor_stats_probe`: in-game test plugin and the maintainer's checklist | `sdk/examples/actor_stats_probe/` | game-sc |
| `game.gamerules` 1.0 with C++20, C# and Lua read wrappers; 1.1 appends `list_params` | `include/sc_gamerules.h`, `include/scosdk/game/gamerules.hpp`, `sdk/csharp/Sco.Sdk/Game/GameRules.cs`, [docs](../game/gamerules.md) | headers and wrappers in game-sc and host-source-sc |
| `game.build` 1.0: running-exe identity and the pack's verified-build pin as data | `include/sc_game_build.h`, [docs](../game/build.md) | header in game-sc and host-source-sc |
| `game.inventory` 1.1: read-only item-port snapshot, item categories, production adapter (read held OFF) | `include/sc_inventory.h`, [docs](../game/inventory.md) | header in game-sc and host-source-sc; implementation in host-source-sc |
| `game.input` 1.0: read-only action registry, 1,297 names | `include/sc_input.h` | header in game-sc and host-source-sc |
| `game.mission.changed` and `game.loadout.respawn_completed` bus events | `include/sc_game_events.h`, [events](../game/events.md) | header in game-sc and host-source-sc |
| `game.relocation` production path (new row `teleport.relocation`, product switch `SetRelocationEnabled`) | `src/game/services/relocation*.{h,cpp}`, [game services](../game-services.md) | host-source-sc |
| `game.quantum` default production backend | `src/game/services/quantum_engine.cpp`, `quantum_owned.*`, [quantum](../quantum.md), [cvars](../game/cvars.md) | host-source-sc |
| Verified build 4.10.197.2878 | `include/sc_game_pack.h`, [game pack](../game-pack.md) | header in game-sc and host-source-sc |
| Windows diagnostics: `NtQuerySection` takes `SIZE_T` lengths | `src/ipc/diagnostics_platform_win.cpp` | host-source-sc |
| `sco-new-plugin.py` and MCP `generate_plugin_scaffold`: `--lang csharp` (NativeAOT project) and `--lang lua`; stand-in checks only | `sdk/tools/sco-new-plugin.py`, `tools/sco-core-mcp/server.py`, [SDK README](../../sdk/README.md#generate-a-plugin-folder) | core and core-source (the C# layer it targets ships in core) |
| MCP documentation at the current release with drift gates; tooling and Windows gate hardening | `tools/`, `docs/sdk-mcp-tutorial.md` | repository |
| Guide search in the discovery MCP server: `search_guides` and `get_guide_section` behind `--extended-tools` (default six tools unchanged), over `sdk-guides.json` built by `tools/sdk_guides.py` from the public guides | `tools/sco-core-mcp/`, [agent integration](../agent-integration.md) | `sdk-guides.json` beside `sdk-catalogue.json` in the combined kit, game-sc and host-source-sc |
| Discovery MCP server: three opt-in development tools behind `--enable-dev-tools` and `--project-root` (`run_plugin_check`, `build_plugin_project`, `test_plugin_project`); default and extended tool lists unchanged | `tools/sco-core-mcp/`, [agent integration](../agent-integration.md) | repository only |
| Game status table and engine-native index regenerated | [evidence status](../game/status.md), `sdk/native_index.json` | repository; the native index stays on the docs site and static MCP only |

Profiles are the `sdk/components.json` classification; `legacy` includes `core` and `game-sc`, and
`host-source-sc` includes `core-source`.

Also in 2.4.0, added after RC1. Each ships a contract, a synthetic provider and a
refusal path. None is enabled.

- [Inventory 1.2 logical layouts](../game/inventory.md#logical-layout-observations-12)
  copy bounded observations. The production host has no layout producer and the capability is OFF.
- [Position-only relocation 1.1](../game/relocation-position.md) has its own capability and guards.
  The default native adapter supplies no position callbacks.
- [ASOP selection 1.0](../game/asop-selection.md) edits a host-owned document through an admitted
  coordinator. It does not establish native terminal publication or observation.
- [Widgets 1.3 numeric presentation](../../sdk/docs/widgets.md#numeric-presentation-13)
  requires a supporting host and explicit version, extent and feature checks.
- **`game.audio.play_once`** resolves the three Wwise entry points by exported name from the
  client's export directory. The adapter refuses to install without a grounded witness that the audio
  pump's shared game object is still registered when a submission lands, so nothing is posted and both
  audio capabilities stay OFF ([audio](../game/audio.md)).
- **`game.gamerules.read`** has a native seam that installs only with a read-only accessor for the
  params blob and a host-owned change counter. No read-only accessor is grounded, so production
  resolves nothing, refuses and names the first missing part, and the read stays OFF
  ([gamerules](../game/gamerules.md)).
- **`game.entities.lookup`** has a native point-lookup seam that installs only with the registry
  receiver lifetime. No native adapter binds and the capability stays OFF ([entities](../game/entities.md)).
- **`game.world.raycast_filtered`** has a native single-hit seam that installs only with the resolved
  zone, world and grid references and a skip-request lifetime. The synthetic policy remains the only
  behaviour and the capability stays OFF ([world](../game/world.md)).

The [game status table](../game/status.md) records each of these four as OFF.

## Capabilities and evidence

Four states per capability: contract (header and ABI pins), implementation, availability (default),
evidence.

| Capability | Default | Evidence level | What is true |
|---|---|---|---|
| `game.cvars.read` | OFF; explicit native read opt-in | `synthetic_only` | Current-build integer method identities and actual Windows adapter tested on synthetic objects |
| `game.cvars.write` | OFF; production native port refuses writes | `synthetic_only` | Setter/restore mechanics and actual host-stop cleanup tested; native lifetime across callbacks and leases remains ungrounded |
| `game.actor_stats.read` | ON when its rows resolve | `sigcheck_resolved` | Real reads; rows resolve on 4.10.197.2878; units and ranges of each stat unconfirmed |
| `game.actor_stats.write` | OFF; product switch plus fresh offline admission | `off_pending_probe` | Real adjust through the game's own function; whether local-only writes persist is unknown; nothing is restored on unload |
| `game.gamerules.read` | OFF | `synthetic_only` | Contract and wrappers; no backend bound, so calls answer unavailable; `list_params` serves static definitions. The native seam refuses without a grounded read-only accessor and a host-owned change counter |
| `game.audio.triggers` | OFF | `synthetic_only` | Owner-bound trigger and playback tokens on a synthetic provider; no native or audible verification |
| `game.audio.play_once` | OFF | `synthetic_only` | Fire-and-forget on a synthetic provider; OK means accepted only. The Wwise exports resolve, and the adapter refuses to install without a registration witness |
| `game.datacore_live` | OFF; explicit native read opt-in | `synthetic_only` | Guarded native reads tested on synthetic hosts; activation needs pre-engine quiescence; writes unavailable |
| `game.entities.lookup` | OFF | `synthetic_only` | Copied lookup on a synthetic provider; the native point-lookup seam refuses without the registry receiver lifetime |
| `game.world.raycast_filtered` | OFF | `synthetic_only` | Filtered rays with copied results on a synthetic provider; the native single-hit seam refuses without the resolved zone, world and grid references and a skip-request lifetime |
| `game.input.registry` | ON (metadata only) | `synthetic_only` | 1,297 named actions, ids 1..1297 on 4.10.197.2878; no injection |
| `game.events.loadout_respawn_completed` | OFF | `synthetic_only` | Event contract; native hook unresolved |
| `game.build.info` | ON | `synthetic_only` | Pure metadata, no rows |
| `game.events.mission_changed` | ON (a product default) | `synthetic_only` | Fires only where a missions backend reports missions; none does today |
| `game.inventory.read` | OFF | `synthetic_only` | Adapter bound in production behind fail-closed signature-row and bounds checks; held OFF; synthetic objects only |
| `game.relocation` | ON only while offline admission is fresh, rows resolve, a live player and known system exist and the product switch is on (was OFF) | `synthetic_only` | Real implementation |
| `game.quantum.control` | OFF (unchanged) | `synthetic_only` | Real backend that restores what it changes |

## Not in this release

- A live inventory read: the adapter exists but `game.inventory.read` is held OFF by default.
- `p_enable_physical_quantum_travel` writes: there is no grounded route, so the quantum backend
  does not write it. Console command execution for quantum control is not offered.
- sco.net server-host and the larger session limit are not part of the kernel.
- Any change to a `sco_api` table.
- Enabled native backends for filtered single-hit rays, exact class-name entity lookup, value-change
  GameRules reads and fire-and-forget audio. Their contracts and synthetic providers ship, and each
  seam refuses without its grounded route, because the evidence does not yet show that the game
  objects each call touches stay valid for the whole call. Every one of these capabilities stays OFF.

## Engineering limits

Evidence is host-side builds and tests, synthetic backends, ABI pins and static `sco-sigcheck` runs
against the build's `StarCitizen.exe` on disk. The Windows-only engine code (`relocation_engine.cpp`,
`quantum_engine.cpp`, the actor-stat engine reads) is compiled by hosted CI, not in the local builds.
Real-game, anti-cheat, official-network and private-LAN acceptance remain unproven.

## Checking the zip

Download the archives you need and `SHA256SUMS` from the `sdk-v2.4.0` release, then run
`sha256sum --ignore-missing`, which skips the lines for files you did not download:

```sh
gh release download sdk-v2.4.0 -R scuba-sc/sco-core -p 'sco-sdk-2.4.0.zip' -p SHA256SUMS
sha256sum --ignore-missing -c SHA256SUMS
unzip -p sco-sdk-2.4.0.zip sco-sdk-2.4.0/VERSIONS.txt
```
