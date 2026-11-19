# Game services

The Star Citizen game pack publishes its services under the reserved owner `game` ([design](design/game-services.md)). A plugin finds a published service with the ordinary `query_service`; `sco_api.h` does not change. [Architecture](architecture.md#layers) shows where the game pack sits among the kernel, host services, plugin SDK and consumers.

**Startup is on hold.** `app::Start` with `Platform::gameServices` and the public `game::services::Start` both refuse before they change anything, so production does not publish these tables today. The one exception is the game host's opt-in `Composition::ProcessRetained`, which reaches the real start through a private entry ([game host](game/host.md)). This page describes the contracts and the composition they were built for.

**Status.** [Status of every game.* table](game/status.md) is generated from [`sdk/status_registry.json`](../sdk/status_registry.json). It gives each capability's version, [stability](stability.md) label, default and evidence. Four things are separate: the contract version, whether a provider is ready, whether the capability is on by default, and the evidence. A published table can still return `SCO_UNAVAILABLE`. Where a capability has only synthetic or static evidence, the status page marks it "Not run in game."

## Services

"First SDK" is the first SDK release that carried the table. SDK 2.0.0 carried game pack 0.3.0, 2.1.0 carried 0.4.0, 2.2.0 carried 0.5.0, 2.3.0 carried 0.6.0, 2.4.0 carried 0.8.0, and 2.4.1 carries 0.8.1.

| Service | Version | Header | First SDK | What |
|---|---|---|---|---|
| `teleport.spatial` | 1.0 | [`sc_spatial.h`](../include/sc_spatial.h) | 2.0.0 | Where you are, and positions between the game's zones. The name and table stay for all of 1.x: sc-offline's `teleport` built-in published them before the game pack did, and a plugin cannot tell the difference. [API](api-v1.md#teleportspatial-10-game-pack) |
| `game.spatial` | 1.0 | [`sc_game_spatial.h`](../include/sc_game_spatial.h) | 2.1.0 | Full-pose conversion, and bounded copied pages of observed zone ancestry and names. The star system is always reported as unknown. Native C and raw C++ only. [Contract](contracts.md#gamespatial-sc_game_spatial_v1) |
| `game.relocation` | 1.1 | [`sc_relocation.h`](../include/sc_relocation.h) | 2.1.0 | Request, cancel and status for moving the local player. 1.0 takes a position and rotation in a streamed zone of the player's star system; 1.1 appends a position-only request. Native C and raw C++ only. [Relocation](#relocation-gamerelocation-10), [1.1 position request](game/relocation-position.md) |
| `game.world` | 1.1 | [`sc_world.h`](../include/sc_world.h) | 2.0.0 | `raycast` into the game's physics and the `camera` pose; 1.1 appends `free_camera_flag`, a read of the free-camera flag (capability `game.world.free_camera_flag`, off). The camera has no field of view. [Guide](game/world.md) |
| `game.world.control` | 1.1 | [`sc_world_control.h`](../include/sc_world_control.h) | 2.1.0 | Filtered aim rays and reversible, owner-exclusive camera leases, in a separate table. All capabilities are off. [World control](#world-control-gameworldcontrol-off) |
| `game.entities` | 1.2 | [`sc_entities.h`](../include/sc_entities.h) | 2.0.0 | Any entity: `alive`, `class_of`, `get_transform`; `set_transform` for what you spawned and the player's own vehicles; `spawn`, `despawn` and `keep`; type-filtered `watch` and `unwatch`; `query_radius`. 1.2 appends copied class-registry, exact-class, instance-name and component-presence lookups (capability `game.entities.lookup`). `watch`, `query_radius` and `lookup` are off. [Guide](game/entities.md) |
| `game.prefabs` | 1.0 | [`sc_prefabs.h`](../include/sc_prefabs.h) | 2.1.0 | Place an owned prefab by content path. Native C and C++ only. [Owned prefabs](#owned-prefabs-gameprefabs) |
| `game.owned` | 1.0 | [`sc_owned.h`](../include/sc_owned.h) | 2.1.0 | Retained build placements, and removal of your own NPCs and the NPC in a seat. Native C and C++ only. [Retained build](#retained-build-and-owned-seat-removal-gameowned) |
| `spawn.entities` | 1.2 | [`sc_spawn.h`](../include/sc_spawn.h) | 2.0.0 | Spawn entities near you, your entity and ship ids, and move what you spawned. Like `teleport.spatial`, its name stays for all of 1.x. [API](api-v1.md#spawnentities-12-game-pack) |
| `game.actors` | 1.1 | [`sc_actors.h`](../include/sc_actors.h) | 2.0.0 | Your player (`local_player`), NPCs you spawn (`spawn_npc`) and despawn (`despawn`). 1.1 appends `health` (the HealthPool stat; `max` is 0, not read yet) and `state` (always `SCO_UNAVAILABLE`; capability `game.actors.state` is off). [Guide](game/actors.md) |
| `game.actor_stats` | 1.1 | [`sc_actor_stats.h`](../include/sc_actor_stats.h) | 2.4.0 | Read your player's named status stats. [Guide](game/actor-stats.md) |
| `game.actor_stats_write` | 1.0 | [`sc_actor_stats_write.h`](../include/sc_actor_stats_write.h) | 2.4.0 | Change your own player's status stats. A separate table, so a plugin that only reads never sees a write route. Capability `game.actor_stats.write` is off. [Writes](game/actor-stats.md#writes-gameactor_stats_write) |
| `game.vehicles` | 1.0 | [`sc_vehicles.h`](../include/sc_vehicles.h) | 2.0.0 | The ship you are aboard, its seats and occupants, seating and unseating your own actors, and Flight Ready. An accepted `seat` logs `seat request sent (actor ..., seat ...); the game links it on its next update`; the log line is not the result, and `isLinked` is true only when the link already exists. [API](api-v1.md#gamevehicles-10-game-pack) |
| `game.creative` | 1.0 | [`sc_creative.h`](../include/sc_creative.h) | 2.0.0 | The local player's god mode, fly (noclip) and fly speed, and infinite ammo for carried weapons and for the ship you are aboard. [Guide](game/creative.md) |
| `game.loadout` | 1.3 | [`sc_loadout.h`](../include/sc_loadout.h) | 2.1.0 | Typed local-player loadout: synchronous `apply`, and bounded async `submit`, `status` and `cancel` (1.2). 1.3 appends outfit slots 22-31 to `submit` (capability `game.loadout.outfit`, off). Native C and C++ only; C# and Lua cannot change a loadout. [Guide](game/loadout.md), [ABI pin](../tests/abi_game_loadout.c) |
| `game.inventory` | 1.2 | [`sc_inventory.h`](../include/sc_inventory.h) | 2.4.0 | Read-only snapshot of the local player's attached item-port tree, depth first, with empty ports, up to 128 copied ports. 1.2 appends a copied logical-layout route. Capabilities `game.inventory.read` and `game.inventory.layout` are off. [Guide](game/inventory.md) |
| `game.inventory.contents` | 1.0 | [`sc_container_contents.h`](../include/sc_container_contents.h) | 2.4.0 | Copied, bounded read of the items stored in a container. Off; no native adapter. [Guide](game/container-contents.md) |
| `game.*` events | bus | [`sc_game_events.h`](../include/sc_game_events.h) | 2.0.0 | `game.player.spawned`, `game.player.died`, `game.actors.died`, `game.zone.changed` and others on the kernel event bus, with size-prefixed payloads. [Events](#events) |
| `game.mining` | 1.0 | [`sc_mining.h`](../include/sc_mining.h) | 2.1.0 | Owner-scoped `request` and copied `state` for natural mining. A successful request records intent; `state` says whether mining is active and why it is blocked. Capability `game.mining` is off. [Service](game/mining-service.md), [rows](game/mining.md) |
| `game.quantum` | 1.0 | [`sc_quantum.h`](../include/sc_quantum.h) | 2.1.0 | Bounded offline quantum controls. [Quantum](quantum.md) |
| `game.hangar` | 1.0 | [`sc_hangar.h`](../include/sc_hangar.h) | 2.2.0 | A hangar's ship lift, and withdrawing the local player's pending ATC request. Both capabilities are off. [Guide](game/hangar.md) |
| `game.asop` | 1.0 | [`sc_asop.h`](../include/sc_asop.h) | 2.3.0 | Two read-only values behind the ship terminal: the insurance claim timeout and the `IsShardPersisted` flag. Both capabilities are off. [Guide](game/asop-service.md) |
| `game.asop.selection` | 1.0 | [`sc_asop_selection.h`](../include/sc_asop_selection.h) | 2.4.0 | Candidate host-owned fixed-document selection editing. Not published by a native provider. [Guide](game/asop-selection.md) |
| `game.contracts` | 1.0 | [`sc_contracts.h`](../include/sc_contracts.h) | 2.3.0 | Read the mission stream radius. Off. [Guide](game/contracts-service.md) |
| `game.reputation` | 1.0 | [`sc_reputation.h`](../include/sc_reputation.h) | 2.2.0 | Read-only look at the game's reputation-service checks. Off. [Guide](game/reputation.md) |
| `game.economy` | 1.0 | [`sc_economy.h`](../include/sc_economy.h) | 2.1.0 | Offline local-economy balance and apply boundary. No production backend. [Design](design/offline-missions.md) |
| `game.missions` | 1.0 | [`sc_missions.h`](../include/sc_missions.h) | 2.1.0 | Offline mission offers, acceptance, progress and script declarations. No production backend. [Design](design/offline-missions.md) |
| `game.cvars` | 1.0 | [`sc_cvars.h`](../include/sc_cvars.h) | 2.1.0 | Read a fixed allowlist of boolean console variables. Off. [Guide](game/cvars.md) |
| `game.cvars_write` | 1.0 | [`sc_cvars_write.h`](../include/sc_cvars_write.h) | 2.1.0 | Reversible boolean writes for the same allowlist, in a separate table. Off. [Guide](game/cvars.md) |
| `game.gamerules` | 1.1 | [`sc_gamerules.h`](../include/sc_gamerules.h) | 2.4.0 | The frozen 1.0 `read` copies the server gates and thresholds the client received, with `generation` and `valid` (check `valid` first). `game.gamerules.read` is off and the default backend returns `SCO_UNAVAILABLE`. The 1.1 `list_params` tail copies 15 static parameter definitions without values (capability `game.gamerules.params`, on; the declared types are unverified). `list_params` is available from C, C++, C# and Lua. [Guide](game/gamerules.md) |
| `game.audio` | 1.1 | [`sc_audio.h`](../include/sc_audio.h) | 2.4.0 | Owner-bound audio trigger and one-shot playback tokens. Both capabilities are off and no native backend ships. [Audio ownership](#audio-ownership-and-cleanup) |
| `game.input` | 1.0 | [`sc_input.h`](../include/sc_input.h) | 2.4.0 | Static vocabulary of 1,297 action names and ids on build 4.10.197.2878. No input dispatch. [Guide](game/input.md) |
| `game.build` | 1.0 | [`sc_game_build.h`](../include/sc_game_build.h) | 2.4.0 | The running `StarCitizen.exe` PE `TimeDateStamp` and `SizeOfImage`, the game pack's version triple, the build it verified and `pin_matches` (the running timestamp equals the verified one). `read` copies a struct and has no drift error: gate on `pin_matches`. No signature rows; capability `game.build.info` is on whenever the pack publishes it. C# and Lua are unsupported. [Guide](game/build.md) |
| `game.private_session` | 1.0 | [`sc_private_session.h`](../include/sc_private_session.h) | 2.1.0 | Host-controlled private-session broker. [Host policy](game/private-session.md) |
| `game.datacore_live` | 1.0 | [`sc_datacore_live.h`](../include/sc_datacore_live.h) | 2.4.0 | Host-backed in-memory DataCore pool access. Native reads are off by default and native writes are unavailable. [Guide](game/datacore-live.md) |

## Relocation (`game.relocation` 1.0)

`game.relocation` 1.0 ([`sc_relocation.h`](../include/sc_relocation.h), frozen) moves the local player to a position and rotation in a streamed zone of the star system the player is already in. Native C and raw C++ only. The header owns the request, tick and status behavior; this section is what the game pack supplies behind it. Every part of it fails closed. The 1.1 position-only request is in [relocation position](game/relocation-position.md); its production capability is unavailable without an explicit position backend.

**When the capability is on.** `game.relocation` is on only while all of these hold. The provider re-checks them every game tick and on every request. If any one fails, the capability turns off and requests are refused with `SCO_UNAVAILABLE`.

- The host's fresh offline admission is positive: `sco::game::host::FreshAdmissionAllowed()` (offline session, no anti-cheat, official network blocked, correct target; Unknown never passes). The engine is not read at all before that answer, and a switch can only turn the feature off further, never bypass it.
- An engine backend is installed and verified. The game pack installs one at start (`EngineRelocationBackend()`, `src/game/services/relocation_engine.cpp`, Windows only). It is verified only when the `teleport.*` rows and `teleport.relocation` resolved.
- A live local player exists and its star system is known.
- The product switch is on.

So it is on by default in a product that runs the host composition with positive admission, and off everywhere else.

**The switch.** `sco::game::services::SetRelocationEnabled(bool)` (`sco/game/services.h`, any thread, default on) is for the product to wire to a player or host setting. Plugins read the capability `game.relocation`. Off: the capability is off, requests are refused, a pending request fails with reason `SC_RELOCATION_REASON_SAFETY`, and neither the offline check nor any engine read runs.

**Where each input comes from** (`RelocationBackend` in `relocation.h`, a game-pack seam copied at start):

| Input | Source |
|---|---|
| offline | `host::FreshAdmissionAllowed()` |
| player, player id | `reads::LocalPlayer` (the v0.7.7 `GetLocalPlayer`): the live actor and the id the game's player record names it by |
| session | One uninterrupted observation of one player entity (a provider counter). Losing the player, offline admission or the switch, or a different player id or entity, starts a new session; a pending request then fails with reason `SESSION` |
| current and target system | The id of the `SolarSystem*` zone found by walking the zone parents (case-insensitive prefix, as v0.7.7's `SystemZoneOf`), counted only when that zone's id leads back to it. No such zone, or a parent cycle, is unknown and refuses. v0.7.7 accepted an unknown current system; this does not |
| streamed | The target zone id resolves to a zone whose id leads back to the id |
| transport | The game's own teleport call, as `CmdTeleportToCamera` makes it: the actor's teleport component (actor slot `0x9D8`) is called through its slot `0x158` with a 0x80-byte params block. The row `teleport.relocation` pins that call and the stores that fill the block. The rotation goes in twice, as the zone-frame quaternion and as the world-frame quaternion composed from the zone's axes; a zone frame that is mirrored or not orthonormal refuses |
| arrived | After the move was applied, a fresh read of the player's pose in the target zone (`EntityLocalPos`) within 0.5 m of the requested position on any later tick. Rotation (`EntityRotation`) is measured and logged (`q` and `-q` are the same orientation) but not required: the game settles a character on the ground and normalises its orientation, so centimetre and sub-degree equality is not observable. A player in any other zone has not arrived |

**Completion.** If the player is already at the target when the request is made (same arrival test), the request completes at once with `applied` 0 and nothing is moved. Otherwise one request moves once. Admission is not completion: the movement starts on a later tick, and Completed is reported only when a fresh pose after the move is within 0.5 m of the target. Arrival seen on any tick completes the request for good, including on the tick the deadline passes. The move is not retried, because re-applying could pull back a player who has since moved.

**Failures.**

- A stream loss after admission waits until the request times out.
- A change of session, player or system, a lost offline admission, the switch going off, or a fault fails the request.
- A transport that returns false fails it with `TRANSPORT`.
- A request that was applied and streamed but never seen within 0.5 m ends `FAILED` with `TIMEOUT` and `applied` 1. A move that never ran has `applied` 0. The status has no text field, so a plugin tells the two cases apart by `applied` 1 with `TIMEOUT`.
- A callback that throws or faults (SEH on Windows) turns the provider off until the process restarts.

The failure log line also says `applied but arrival not confirmed`, whether the pose was read, whether position and rotation were near, and the pose trail: `first=0.412 m 3.100 deg closest=0.212 m 2.900 deg last=12.351 m 95.263 deg`. These are the first observation after the move, the closest approach on any later tick and the last observation at failure (`none` or `not measured` when nothing was read; metres only for a position-only request). The last one can be long after the player walked away, so the first and closest values show what the game did.

**Open question.** Whether the engine honours the rotation fields, and how the player ends up oriented, is not established. `test_relocation` and `test_relocation_provider` exercise every input and reason through the real provider over a fake backend.

## Events

The game pack puts low-volume events on the kernel event bus; entity streaming is the type-filtered watch on `game.entities` instead. Subscribe by name with `sco_api`'s `subscribe` or the supported language route, and check per-event support before assuming a typed wrapper. The data argument is a pointer to a size-prefixed struct from [`sc_game_events.h`](../include/sc_game_events.h); copy what you keep. Handlers run on the game thread, from the host's tick. Native hooks record occurrences without running plugin code; producers use bounded capture, including a fixed ring or a coalesced occurrence marker. Resolved rows alone do not establish a working producer, and an event whose capability is not ready never fires.

| Event | Payload | Capability | How | Default |
|---|---|---|---|---|
| `game.player.spawned` | `sc_game_player_spawned { entity_id, zone_id }` | `game.events.player_spawned` | Hook on the game's `QueueEvent<SPI_Player_OnSpawn>` (`event.player_spawn`); published only for your player | On when its rows are OK |
| `game.player.died` | `sc_game_player_died { entity_id, killer_id }` (`killer_id` is always 0: the death record doesn't pin it) | `game.events.player_died` | Hook on `QueueEvent<SPI_Player_OnDeath>` (`event.player_death`); only your player | On when its rows are OK |
| `game.actors.died` | `sc_game_actor_died { entity_id, killer_id }` (unknown ID and killer are 0) | `game.events.actor_died` | All-actor view of the same `event.player_death` hook; the ID is captured before the original code runs | On when its rows and hook are OK |
| `game.zone.changed` | `sc_game_zone_changed { old_zone_id, new_zone_id }` | `game.events.zone_changed` | The host's tick reads your zone id every 100 ms on the `teleport.*` rows (no hook); not published while you aren't spawned | On when the `teleport.*` rows are OK |
| `game.vehicle.boarded` / `game.vehicle.exited` | `sc_game_vehicle_seat { seat_index, vehicle_entity_id }` (reserved) | `game.events.vehicle_seat` | Hooks on `CSCActorResultStateLinked::Enter` / `::Exit` (`event.seat_enter`, `event.seat_exit`) | Off: log-only until an in-game run confirms the payload |
| `game.mission.changed` | `sc_game_mission_changed { mission_id, old_state, new_state, new_phase, new_objective, session }` | `game.events.mission_changed` | No rows. Polls the `game.missions` status reads on the tick and publishes when a mission's state, phase or completed-objective count changed. It is a notification, not a stream: intermediate states can be missed. It fires only where a bound, admitted `game.missions` backend reports active missions. [Events](game/events.md#gamemissionchanged-no-rows) | On, as a product default; the default has no native mission backend, so it does not fire |
| `game.loadout.respawn_completed` | `sc_game_loadout_respawn_completed`: size-only occurrence, with padding (8 bytes); no entity, zone, slot or item fields | `game.events.loadout_respawn_completed` | A verified, offline-admitted host producer records occurrences. Several records within one tick coalesce into one notification: not a counter, and not a per-slot equip result. [Loadout](game/loadout.md) | Off in production: the native hook site and calling convention are unresolved |

`game.player.spawned` waits up to 10 s for your player to be readable before it gives up, because a spawn record arrives before the actor is fully set up. A spawn is not a zone change: the spawn's zone is the baseline for `game.zone.changed`. Zone and entity ids are session handles.

**Vehicle seat probe.** The seat hooks install when their rows are OK and only log, because nothing in the executable says which actor a seat transition belongs to or that the link is a vehicle seat (it may be a chair or a bed). Each Enter and Exit logs `[game] vehicle_seat probe: Enter|Exit: state=0x… host=0x… localActor=0x… parents: <id> '<name>' > …` (your player's chain of parent entities, up to 4; at most 100 lines a session). Board a pilot seat, a turret, a station chair and a bed, and read which lines say which. That decides the actor filter and the vehicle test; then the capability goes on.

### Mission event polling and session boundaries

`game.mission.changed` compares admitted, accepted-mission observations by mission ID and session. On first sight in a session, `old_state` equals `new_state`. An empty poll is not evidence that the mission list is empty and does not clear the cache. A later nonempty poll prunes absent IDs, and service stop resets the cache. Terminal status can still be observed; intermediate changes can be missed.

## Audio ownership and cleanup

Table publication does not mean a provider is available. Trigger operations need a resolve/play/stop backend. One-shot needs resolve/play_once and its own capability. Both need the cleanup hook and live-owner admission. `game.audio.triggers` and `game.audio.play_once` are independent capabilities.

Owner release drains the owner's effects, including releases made during callbacks. A failed stop stays tracked for retry and latches new operations unavailable; a later successful retry does not clear that latch. A `play_once` call may have been accepted even if a concurrent release makes it return unavailable: do not assume it was rolled back, and do not retry it automatically. No native audio backend ships in this source; the [audio contract](game/audio.md) has the version and lifetime details.

## For products

- `sco::host::ProvideGameService(name, version, table)` publishes under the owner `game` (`sco/host.h`): `game.<name>`, or one of the product names a game pack took over (`kGameCompatNames`). The owner is never released. `sco::app::Stop` withdraws game services after every plugin has unloaded. Under `Composition::ProcessRetained`, `Stop` closes admission only and withdraws nothing.
- `sco::app::Platform::gameServices = true` currently refuses app startup before publication. A future composition must resolve the rows and avoid duplicate service names.
- `spawn.entities`' mover lets any plugin move the player's own vehicle once the product registers it (`sco::game::services::RegisterPlayerVehicle` / `UnregisterPlayerVehicle`, `sco/game/services.h`, game thread). Entities a plugin spawned through `spawn_as` stay movable by that plugin only. Ordinary owner release (including unload or non-retained `crashed` cleanup) forgets their ownership; restart-required retained-provider faults skip the hook and keep that state.
- The host removes a plugin's NPCs and entities on ordinary owner release (unload or non-retained `crashed` cleanup). A restart-required retained-provider fault skips that cleanup and keeps them while host callbacks are suppressed. `game.entities` 1.1's `keep(self, id)` releases one entity from that: it stays in the world.
- `game.creative` toggles belong to the plugin that switched them on and go off on ordinary owner release; a retained-provider fault skips this hook and does not switch them off. Each switch-off logs one line naming the plugin and `owner unloaded` or `stop`. A refusal reason is logged once per distinct reason (`last_error` keeps the latest). At service start the game pack logs one line per capability saying ready or not ready and which signature rows are not OK.
- `game.actors` reads your player through the `teleport.*` rows and `spawn.handle_to_id`, spawns through `spawn.entities`' spawner (`spawn.helpers` rows), and removes NPCs as sc-offline's Clear NPCs did (`npc.clear` rows: RemoveEntity, checked 1.5 s later, then the `npc.direct_remove` fallback, then a move far out of range). It logs `[game] game.actors: <plugin> spawned <class> as NPC <id>` and `[game] game.actors: <plugin> unloaded: removing the <n> NPCs it spawned`. `spawn_npc` refuses when it could not remove the NPC again (no `npc.clear` rows, or no release hook).
- `game.actors` 1.1's `health` finds an entity id's actor (yours, or through its `ISCItemUser` component as `game.vehicles` does) and reads the HealthPool stat through the `actor.*` rows. `state` stays off. To decide which of the game's two "not fully alive" checks means dead and which incapacitated, the game pack logs, with the `actor.state_*` rows OK, `[game] game.actors state probe (your player): P1 (status vtable slot 0x60) = <0|1>, P2 (CSCActorStatus::IsDeadConfirmed) = <0|1>, HealthPool = <n>` whenever either answer changes (at most every 250 ms), and `... state probe (died): ...` from the death hook. Get downed (incapacitated), then die: which of P1 and P2 is 1 in each state decides the mapping. The probe goes away when `state` ships.
- `game.world` casts through the physics world the `build.*` rows find (`build.ground_ray` capability rows; the ray block, slot `0x1C0` and grid release are sc-offline's build mode moved over) and reads the camera at the offsets `build.camera_fields` pins. The cast tries the zone you pass and up to two of its parents, never the world's root zone. It logs nothing on success. The first cast checks a live entity's vtable slots against the rows and answers `SCO_UNAVAILABLE` if the game changed them.
- `game.entities` spawns through the same spawner (`spawn.helpers` rows) and removes with `game.actors`' removal (`npc.clear` rows), so `spawn` refuses (`SCO_UNAVAILABLE`) when it could not remove the entity again. `set_transform` moves what the plugin spawned (through `game.entities`, `spawn_as` or `game.actors`) and the player's own vehicles (`RegisterPlayerVehicle`); nothing else. `keep` removes an entity from the plugin's ownership list without removing it from the world: the release hook and the stop sweep never see it, and `set_transform` and `despawn` by that plugin refuse it afterwards. No SDK removal authority survives; retained-build undo uses `game.owned` instead. It logs `[game] game.entities: <plugin> kept <id>: it stays when the plugin unloads`, `[game] game.entities: <plugin> spawned <class> as <id>` and `[game] game.entities: <plugin> unloaded: removing the <n> entities it spawned`. `class_of` reads the entity's class name the way the game's own entity dump does (entity slot `0x20`, then that class's slot `0x18`, both pinned by the `entities.class_site` row) and answers `SCO_FAILED` rather than text that isn't a plausible class name.
- The `watch` type is a class name (exact, case-sensitive) or `PREFIX*`. It is refused with `SCO_BAD_ARG` and a reason for: `NULL`, empty, a lone `*`, a `*` that isn't the last character (`A*B`, `**`, `*A`), more than 63 characters, and any byte that isn't printable ASCII (0x21 to 0x7E) or is `?`. The host matches before calling the plugin; events reach it a tick later, in order, up to 256 held per watch (the oldest are dropped and logged).
- The implementations are in `src/game/services/` (library `sco_game_services`, Windows only: game reads run under SEH). Builds without them log `[app] game services not built` and go on.

### Entities watch and query_radius diagnostic run

`game.entities.watch` and `game.entities.query_radius` are off. The [game world spikes](design/game-world-spikes.md) found where the game does these things (the entity walk; `CEntitySystem::CallOnSpawnSinks` and `DeleteEntity`) but left what only a run can show: the thread the callbacks run on, the spawn batch's vector layout and the walk's flag byte. Until a run confirms them, both capabilities are not ready, and `watch` and `query_radius` answer `SCO_UNAVAILABLE` with a reason in `last_error`.

To collect that evidence, start the game once with the environment variable `SCO_ENTITIES_DIAG=1`. The game pack installs the two streaming hooks log-only. Twenty seconds after you spawn, it logs your own entity's class and runs the walk with each flag value. Read the `[game] game.entities diag:` lines in `mod.log`: callbacks per hook, the thread each ran on against the game thread, the vector's raw words and element count, the class and id of the first entities, and the walk's callback count right away, 1 s and 3 s later. Then set `kWatchConfirmed` and `kQueryRadiusConfirmed` in `sco/game/entities.h`. The diagnostics change nothing else, and nothing runs without the variable.

## Retained build and owned-seat removal (`game.owned`)

`game.owned` 1.0 ([`sc_owned.h`](../include/sc_owned.h)) composes the existing entities and actors despawn and the vehicle seat reads. Native C and C++ only; C# and Lua are unsupported.

- Build placements opt into `retain_build`. They stay owned and movable for undo and clear while loaded, and are removed on ordinary unload.
- Legacy `keep` refuses retained placements, and grants no SDK removal rights for unretained ones.
- `remove_npc` and `remove_seat_npc` require actors-owned NPCs. A fresh expected seat occupant never grants rights over a foreign NPC or the player.
- Bounded receipts report that removal was initiated, not that the entity disappeared.
- There is no persistence across reloads, no arbitrary delete and no new player-vehicle registration.

[Policy and open acceptance](design/owned-removal.md).

## Owned prefabs (`game.prefabs`)

`game.prefabs` 1.0 ([`sc_prefabs.h`](../include/sc_prefabs.h)) places an owned prefab through the existing object-container spawner. Native C and C++ only; C# and Lua are unsupported. The game pack publishes it alongside entities. Query success is not capability: capability `game.entities.prefabs` is on by default, and it still refuses until readiness is met or after a provider fault.

- It uses the existing entities ownership, mover and despawn, not arbitrary delete.
- Canonical content-relative paths (at most 511 bytes), finite normalized poses and the shared 1,024 and per-owner 128 entity quotas are checked before any engine call.
- Pending ownership never expires because `alive` is 0.
- Use the `game.owned` retained-build policy for undo while loaded. `keep` gives up all rights.

[Design](design/prefabs.md).

## World control (`game.world.control`, off)

`game.world.control` 1.0 ([header](../include/sc_world_control.h), [contract](design/world-control.md)) supplies filtered aim rays and bounded, owner-exclusive, reversible camera leases through a separate table. Native C and C++ only; C# and Lua are unsupported. `game.world.raycast_filtered` and `game.world.free_camera` are off until a verified offline engine adapter exists. Existing world reads are unchanged.

## Internal game-pack helpers

These are not services: they have no table and no `query_service`. They are functions in `src/game/services/` (library `sco_game_services`, Windows only) that a product links and calls, because they read live game objects that a signature row cannot express. They run under SEH, answer 0 on a fault, and give a stable reason string. Call them from the game thread only.

| Header | Function | What |
|---|---|---|
| [`sco/game/missions.h`](../include/sco/game/missions.h) | `sco::game::missions::ScriptLibrary(manager, &reason)` | The mission script library, from the live mission manager (slot `0x48` of its vtable; canary `48 8B 4B` at `+0x32` of that function; the field displacement is the byte at `+0x35`; all checked on every call). Returns 0 with `kReasonNotFound` or `kReasonNotYet`. sc-offline's `missions` built-in calls it. |
