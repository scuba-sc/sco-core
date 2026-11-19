# Status of every game.* table

<!-- release-block:current-release -->
SDK **2.4.1** (release candidate 1, a pre-release for testing; the final release follows) uses Star Citizen game pack **0.8.1** (checked against Star Citizen
Alpha 4.10.3, build 4.10.200.58746; see [game builds](../../docs/game/builds.md)) and plugin ABI **1.1**. Download it from the
[`sdk-v2.4.1-rc.1` release page](https://github.com/scuba-sc/sco-core/releases/tag/sdk-v2.4.1-rc.1) and verify each archive against that release's
`SHA256SUMS`; the [release notes](https://github.com/scuba-sc/sco-core/blob/sdk-v2.4.1-rc.1/docs/releases/sdk-v2.4.1.md) list what changed. The previous release is
[SDK 2.4.0](https://github.com/scuba-sc/sco-core/blob/sdk-v2.4.0/docs/releases/sdk-v2.4.0.md).
<!-- /release-block:current-release -->

Game pack <!--rv:game_pack_version-->0.8.1<!--/rv--> pins game build **<!--rv:game_build-->4.10.200.58746<!--/rv-->** (PE timestamp `0x6af4861d`, [game pack](../game-pack.md#verified-game-builds)). Each evidence string below names the game build it was recorded on; most were recorded on 4.10.196.36804. A pin bump does not establish native or in-game behavior.

The table has one line per service table and per capability: its version, its [stability](../stability.md) label, whether it is on by default, and how far it has been checked.

> ON records the default availability policy, subject to readiness, admission and product conditions. It does not certify gameplay. Some capabilities expose static metadata without signature rows.

> **Startup is on hold.** Production game-service startup refuses before publishing
> these tables. The defaults and evidence below describe contracts and provider
> readiness, not current production availability. See [readiness](../defaults-readiness.md)
> and [service-table lifetime](../design/service-table-lifetime.md).

## How to read it

**Stability.** *Stable* or *Experimental*, per table: a compatibility promise about the table's layout and lifetime within SDK 2.x, decided by the rule in [Stability](../stability.md#how-a-game-table-gets-its-label) (a new label is Stable when at least one capability has a `sigcheck` row result, an OFF-pending-probe line or a dated in-game result, otherwise Experimental; once a released SDK has carried Stable, the table stays Stable for the rest of SDK 2.x). The `game.*` event bus row's label covers its event names and payload structs. A `-` marks a signature-row group (mining rows, CryPak adapter rows): it is not a service table, its rows can stop resolving with a game patch, and it carries no stability promise. A label says nothing about availability or evidence: a Stable table can still be OFF, answer `SCO_UNAVAILABLE`, or have no in-game run.

**Default.** *ON*: default availability policy, still subject to each row's readiness, admission and product conditions ([game services](../game-services.md#for-products)). *OFF*: the operation is held off; a published table can still answer `SCO_UNAVAILABLE`. *none*: no independent capability gates the line, or no blocked surface is published. Defaults are not execution evidence.

**Source.** The service table below is generated from `sdk/status_registry.json` by `python3 tools/status_registry.py --write`, and CI fails when the two differ (`--check`). Change the registry, never the table. Its evidence levels restate what the linked pages already record.

**Evidence level**, exactly one of:

| Level | Means |
|---|---|
| `row resolved on 4.10.196.36804 (sigcheck)` | Every row the capability reads is `OK` in a `sco-sigcheck` report of that executable. The byte checks pass and each locator matches once. Nothing was executed in the game |
| synthetic fixtures only | Provider, ABI and native-boundary tests with synthetic objects, or static analysis. No signature row, or no adapter. Not run in game. |
| `OFF pending in-game confirmation` | The rows resolve, but something only a run can show is open (the thread a callback runs on, a vector layout, which of two checks means dead). The capability stays off until that run |
| `BLOCKED` | No locator we can trust, so nothing is published |
| `undocumented` | The repository records no evidence. No entry has this level |
| `verified in game (date)` | A dated run in a game session. No entry has this level |

## Loadout (`game.loadout`)

`game.loadout` 1.3 / `game.loadout.apply`: production startup currently refuses
before the table is published. Typed engine methods are unverified: there is no
trusted per-slot equip mapping and no offline admission adapter installed. See
[loadout](loadout.md).

`game.loadout.weapons` (added in 1.1) gates the weapon and multitool slots 8-21
of `apply` and is **OFF by default**, vocabulary only. A fixture or service started
without an engine backend returns unavailable for those slots, and production
currently does not publish the table. No per-slot port mapping is checked. The
service version macro `SC_LOADOUT_SERVICE_VERSION` retains the synchronous prefix
version. The separate 1.2 asynchronous adapter is opt-in. The native respawn event
producer is unresolved and default OFF.

`game.loadout.outfit` (added in 1.3) gates outfit slots 22-31 of `submit` (clothing,
hat, head accessory, head horn, jewellery). It is **OFF by default**: it is on only
with the opt-in async route and the product switch `SCO_LOADOUT_OUTFIT=1`. Hair,
hair colour, eyebrow, head, body, eyes and teeth have no slot. The item-port names are
not verified engine rows. See [loadout](loadout.md#outfit-slots-13).

## Quantum control (`game.quantum`)

`game.quantum` 1.0 / `game.quantum.control` is on only after the production adapter
configures: every row it needs resolves, fresh positive offline admission holds at
start, and the shared owner-release dispatcher is installed. Otherwise it stays off.
The registry records the default as OFF, the conservative value. Every enable or hold
call still requires fresh positive offline and EAC-absent admission, an owned drive,
the game thread and bounded values. No additional public table or header and no
kernel mutation route is added.

The adapter (Windows game pack) writes four int quantum cvars and one shared drive
parameter cell only while a hold is active. It saves the prior value first and
restores it on every release path; see [quantum](../quantum.md). The tests use a fake
engine. Windows/MSVC behavior, the actual drive input, charge and effects, and the
`StartUse` user match are not verified.

## Private-session broker (`game.private_session`)

`game.private_session` 1.0 ([public C header](../../include/sc_private_session.h),
[host policy](private-session.md)) is **on by default**.
Default bind and exact peer allowlist are `127.0.0.1`; startup publishes the
broker without sockets. Host/Join needs fresh explicit UI consent, a 12-256-byte
passphrase, and positive offline/EAC/official-network/target admission. The host,
not plugins, controls explicit private LAN bind and 1-16 exact peer addresses.
Authenticated backend messages keep existing size/send/inbox quotas; global
broker requests are bounded at four/second and raw ingress at 4,096/second.
Admission loss, owner unload and host stop close the session; lifecycle records
contain no passphrase, player name or endpoint. The portable real-backend and
native-C service and ABI checks are synthetic. Windows/MSVC, private LAN, EAC
inspection and firewall policy are not verified. Relocation has its own admission
gates ([relocation](../game-services.md#relocation-gamerelocation-10)).

## Service tables and capabilities

`game.prefabs` 1.0 uses the existing object-container spawner and attribute writer
and entities ownership. Capability `game.entities.prefabs` is **on by default**.
Startup and cleanup readiness, provider-fault refusal, canonical 511-byte content
paths, finite normalized poses and the shared 1,024 and per-owner 128 entity quotas
are still enforced. Windows/MSVC and native SEH behavior are not verified. Streamed
materialization, cancellation before first stream, preview and unload cleanup and
native attribute, spawn and batch-release fault handling are not checked. The
admission and ABI fixtures are synthetic.

`game.owned` 1.0 ([header](../../include/sc_owned.h),
[policy](../design/owned-removal.md)) is a game-pack composition over the existing
entities and actors removal and seat-read capabilities below, not a new engine
binding. Only synthetic provider and ownership fixtures and header layout checks
are supplied. Its receipts record initiation, not disappearance. Retained build
ownership lasts only while loaded, and ordinary unload removes placements. C and
C++ direct routes; C# and Lua unsupported.

<!-- status-registry:start -->
| Table | Version | Stability | Capability | Default | Evidence |
|---|---|---|---|---|---|
| `game.asop.selection` ([`sc_asop_selection.h`](../../include/sc_asop_selection.h)) | 1.0 | Experimental | `game.asop.selection.file` (`state`, `refresh`, `rows`, `acquire`, `release`, `replace`, `status`) | OFF | Fixed-document coordinator with synthetic adapter tests. Product file adapter and native terminal integration are not admitted. Not run in game. |
| `game.audio` ([`sc_audio.h`](../../include/sc_audio.h)) | 1.1 | Experimental | `game.audio.triggers` (`resolve`, `play`, `stop`) | OFF | Owner-bound trigger and playback tokens, host release cleanup and bounded synthetic provider tests. No native adapter. Not run in game. |
| | | | `game.audio.play_once` (`resolve`, `play_once`) | OFF | Independent default-OFF capability with owner-bound tokens, extent-checked v1.1 tail, fault latch until process restart and synthetic provider tests. The native adapter resolves the Wwise one-shot exports by name from the client export directory and refuses to install without a grounded witness that the pump's game object is still registered at submission. Not run in game. |
| `game.inventory.contents` ([`sc_container_contents.h`](../../include/sc_container_contents.h)) | 1.0 | Experimental | `game.inventory.contents.read` (`snapshot`) | OFF | Copied bounded container-item read provider with real-host consumer and ABI fixtures; no native adapter. Not run in game. |
| `game.datacore_live` ([`sc_datacore_live.h`](../../include/sc_datacore_live.h)) | 1.0 | Experimental | `game.datacore_live` (`is_ready`, `get_pool_stats`, `find_record`, `read_pool`, `write_pool`) | OFF | Default-OFF native counted read adapter with validated lifecycle locators, serialized text-blob bytes and inherited record metadata. Requires shell-enforced pre-engine quiescence, lifecycle exclusion and fresh admission. Native writes unavailable. Synthetic service, hook and fault tests. Not run in game. |
| `game.input` ([`sc_input.h`](../../include/sc_input.h)) | 1.0 | Experimental | `game.input.registry` (`count`, `by_id`, `by_name`) | ON | Static metadata lookups only. The C++20 header-only consumer wrapper is available; synthetic checks do not establish live production-provider availability. 1,297 actions, ids 1..1297, re-derived statically from the 4.10.197.2878 action registry. No input dispatch. Not run in game. |
| `teleport.spatial` ([`sc_spatial.h`](../../include/sc_spatial.h)) | 1.0 | Stable | the four `teleport.*` rows (the product's capability `teleport`) | ON | row resolved on 4.10.196.36804 (sigcheck) |
| `spawn.entities` ([`sc_spawn.h`](../../include/sc_spawn.h)) | 1.2 | Stable | the `spawn.helpers` rows (the product's capability `spawn.ship`) | ON | row resolved on 4.10.196.36804 (sigcheck) |
| `game.actors` ([`sc_actors.h`](../../include/sc_actors.h)) | 1.1 | Stable | `game.actors.local_player` | ON | row resolved on 4.10.196.36804 (sigcheck) |
| | | | `game.actors.spawn_npc` | ON | row resolved on 4.10.196.36804 (sigcheck) |
| | | | `game.actors.despawn` | ON | row resolved on 4.10.196.36804 (sigcheck) |
| | | | `game.actors.health` | ON | row resolved on 4.10.196.36804 (sigcheck) |
| | | | `game.actors.state` | OFF | OFF pending in-game confirmation |
| `game.actor_stats` ([`sc_actor_stats.h`](../../include/sc_actor_stats.h)) | 1.1 | Stable | `game.actor_stats.read` (`read`) | ON | row resolved on 4.10.197.2878 (sigcheck); actor.stat_names keeps each named stat on its game index; stat units, range and direction are unconfirmed. Not run in game. |
| `game.actor_stats_write` ([`sc_actor_stats_write.h`](../../include/sc_actor_stats_write.h)) | 1.0 | Stable | `game.actor_stats.write` (`adjust`, `set`, `mode`) | OFF | OFF pending in-game confirmation |
| `game.vehicles` ([`sc_vehicles.h`](../../include/sc_vehicles.h)) | 1.0 | Stable | `game.vehicles.seats` | ON | row resolved on 4.10.196.36804 (sigcheck) |
| | | | `game.vehicles.seat` | ON | row resolved on 4.10.196.36804 (sigcheck) |
| | | | `game.vehicles.flight_ready` | ON | row resolved on 4.10.196.36804 (sigcheck) |
| `game.world.control` ([`sc_world_control.h`](../../include/sc_world_control.h)) | 1.1 | Experimental | `game.world.raycast_filtered_many` (`raycast_filtered_many`) | OFF | Bounded scoped multi-hit policy and lifecycle tests; no native grid or hit adapter. Not run in game. |
| | | | `game.world.raycast_filtered` (`raycast_filtered`) | OFF | Synthetic policy only; no verified offline engine adapter: the native single-hit seam installs nothing without the resolved zone/world/grid and a skip-request lifetime. Not run in game. |
| | | | `game.world.free_camera` (`camera_acquire`) | OFF | Synthetic policy only; no verified offline engine adapter. Not run in game. |
| `game.world` ([`sc_world.h`](../../include/sc_world.h)) | 1.1 | Stable | `game.world.raycast` | ON | row resolved on 4.10.196.36804 (sigcheck) |
| | | | `game.world.camera` (position, rotation, zone) | ON | row resolved on 4.10.196.36804 (sigcheck) |
| | | | `game.world.free_camera_flag` (`free_camera_flag`, read-only) | OFF | OFF pending in-game confirmation |
| | | | camera field of view | none | BLOCKED |
| `game.entities` ([`sc_entities.h`](../../include/sc_entities.h)) | 1.2 | Stable | `game.entities.lookup` (`list_classes`, `find_class`, `find_by_name`, `component`) | OFF | Copied registry/name/typed-presence provider policy and ABI tests; the native point-lookup seam installs nothing without the registry receiver lifetime, so no native adapter binds. Not run in game. |
| | | | `game.entities.transform` (`alive`, `get_transform`, `set_transform`) | ON | row resolved on 4.10.196.36804 (sigcheck) |
| | | | `game.entities.spawn` (`spawn`, `despawn`, `keep`) | ON | row resolved on 4.10.196.36804 (sigcheck) |
| | | | `game.entities.class_of` | ON | row resolved on 4.10.196.36804 (sigcheck) |
| | | | `game.entities.query_radius` | OFF | OFF pending in-game confirmation |
| | | | `game.entities.watch` (`watch`, `unwatch`) | OFF | OFF pending in-game confirmation |
| `game.*` events ([`sc_game_events.h`](../../include/sc_game_events.h)) | bus | Stable | `game.events.player_spawned` (`game.player.spawned`) | ON | row resolved on 4.10.196.36804 (sigcheck) |
| | | | `game.events.player_died` (`game.player.died`) | ON | row resolved on 4.10.196.36804 (sigcheck) |
| | | | `game.events.actor_died` (`game.actors.died`) | ON | row resolved on 4.10.196.36804 (sigcheck); the same row as player_died, one hook two events |
| | | | `game.events.zone_changed` (`game.zone.changed`) | ON | row resolved on 4.10.196.36804 (sigcheck) |
| | | | `game.events.vehicle_seat` (`game.vehicle.boarded` / `exited`) | OFF | OFF pending in-game confirmation |
| | | | `game.events.mission_changed` (`game.mission.changed`) | ON | On as a product default. No rows; published once a bound, admitted game.missions backend reports active missions. Synthetic tests only: backend adapters are synthetic today, so it does not fire. Not run in game. |
| | | | `game.events.loadout_respawn_completed` (`game.loadout.respawn_completed`) | OFF | Host-side producer seam only; the native hook site and calling convention are unresolved. Not run in game. |
| mining rows ([`mining.md`](mining.md)) | row | - | `mining.cell` (legacy host row) | ON | row resolved on 4.10.196.36804 (sigcheck) |
| `game.creative` ([`sc_creative.h`](../../include/sc_creative.h)) | 1.0 | Stable | `game.creative.god_mode` (`set_god_mode`) | ON | row resolved on 4.10.196.36804 (sigcheck) |
| | | | `game.creative.fly` (`set_fly_mode`, `set_fly_speed`) | ON | row resolved on 4.10.196.36804 (sigcheck) |
| | | | `game.creative.ammo` (`set_infinite_ammo`) | ON | row resolved on 4.10.196.36804 (sigcheck) |
| | | | `game.creative.ship_ammo` (`set_infinite_ship_ammo`, the ship you're aboard) | ON | row resolved on 4.10.196.36804 (sigcheck) |
| `game.hangar` ([`sc_hangar.h`](../../include/sc_hangar.h)) | 1.0 | Stable | `game.hangar.lift` (`lift_of_landing_area`, `lift_request`) | OFF | OFF pending in-game confirmation |
| | | | `game.hangar.atc_cancel` (`atc_cancel_request`) | OFF | OFF pending in-game confirmation |
| `game.reputation` ([`sc_reputation.h`](../../include/sc_reputation.h)) | 1.0 | Stable | `game.reputation.checks` (`checks`, read-only) | OFF | OFF pending in-game confirmation |
| `game.asop` ([`sc_asop.h`](../../include/sc_asop.h)) | 1.0 | Stable | `game.asop.claim_timeout` (`claim_timeout`) | OFF | OFF pending in-game confirmation |
| | | | `game.asop.shard_persisted` (`shard_persisted`) | OFF | OFF pending in-game confirmation |
| `game.contracts` ([`sc_contracts.h`](../../include/sc_contracts.h)) | 1.0 | Stable | `game.contracts.stream_radius` (`stream_radius`, read-only) | OFF | OFF pending in-game confirmation |
| `game.prefabs` ([`sc_prefabs.h`](../../include/sc_prefabs.h)) | 1.0 | Experimental | `game.entities.prefabs` (`spawn`) | ON | On by default; synthetic admission and ABI fixtures. Observed once in game by the maintainer on build 4.10.200.58746: a prefab placement placed an Outposts item. One or two sessions, recorded as observation text only; not a verification. Windows/MSVC, native SEH and wider gameplay are not verified. |
| `game.owned` ([`sc_owned.h`](../../include/sc_owned.h)) | 1.0 | Experimental | no capability of its own: composes the `game.entities` and `game.actors` removal and seat reads | none | Synthetic provider and ownership fixtures and header layout checks only; not verified on Windows/MSVC. Not run in game. |
| `game.loadout` ([`sc_loadout.h`](../../include/sc_loadout.h)) | 1.3 | Experimental | `game.loadout.apply` (`apply`) | OFF | Synthetic and native-boundary checks only; typed engine methods unverified. Not run in game. |
| | | | `game.loadout.weapons` (weapon and multitool slots 8-21 of `apply`) | OFF | Synthetic policy only; no verified engine backend. Not run in game. |
| | | | `game.loadout.async` (`submit`, `status`, `cancel`) | OFF | Opt-in native spawn/attach/readback adapter, Windows synthetic-object fixture and current-build locator checks. Observed once in game by the maintainer on build 4.10.200.58746: a gear equip through the async route attached items (Game.log AttachmentReceived). One or two sessions, recorded as observation text only; not a verification, and the default is unchanged |
| | | | `game.loadout.outfit` (outfit slots 22-31 of `submit`: clothing, hat, head accessory, head horn, jewellery) | OFF | On only with the async route and the explicit product switch SCO_LOADOUT_OUTFIT=1 (the product host decides its own default); item-port names are consumer-side data, not verified engine rows; hair, hair colour, eyebrow, head, body, eyes and teeth have no slot because this version cannot undo them. Synthetic fixture only. Not run in game. |
| `game.quantum` ([`sc_quantum.h`](../../include/sc_quantum.h)) | 1.0 | Experimental | `game.quantum.control` | OFF | Production adapter configures at start only when its rows resolve and fresh offline admission is positive; otherwise OFF. It writes four int quantum cvars and one shared drive parameter cell only while holding, saves the prior value first and restores it on every release path. Synthetic fake-engine tests only; not verified on Windows/MSVC. Not run in game. |
| `game.build` ([`sc_game_build.h`](../../include/sc_game_build.h)) | 1.0 | Experimental | `game.build.info` (`read`) | ON | No rows: reads TimeDateStamp/SizeOfImage from the running module header and the pack's own verified-build pin; starts only when the numeric mirrors agree with the strings; host-side compile and layout pins checked. Not run in game. |
| `game.gamerules` ([`sc_gamerules.h`](../../include/sc_gamerules.h)) | 1.1 | Experimental | `game.gamerules.read` | OFF | Read-only snapshot of server GameRules parameters; rows gamerules.params_label/params_sync resolve on 4.10.196.36804 and 4.10.197.2878 in local sco-sigcheck runs; default production backend binds nothing (read returns UNAVAILABLE) and the native read seam installs nothing without a read-only accessor; not verified on Windows/MSVC. Not run in game. |
| | | | `game.gamerules.params` (`list_params`) | ON | Static definitions of the 15 GameRules parameter names, checked statically on 4.10.197.2878; types not verified; no values. Not run in game. |
| `game.inventory` ([`sc_inventory.h`](../../include/sc_inventory.h)) | 1.2 | Experimental | `game.inventory.read` (`snapshot`) | OFF | Read-only item-port snapshot of the local player (whole attached tree, depth first, 128 ports max with TRUNCATED and total_seen); adapter grounded by static analysis and bound in production behind fail-closed guards; the signature rows resolve against the 4.10.197.2878 executable on disk, and the adapter is exercised against synthetic game objects only; off by default. Not run in game. |
| | | | `game.inventory.layout` (`snapshot_layout`) | OFF | Copied stable logical-port identities and roles through an explicitly bound synthetic host producer; no production native layout producer is installed. Not run in game. |
| `game.private_session` ([`sc_private_session.h`](../../include/sc_private_session.h)) | 1.0 | Experimental | `game.private_session` | ON | On by default (loopback broker, no socket until Host/Join). Synthetic service and ABI checks only; Windows/MSVC, private LAN and EAC inspection are not verified. Not run in game. |
| `game.cvars` ([`sc_cvars.h`](../../include/sc_cvars.h)) | 1.0 | Experimental | `game.cvars.read` (`get_bool`) | OFF | Opt-in Windows reference-integer native read adapter with exact receiver/method checks, fresh admission and synthetic native-object fixtures. Not run in game. |
| `game.cvars_write` ([`sc_cvars_write.h`](../../include/sc_cvars_write.h)) | 1.0 | Experimental | `game.cvars.write` (`set_bool`) | OFF | Production native writes refused pending object and backing lifetime across callbacks/leases; setter and cleanup admission tested with synthetic native objects and actual host Stop. Not run in game. |
| `game.economy` ([`sc_economy.h`](../../include/sc_economy.h)) | 1.0 | Experimental | `game.economy.read` | OFF | Default provider unavailable until a Windows adapter is supplied. Synthetic callbacks only. Not run in game. |
| | | | `game.economy.apply` | OFF | Default provider unavailable until a Windows adapter is supplied. Synthetic callbacks only. Not run in game. |
| `game.missions` ([`sc_missions.h`](../../include/sc_missions.h)) | 1.0 | Experimental | `game.missions.read` | OFF | Default provider unavailable until a Windows adapter is supplied. Synthetic callbacks only. Not run in game. |
| | | | `game.missions.accept` | OFF | Default provider unavailable until a Windows adapter is supplied. Synthetic callbacks only. Not run in game. |
| | | | `game.missions.progress` | OFF | Default provider unavailable until a Windows adapter is supplied. Synthetic callbacks only. Not run in game. |
| | | | `game.missions.scripts` | OFF | Default provider unavailable until a Windows adapter is supplied. Synthetic callbacks only. Not run in game. |
| `game.mining` ([`sc_mining.h`](../../include/sc_mining.h)) | 1.0 | Experimental | `game.mining` (the service's mining activation) | OFF | Off by default; synthetic provider tests only; native adapter not verified on Windows. Not run in game. |
| `game.relocation` ([`sc_relocation.h`](../../include/sc_relocation.h)) | 1.1 | Experimental | `game.relocation` | ON | ON only while the host certifies offline admission, the engine rows resolve, a live player and a known star system exist and the product switch is on; otherwise OFF. Portable and Windows/MSVC synthetic provider and ABI tests. Not run in game. |
| | | | `game.relocation.position` (`request_position`) | OFF | Explicit position-only request and shared provider/socket fixtures; no native position transport, independent target-instance/epoch producer or fresh readback adapter. Existing FullPose adapter does not supply this path. Portable and Windows/MSVC synthetic provider and ABI tests. Not run in game. |
| `game.spatial` ([`sc_game_spatial.h`](../../include/sc_game_spatial.h)) | 1.0 | Experimental | no capability of its own: full-pose conversion and copied observed zones | none | Synthetic fixtures only; not verified on Windows. Not run in game. |
| CryPak adapter rows ([`api.md`](../api.md)) | row | - | `game.pak.all_files` (`sco::game::pak` `Scope::AllFiles`: persistent CryPak slots, `files/` replacement mounts) | OFF | OFF pending in-game confirmation |
<!-- status-registry:end -->

Fields a published table leaves empty, because nothing in the game's code pins them:

| Field | Where | Evidence |
|---|---|---|
| the entity and normal of a hit | `game.world.raycast`'s `sc_world_hit` (`entity_id` and `normal` stay 0, `flags` has neither bit) | BLOCKED (the game's hit record doesn't pin where they are) |
| the killer | `game.player.died`'s `killer_id` (always 0) | BLOCKED (the death record doesn't pin it) |

An actor's maximum health (`game.actors.health`'s `max`, 0) is not read yet; that is unfinished work, not a blocked item.

`teleport.spatial` and `spawn.entities` (and its mover) first shipped as sc-offline's own built-ins, on the same rows and the same code. That history is not counted above: the game pack's copy, published under the owner `game`, has only the row check.

## What an in-game run has to show

Each OFF line turns ON by a code change after one run, never by a documentation change. The [game world spikes](../design/game-world-spikes.md) record the research behind these runs.

| Capability | The run | What it decides |
|---|---|---|
| `game.entities.query_radius` | Start the game once with `SCO_ENTITIES_DIAG=1` and read the `game.entities diag` lines in `mod.log` | The thread the entity walk's callbacks run on and the walk's flag byte; then `kQueryRadiusConfirmed` |
| `game.entities.watch` | The same run | The spawn batch's vector layout and the thread of the streaming hooks; then `kWatchConfirmed` |
| `game.events.vehicle_seat` | Board a pilot seat, a turret, a station chair and a bed; read the `vehicle_seat probe` lines | Which actor a seat transition belongs to, and whether the link is a vehicle seat |
| `game.actors.state` | Get downed, then die; read the `game.actors state probe` lines | Which of the game's two "not fully alive" checks means dead and which incapacitated |

The ON lines still need an acceptance run: every usable seat fills on the Javelin and on a small ship; NPCs spawn, despawn and go when the plugin unloads; entities spawn, move and despawn.

## Rows behind the original capabilities (4.10.196.36804)

Each row below is an `OK` line of the sigcheck report named in [Sources](#sources), on 4.10.196.36804. Addresses are not published here; `sco-sigcheck` prints them for your own game build.

| Capability | Rows |
|---|---|
| `game.mining` | `mining.cell` |
| `game.world.camera` | `teleport.to_camera`, `build.camera_fields` |
| `game.actors.health` | `actor.health_site`, `actor.status_accessor`, `actor.get_stat` |
| `game.actors.state` (off) | `actor.state_site`, `actor.status_accessor`, `actor.is_dead_confirmed` |
| `game.events.player_spawned` | `event.player_spawn` |
| `game.events.player_died` | `event.player_death` |
| `game.events.actor_died` | `event.player_death` (the same hook; it publishes both events) |
| `game.events.vehicle_seat` (off) | `event.seat_enter`, `event.seat_exit` |
| `game.entities.class_of` | `entities.dump`, `entities.class_site` |
| `game.entities.query_radius` (off) | `entities.dump`, `entities.index`, `entities.for_each`, `entities.class_site` |
| `game.entities.watch` (off) | `entities.spawn_sinks`, `entities.delete_entity`, `entities.class_site` |
| `game.creative.*` (existing rows, none added) | `spawn.set_god_mode`, `spawn.god_mode_call`, `spawn.request_fly_mode`, `spawn.fly_speed_scaler`, `spawn.game_cvars`, `ammo.set_ammo`, `spawn.actor_link`, `spawn.handle_to_id`; per-capability mapping in [creative.md](creative.md) |

The rows of the older capabilities are on the pages of their groups: [actors](actors.md), [world](world.md), [entities](entities.md) and [events](events.md).

## Sources

- **Sigcheck reports:** the row counts and builds are in [verified game builds](../game-pack.md#verified-game-builds); the reports stay on the maintainer's machine.
- **Spikes:** [game world spikes](../design/game-world-spikes.md), whose verdicts decide the BLOCKED and OFF lines.
