# Status of every game.* table

SDK 2.4.0 (published 2026-10-21 as release candidate 1, `sdk-v2.4.0-rc.1`) uses game pack **0.8.0**. `main` pins the verified build **4.10.197.2878** (PE timestamp `0x6accbae3`, [game pack](../game-pack.md#verified-game-builds)): the rows recorded on 4.10.196.36804 re-scan unchanged on it, and the evidence strings below keep the build each was recorded on. A package coordinate or a pin bump includes additive contracts; it does not establish in-game verification.

The dated signature evidence below was recorded at sco-core `main` `2197dfb` (2026-10-11, with `game.actors.died`), where the game pack was still **0.2.0** (0.3.0 arrived in `95a0d34` and first shipped in `sdk-v2.0.0`), against Star Citizen build **4.10.196.36804** (`StarCitizen.exe` PE timestamp `0x6ac64e6f`). One line per service table and per capability: its version, its [stability](../stability.md) label, whether it is on by default, and how far it has been checked. Later sections retain their own synthetic-only and unverified limits.

> **Nothing has been verified in game since these services landed.** ON records default availability policy subject to readiness, admission and product conditions. Some capabilities expose static metadata without signature rows; ON does not certify gameplay. Recorded in-game results require their own dated evidence.

## How to read it

**Stability.** *Stable* or *Experimental*, per table: a compatibility promise about the table's layout and lifetime within SDK 2.x, decided by the rule in [Stability](../stability.md#how-a-game-table-gets-its-label) (a new label is Stable when at least one capability has a `sigcheck` row result, an OFF-pending-probe line or a dated in-game result, otherwise Experimental; once a released SDK has carried Stable, the table stays Stable for the rest of SDK 2.x). The `game.*` event bus row's label covers its event names and payload structs. A `-` marks a signature-row group (mining rows, CryPak adapter rows): it is not a service table, its rows can stop resolving with a game patch, and it carries no stability promise. A label says nothing about availability or evidence: a Stable table can still be OFF, answer `SCO_UNAVAILABLE`, or have no in-game run.

**Default.** *ON*: default availability policy, still subject to each row's readiness, admission and product conditions ([game services](../game-services.md#for-products)). *OFF*: the operation is held off; a published table can still answer `SCO_UNAVAILABLE`. *none*: no independent capability gates the line, or no blocked surface is published. Defaults are not execution evidence.

**Source.** The service table below is generated from `sdk/status_registry.json` by `python3 tools/status_registry.py --write`, and CI fails when the two differ (`--check`). Change the registry, never the table; its evidence levels restate what the linked pages already record and never claim an in-game run.

**Evidence level**, exactly one of:

| Level | Means |
|---|---|
| `row resolved on 4.10.196.36804 (sigcheck)` | Every row the capability reads is `OK` in a `sco-sigcheck` report of that exe. The byte checks pass and each locator matches once. Nothing was executed in the game |
| `verified in game (date)` | Someone ran it in a game session on that date and the result matched. **No entry has this level yet** |
| `OFF pending in-game confirmation` | The rows resolve, but something only a run can show is open (the thread a callback runs on, a vector layout, which of two checks means dead). The capability stays off until that run |
| `BLOCKED` | No locator we can trust, so nothing is published |

## Typed loadout extension (G6)

`game.loadout` 1.2 / `game.loadout.apply`: table published by Windows game-services
composition, capability **OFF — typed engine methods unverified**. Synthetic real
host/provider/native-module checks only; no trusted per-slot equip mapping and no
offline admission adapter installed. See [loadout](loadout.md). Not run in game.

`game.loadout.weapons` (added in 1.1) gates the weapon and multitool slots 8-21
of `apply` and is **OFF by default**, vocabulary only: production starts the service
with no engine backend, so those slots answer unavailable or unsupported until a
verified backend declares weapon support. Synthetic evidence only; no per-slot port
mapping is verified and nothing was run in game. The service version macro
`SC_LOADOUT_SERVICE_VERSION` retains the synchronous prefix version. The separate
1.2 asynchronous adapter is opt-in; the native respawn event producer remains
unresolved and default OFF. Neither route is verified in game.

## Quantum control activation (G8, unreleased R1)

`game.quantum` 1.0 / `game.quantum.control` is **owner-approved and conditionally
ON** after successful registration of a complete private host backend with the
shared owner-release dispatcher installed. All enable/hold calls still require
fresh positive offline/EAC-absent admission, owned drive, game thread and bounded
values. No additional public table/header or kernel mutation route is added.

**Default production adapter present under the maintainer's 2026-10-15 amended rule
(Windows game pack) — capability ON only when it configures.** It configures only
when every row it needs resolves and fresh positive offline admission holds at start;
otherwise the capability stays OFF. It writes four int quantum cvars and one shared
drive parameter cell only while a hold is active, with first-prior save and restore on
every release path; see [the amended rule](../quantum.md#amended-rule-2026-10-15).
The tests are synthetic (a fake engine); **Windows/MSVC, actual drive input/charge/
effects, the `StartUse` user match and gameplay are NOT VERIFIED; not run in game**.

## Private-session broker (G13, unreleased)

`game.private_session` 1.0 ([public C header](../../include/sc_private_session.h),
[host policy](private-session.md)) is **ON by default for R1 by owner approval**.
Default bind and exact peer allowlist are `127.0.0.1`; startup publishes the
broker without sockets. Host/Join needs fresh explicit UI consent, a 12–256-byte
passphrase, and positive offline/EAC/official-network/target admission. The host,
not plugins, controls explicit private LAN bind and 1–16 exact peer addresses.
Authenticated backend messages keep existing size/send/inbox quotas; global
broker requests are bounded at four/second and raw ingress at 4,096/second.
Admission loss, owner unload and host stop close the session; lifecycle records
contain no passphrase, player name or endpoint. Portable real-backend/native-C
service and ABI checks are synthetic. **Windows/MSVC, private LAN, EAC inspection,
firewall policy and gameplay are NOT VERIFIED.** Relocation retains its separately
documented conditional admission gates.

## Service tables and capabilities

**Unreleased G4 addition:** `game.prefabs` 1.0 uses the existing object-container
spawner/attribute writer and entities ownership. Capability `game.entities.prefabs`
is **ON by default for R1 by owner approval**, with the hard-disable removed.
Existing startup/cleanup readiness, provider-fault refusal, canonical 511-byte
content paths, finite normalized poses and shared 1,024/per-owner 128 entity
quotas remain enforced. This approval is not runtime acceptance: **Windows/MSVC,
native SEH behavior and gameplay are NOT VERIFIED**. Streamed materialization,
cancellation before first stream, preview/unload cleanup and native attribute/
spawn/batch-release fault handling still need owner-run evidence. Portable
admission/ABI fixtures are synthetic. G5 and G9 behavior is unchanged.


**Unreleased G9 addition:** `game.owned` 1.0 ([header](../../include/sc_owned.h),
[policy](../design/owned-removal.md)) is a game-pack composition over the existing
entities/actors removal and seat-read capabilities below, not a new engine binding.
Only synthetic provider/ownership fixtures and header layout checks are supplied;
**not run in game or on Windows/MSVC runtime**. Its receipts record initiation,
not disappearance. Retained build ownership lasts only while loaded and ordinary
unload removes placements. C/C++ direct routes; C#/Lua unsupported.

<!-- status-registry:start -->
| Table | Version | Stability | Capability | Default | Evidence |
|---|---|---|---|---|---|
| `game.audio` ([`sc_audio.h`](../../include/sc_audio.h)) | 1.1 | Experimental | `game.audio.triggers` (`resolve`, `play`, `stop`) | OFF | Owner-bound trigger/playback tokens, actual host release cleanup and bounded synthetic provider tests. No native or audible output verification; not run in game. |
| | | | `game.audio.play_once` (`resolve`, `play_once`) | OFF | Independent default-OFF capability with owner-bound tokens, extent-checked v1.1 tail, fault latch until process restart and synthetic provider tests. No native backend ships; held until current game-object registration can be confirmed at submission. Not run in game. |
| `game.inventory.contents` ([`sc_container_contents.h`](../../include/sc_container_contents.h)) | 1.0 | Experimental | `game.inventory.contents.read` (`snapshot`) | OFF | Copied bounded container-item read provider with real-host consumer and ABI fixtures; no native adapter, not run in game. |
| `game.datacore_live` ([`sc_datacore_live.h`](../../include/sc_datacore_live.h)) | 1.0 | Experimental | `game.datacore_live` (`is_ready`, `get_pool_stats`, `find_record`, `read_pool`, `write_pool`) | OFF | Default-OFF native counted read adapter with validated lifecycle locators, serialized text-blob bytes and inherited record metadata. Requires shell-enforced pre-engine quiescence, lifecycle exclusion and fresh admission. Native writes unavailable; synthetic service/hook/fault tests, not run in game. |
| `game.input` ([`sc_input.h`](../../include/sc_input.h)) | 1.0 | Experimental | `game.input.registry` (`count`, `by_id`, `by_name`) | ON | Static metadata lookups only. 1,297 actions, ids 1..1297, re-derived statically from the 4.10.197.2878 action registry (F-4.10.197.2878-050); not run in game. |
| `teleport.spatial` ([`sc_spatial.h`](../../include/sc_spatial.h)) | 1.0 | Stable | the four `teleport.*` rows (the product's capability `teleport`) | ON | row resolved on 4.10.196.36804 (sigcheck) |
| `spawn.entities` ([`sc_spawn.h`](../../include/sc_spawn.h)) | 1.2 | Stable | the `spawn.helpers` rows (the product's capability `spawn.ship`) | ON | row resolved on 4.10.196.36804 (sigcheck) |
| `game.actors` ([`sc_actors.h`](../../include/sc_actors.h)) | 1.1 | Stable | `game.actors.local_player` | ON | row resolved on 4.10.196.36804 (sigcheck) |
| | | | `game.actors.spawn_npc` | ON | row resolved on 4.10.196.36804 (sigcheck) |
| | | | `game.actors.despawn` | ON | row resolved on 4.10.196.36804 (sigcheck) |
| | | | `game.actors.health` | ON | row resolved on 4.10.196.36804 (sigcheck) |
| | | | `game.actors.state` | OFF | OFF pending in-game confirmation |
| `game.actor_stats` ([`sc_actor_stats.h`](../../include/sc_actor_stats.h)) | 1.1 | Stable | `game.actor_stats.read` (`read`) | ON | row resolved on 4.10.197.2878 (sigcheck); actor.stat_names keeps each named stat on its game index (F-4.10.197.2878-006); stat units, range and direction not run in game |
| `game.actor_stats_write` ([`sc_actor_stats_write.h`](../../include/sc_actor_stats_write.h)) | 1.0 | Stable | `game.actor_stats.write` (`adjust`, `set`, `mode`) | OFF | OFF pending in-game confirmation |
| `game.vehicles` ([`sc_vehicles.h`](../../include/sc_vehicles.h)) | 1.0 | Stable | `game.vehicles.seats` | ON | row resolved on 4.10.196.36804 (sigcheck) |
| | | | `game.vehicles.seat` | ON | row resolved on 4.10.196.36804 (sigcheck) |
| | | | `game.vehicles.flight_ready` | ON | row resolved on 4.10.196.36804 (sigcheck) |
| `game.world.control` ([`sc_world_control.h`](../../include/sc_world_control.h)) | 1.1 | Experimental | `game.world.raycast_filtered_many` (`raycast_filtered_many`) | OFF | Bounded scoped multi-hit policy and lifecycle tests; no native grid/hit adapter, not run in game |
| | | | `game.world.raycast_filtered` (`raycast_filtered`) | OFF | Synthetic policy only; no verified offline engine adapter, no Windows/game proof |
| | | | `game.world.free_camera` (`camera_acquire`) | OFF | Synthetic policy only; no verified offline engine adapter, no Windows/game proof |
| `game.world` ([`sc_world.h`](../../include/sc_world.h)) | 1.1 | Stable | `game.world.raycast` | ON | row resolved on 4.10.196.36804 (sigcheck) |
| | | | `game.world.camera` (position, rotation, zone) | ON | row resolved on 4.10.196.36804 (sigcheck) |
| | | | `game.world.free_camera_flag` (`free_camera_flag`, read-only) | OFF | OFF pending in-game confirmation |
| | | | camera field of view | none | BLOCKED |
| `game.entities` ([`sc_entities.h`](../../include/sc_entities.h)) | 1.2 | Stable | `game.entities.lookup` (`list_classes`, `find_class`, `find_by_name`, `component`) | OFF | Copied registry/name/typed-presence provider policy and ABI tests; no native adapter, not run in game |
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
| | | | `game.events.mission_changed` (`game.mission.changed`) | ON | ON by owner ruling (2026-10-16): a product default, not a verified claim. No rows; published once a bound, admitted game.missions backend reports active missions. Synthetic only: backend adapters are synthetic today, so it does not fire; not run in game |
| | | | `game.events.loadout_respawn_completed` (`game.loadout.respawn_completed`) | OFF | Verified offline host producer seam only; native hook signature/site unresolved (F-INTAKE-004, T-3); not run in game. |
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
| `game.prefabs` ([`sc_prefabs.h`](../../include/sc_prefabs.h)) | 1.0 | Experimental | `game.entities.prefabs` (`spawn`) | ON | ON by owner approval for R1; synthetic admission/ABI fixtures only; Windows/MSVC, native SEH and gameplay NOT VERIFIED |
| `game.owned` ([`sc_owned.h`](../../include/sc_owned.h)) | 1.0 | Experimental | no capability of its own: composes the `game.entities` and `game.actors` removal and seat reads | none | Synthetic provider/ownership fixtures and header layout checks only; not run in game or on Windows/MSVC runtime |
| `game.loadout` ([`sc_loadout.h`](../../include/sc_loadout.h)) | 1.2 | Experimental | `game.loadout.apply` (`apply`) | OFF | Synthetic/native-boundary checks only; typed engine methods unverified; not run in game |
| | | | `game.loadout.weapons` (weapon and multitool slots 8-21 of `apply`) | OFF | Synthetic policy only; no verified engine backend, no in-game proof |
| | | | `game.loadout.async` (`submit`, `status`, `cancel`) | OFF | Opt-in native spawn/attach/readback adapter, Windows synthetic-object fixture and current-build locator checks; not run in game |
| `game.quantum` ([`sc_quantum.h`](../../include/sc_quantum.h)) | 1.0 | Experimental | `game.quantum.control` | OFF | Default production adapter ported from v0.7.7 under the maintainer's 2026-10-15 amended rule: writes four int quantum cvars and one shared drive parameter cell only while holding, after fresh offline admission, first prior saved, restored on every release path; ON only when its rows resolve and admission is positive at start; synthetic fake-engine tests only; Windows/MSVC and gameplay NOT VERIFIED |
| `game.build` ([`sc_game_build.h`](../../include/sc_game_build.h)) | 1.0 | Experimental | `game.build.info` (`read`) | ON | No rows: reads TimeDateStamp/SizeOfImage from the running module header and the pack's own verified-build pin; starts only when the numeric mirrors agree with the strings; host-side compile and layout pins verified, not run in game |
| `game.gamerules` ([`sc_gamerules.h`](../../include/sc_gamerules.h)) | 1.1 | Experimental | `game.gamerules.read` | OFF | Read-only snapshot of server GameRules parameters; rows gamerules.params_label/params_sync resolve on 4.10.196.36804 and 4.10.197.2878 in local sco-sigcheck runs (F-4.10.196.36804-007/-008); default production backend binds nothing (read returns UNAVAILABLE); Windows/MSVC and gameplay NOT VERIFIED |
| | | | `game.gamerules.params` (`list_params`) | ON | Static definitions of the 15 GameRules parameter names verified on 4.10.197.2878 (F-4.10.197.2878-054); types not verified; no values; not run in game. |
| `game.inventory` ([`sc_inventory.h`](../../include/sc_inventory.h)) | 1.1 | Experimental | `game.inventory.read` (`snapshot`) | OFF | Read-only item-port snapshot of the local player (whole attached tree, depth first, 128 ports max with TRUNCATED and total_seen); adapter grounded on the private corpus (F-4.10.197.2878-011..-016, static analysis only) and bound in production behind fail-closed guards; the signature rows resolve against the 4.10.197.2878 executable on disk, and the adapter is exercised against synthetic game objects only; switched off by default; not run in game |
| `game.private_session` ([`sc_private_session.h`](../../include/sc_private_session.h)) | 1.0 | Experimental | `game.private_session` | ON | ON by owner approval for R1 (loopback broker, no socket until Host/Join); synthetic service and ABI checks only; Windows/MSVC, private LAN, EAC inspection and gameplay NOT VERIFIED |
| `game.cvars` ([`sc_cvars.h`](../../include/sc_cvars.h)) | 1.0 | Experimental | `game.cvars.read` (`get_bool`) | OFF | Opt-in Windows reference-integer native read adapter with exact receiver/method checks, fresh admission and synthetic native-object fixtures; not run in game |
| `game.cvars_write` ([`sc_cvars_write.h`](../../include/sc_cvars_write.h)) | 1.0 | Experimental | `game.cvars.write` (`set_bool`) | OFF | Production native writes refused pending object and backing lifetime across callbacks/leases; setter and cleanup admission tested with synthetic native objects and actual host Stop; not run in game |
| `game.economy` ([`sc_economy.h`](../../include/sc_economy.h)) | 1.0 | Experimental | `game.economy.read` | OFF | Default provider unavailable pending an owner-approved Windows adapter; synthetic callbacks only |
| | | | `game.economy.apply` | OFF | Default provider unavailable pending an owner-approved Windows adapter; synthetic callbacks only |
| `game.missions` ([`sc_missions.h`](../../include/sc_missions.h)) | 1.0 | Experimental | `game.missions.read` | OFF | Default provider unavailable pending an owner-approved Windows adapter; synthetic callbacks only |
| | | | `game.missions.accept` | OFF | Default provider unavailable pending an owner-approved Windows adapter; synthetic callbacks only |
| | | | `game.missions.progress` | OFF | Default provider unavailable pending an owner-approved Windows adapter; synthetic callbacks only |
| | | | `game.missions.scripts` | OFF | Default provider unavailable pending an owner-approved Windows adapter; synthetic callbacks only |
| `game.mining` ([`sc_mining.h`](../../include/sc_mining.h)) | 1.0 | Experimental | `game.mining` (the service's mining activation) | OFF | Off by default; native adapter not verified on Windows or in game; synthetic provider tests only |
| `game.relocation` ([`sc_relocation.h`](../../include/sc_relocation.h)) | 1.1 | Experimental | `game.relocation` | ON | ON only while the host certifies offline admission, the engine rows resolve, a live player and a known star system exist and the product switch is on; otherwise OFF. Portable and Windows/MSVC synthetic provider/ABI tests; native engine behavior and gameplay not verified |
| | | | `game.relocation.position` (`request_position`) | OFF | Explicit position-only request and shared provider/socket fixtures; no native position transport, independent target-instance/epoch producer or fresh readback adapter. Existing FullPose adapter does not supply this path. Portable and Windows/MSVC synthetic provider/ABI tests; not run in game. |
| `game.spatial` ([`sc_game_spatial.h`](../../include/sc_game_spatial.h)) | 1.0 | Experimental | no capability of its own: full-pose conversion and copied observed zones | none | Synthetic fixtures only; not verified on Windows or in game |
| CryPak adapter rows ([`api.md`](../api.md)) | row | - | `game.pak.all_files` (`sco::game::pak` `Scope::AllFiles`: persistent CryPak slots, `files/` replacement mounts) | OFF | OFF pending in-game confirmation |
<!-- status-registry:end -->

Fields a published table leaves empty, because nothing in the game's code pins them:

| Field | Where | Evidence |
|---|---|---|
| camera field of view | `game.world.camera` | BLOCKED (spike B2: one candidate, `CalculateFOV`, whose returned value is not pinned) |
| the entity and normal of a hit | `game.world.raycast`'s `sc_world_hit` (`entity_id` and `normal` stay 0, `flags` has neither bit) | BLOCKED (the game's hit record doesn't pin where they are) |
| the killer | `game.player.died`'s `killer_id` (always 0) | BLOCKED (the death record doesn't pin it) |

An actor's maximum health (`game.actors.health`'s `max`, 0) is not read yet; that is unfinished work, not a spike verdict.

`teleport.spatial` and `spawn.entities` (and its mover) ran in game on 2026-10-09 as sc-offline's own built-ins, on the same rows and the same code. That history is not counted above: the game pack's copy, published under the owner `game`, has not run in a game session.

## What an in-game run has to show

Each OFF line turns ON by a code change after one run, never by a documentation change:

| Capability | The run | What it decides |
|---|---|---|
| `game.entities.query_radius` | Start the game once with `SCO_ENTITIES_DIAG=1` and read the `game.entities diag` lines in `mod.log` | The thread the entity walk's callbacks run on and the walk's flag byte (spike B3); then `kQueryRadiusConfirmed` |
| `game.entities.watch` | The same run | The spawn batch's vector layout and the thread of the streaming hooks (spike B9); then `kWatchConfirmed` |
| `game.events.vehicle_seat` | Board a pilot seat, a turret, a station chair and a bed; read the `vehicle_seat probe` lines | Which actor a seat transition belongs to, and whether the link is a vehicle seat (spike B8) |
| `game.actors.state` | Get downed, then die; read the `game.actors state probe` lines | Which of the game's two "not fully alive" checks means dead and which incapacitated (spike B4) |

The ON lines still need the acceptance run the maintainer listed: every usable seat fills on the Javelin and on a small ship; NPCs spawn, despawn and go when the plugin unloads; entities spawn, move and despawn.

## Rows behind the newest capabilities

Every address below is the `OK` line of the sigcheck report named in [Sources](#sources), on the build above.

| Capability | Rows (RVA) |
|---|---|
| `game.mining` | `mining.cell` 0x265fb10 |
| `game.world.camera` | `teleport.to_camera` 0x2e27140, `build.camera_fields` 0x2e272ba |
| `game.actors.health` | `actor.health_site` 0x5bd3fe0, `actor.status_accessor` 0x2e77ef0, `actor.get_stat` 0x3fe3b10 |
| `game.actors.state` (off) | `actor.state_site` 0x400a702, `actor.status_accessor`, `actor.is_dead_confirmed` 0x3fec460 |
| `game.events.player_spawned` | `event.player_spawn` 0x602a790 |
| `game.events.player_died` | `event.player_death` 0x2edb6d0 |
| `game.events.actor_died` | `event.player_death` 0x2edb6d0 (the same hook; it publishes both events) |
| `game.events.vehicle_seat` (off) | `event.seat_enter` 0x6133960, `event.seat_exit` 0x6133e40 |
| `game.entities.class_of` | `entities.dump` 0x688c918, `entities.class_site` 0x6858880 |
| `game.entities.query_radius` (off) | `entities.dump`, `entities.index` 0x11cad080, `entities.for_each` 0x68926e0, `entities.class_site` |
| `game.entities.watch` (off) | `entities.spawn_sinks` 0x687d950, `entities.delete_entity` 0x6886c20, `entities.class_site` |
| `game.creative.*` (existing rows, none added) | `spawn.set_god_mode` 0x59fcdd0, `spawn.god_mode_call` 0x2e23e4c, `spawn.request_fly_mode` 0x5be07f0, `spawn.fly_speed_scaler` 0x2ec9eb2, `spawn.game_cvars` 0xa1ae200, `ammo.set_ammo` 0x5b48f50, `spawn.actor_link` 0x2475010, `spawn.handle_to_id` 0x3d5410; per-capability mapping in [creative.md](creative.md) |

The rows of the older capabilities are on the pages of their groups: [actors](actors.md), [world](world.md), [entities](entities.md) and [events](events.md).

## Sources

- **Sigcheck report** (kept outside the repository, `sc-baseline/4.10.196.36804/` on the maintainer's machine; [game pack](../game-pack.md#verified-game-builds)): `sigcheck-main-17e2620.txt`, one run of sco-core `main` at `17e2620` (every game service and the mining row) on PE timestamp `0x6ac64e6f`: **317 of 317 rows OK**, none failed. `game.creative` (#74, in `31cf53a`) added no rows, and the actor-death change removed the redundant `contracts.actor_kill` row without adding one, so **316 rows** back every capability on this page (the post-removal run resolves 316/316).
- **Integration record:** The merged game-service pull requests and their CI runs, plus the signature report above, document service integration and signature-check counts. The 2026-10-10 maintainer decision deferred in-game acceptance to a subsequent run after the conversion.
- **Spikes:** [game world spikes](../design/game-world-spikes.md) (Appendix B), whose verdicts decide the BLOCKED and OFF lines.
