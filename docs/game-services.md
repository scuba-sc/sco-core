# Game services

The Star Citizen game pack publishes its services under the reserved owner `game` ([design](design/game-services.md), implemented). Plugins find them with the ordinary `query_service`; `sco_api.h` doesn't change. They exist only in builds with the game pack ([game pack](game-pack.md)), and only when the product turns them on (`sco::app::Platform::gameServices`). Where they sit among the layers (kernel, host services, game pack, plugin SDK, consumers): [architecture](architecture.md#layers).

**What is on, and what has been checked:** [Status of every game.* table](game/status.md). In short: every table below resolves its rows on 4.10.196.36804; **none has been run in game since it landed**; `game.entities.watch`, `game.entities.query_radius`, `game.events.vehicle_seat` and `game.actors.state` are off until an in-game check; the camera's field of view is blocked. The "Since game pack" column says which game-pack release first carried the table.

| Service | Version | Header | Since game pack | What |
|---|---|---|---|---|
| `teleport.spatial` | 1.0 | [`sc_spatial.h`](../include/sc_spatial.h) | 0.1.0 | Where you are, and positions between the game's zones |
| `spawn.entities` | 1.2 | [`sc_spawn.h`](../include/sc_spawn.h) | 0.2.0 | Spawn entities near you, your entity and ship ids, and move what you spawned |
| `game.actors` | 1.1 | [`sc_actors.h`](../include/sc_actors.h) | 0.2.0 | Your player (`local_player`), NPCs you spawn (`spawn_npc(self, ...)`) and despawn (`despawn(self, id)`); the host removes a plugin's NPCs on ordinary owner release (including unload or non-retained `crashed` cleanup), not on a restart-required retained-provider fault. Capabilities `game.actors.local_player`, `.spawn_npc`, `.despawn`. 1.1 appends `health(id, &cur, &max)` (the HealthPool stat; `max` is 0, not read yet; capability `game.actors.health`) and `state(id, &state)` (never ready yet: `SCO_UNAVAILABLE`, capability `game.actors.state` stays off until an in-game run says which of the game's two "not fully alive" checks is dead and which incapacitated). Wrappers: `scosdk/game/actors.hpp`, C# `Sco.Sdk.Game.Actors`, Lua `sco.game.actors.local_player` and `.health` (read-only) |
| `game.vehicles` | 1.0 | [`sc_vehicles.h`](../include/sc_vehicles.h) | 0.2.0 | The ship you're aboard, its seats and occupants, seating and unseating your own actors, Flight Ready. Capabilities `game.vehicles.seats`, `.seat`, `.flight_ready` ([API](api-v1.md#gamevehicles-10-game-pack)) |
| `game.world` | 1.0 | [`sc_world.h`](../include/sc_world.h) | 0.2.0 | `raycast(zone, from, dir, max_dist, &hit)` into the game's physics (position and distance; the hit entity and normal are not pinned yet, so `entity_id` and `normal` stay 0 and `flags` says so; `max_dist` clamps to `SC_WORLD_RAY_MAX_DISTANCE`, 20,000 m) and `camera(&pos, &rot_xyzw, &zone)` (world frame, plus your zone; **no field of view**: the game doesn't pin one). Capabilities `game.world.raycast`, `game.world.camera`. Wrappers: `scosdk/game/world.hpp`, C# `Sco.Sdk.Game.World`, Lua `sco.game.world.raycast` and `.camera` (read-only) |
| `game.*` events | bus | [`sc_game_events.h`](../include/sc_game_events.h) | 0.2.0 | `game.player.spawned`, `game.player.died`, `game.zone.changed` on the kernel event bus, with size-prefixed payloads; `game.vehicle.boarded` / `exited` reserved. See [Events](#events). Wrappers: `scosdk/game/events.hpp`, C# `GameEvents`, Lua `sco.subscribe` |
| `game.entities` | 1.1 | [`sc_entities.h`](../include/sc_entities.h) | 0.2.0 | Any entity: `alive`, `class_of`, `get_transform`; `set_transform(self, ...)` for what you spawned and the player's own vehicles; `spawn(self, ...)` anywhere and `despawn(self, id)`; the type-filtered `watch` / `unwatch` of entities streaming in and out; `query_radius`. Ordinary owner release removes a plugin's entities and ends its watches; restart-required retained-provider faults skip that resource cleanup and retain them while host callbacks are suppressed (1.1's `keep(self, id)` releases one entity from that: it stays in the world). Capabilities `game.entities.transform`, `.spawn`, `.class_of`, `.query_radius`, `.watch` (the last two **off** until an in-game check, see below; [API](api-v1.md#gameentities-11-game-pack)). Wrappers: `scosdk/game/entities.hpp`, C# `Sco.Sdk.Game.Entities`, Lua `sco.game.entities` (`alive`, `class_of`, `get_transform`; read-only) |

Not a table: the capability `game.mining` (the row `mining.cell`, [mining](game/mining.md)) tells a product that the game's biome builder cell function was found; the natural-mining feature that hooks it stays in the product.
| `game.creative` | 1.0 | [`sc_creative.h`](../include/sc_creative.h) | 0.2.0 | The local player's god mode, fly (noclip) and its speed, infinite ammo for carried weapons and for the ship you're aboard: `set_god_mode(self, on)`, `set_fly_mode`, `set_fly_speed`, `set_infinite_ammo`, `set_infinite_ship_ammo`, `state`, `last_error`. A toggle belongs to the plugin that switched it on and goes off on ordinary owner release (including unload or non-retained `crashed` cleanup). Restart-required retained-provider faults skip this resource hook and do not switch it off. Capabilities `game.creative.god_mode`, `.fly`, `.ammo`, `.ship_ammo` ([API](api-v1.md#gamecreative-10-game-pack), [rows](game/creative.md)). Wrappers: `scosdk/game/creative.hpp`, C# `Sco.Sdk.Game.Creative`, Lua `sco.game.creative.state` (read-only) |

`teleport.spatial` and `spawn.entities` keep their names and tables for all of 1.x: sc-offline's `teleport` and `spawn` built-ins published them before the game pack did, and a plugin can't tell the difference. New game services are named `game.<name>`.

## Owned prefab extension (G4)

`game.prefabs` 1.0 (`sc_prefabs.h`), native C/C++ only. The game pack publishes
it alongside entities; query success is not capability. `game.entities.prefabs`
remains OFF pending owner-run streamed spawn/unload cleanup acceptance.
It uses existing entities ownership, mover and despawn, not arbitrary delete.
Canonical content-relative paths, shared/per-owner quotas and finite poses are
checked before engine calls. Pending ownership never expires via alive=0.
Use game.owned retained-build policy for undo while loaded; keep gives up rights.
See [design and Windows acceptance](design/prefabs.md). C#/Lua unsupported.

## For products

### Retained build and owned-seat removal (G9, unreleased)

`game.owned` 1.0 ([`sc_owned.h`](../include/sc_owned.h)) composes existing
entities/actors despawn and vehicle seat reads. Build placements opt into
`retain_build`, remain owned/movable for undo/clear while loaded, and are removed
on ordinary unload. Legacy `keep` refuses retained placements and grants no SDK
removal rights for unretained ones. `remove_npc` and `remove_seat_npc` require
actors-owned NPCs; a fresh expected seat occupant never grants foreign/player
deletion rights. Bounded receipts report initiation only, not disappearance.
C/C++ direct route; C#/Lua unsupported. No across-reload persistence, arbitrary
delete or new player-vehicle registration. [Policy and owed acceptance](design/owned-removal.md).
Synthetic evidence only; **not run in game**.

- `sco::host::ProvideGameService(name, version, table)` publishes under the owner `game` (`sco/host.h`): `game.<name>`, or one of the product names a game pack took over (`kGameCompatNames`). The owner is never released; `sco::app::Stop` withdraws game services after every plugin has unloaded.
- `sco::app::Platform::gameServices = true` starts them before any plugin loads. They read the game through the `teleport.*` and `spawn.*` rows, so resolve the rows first. A product that still publishes `teleport.spatial` or `spawn.entities` itself must leave it off, or drop its own provider: the second provider of a name is refused.
- `spawn.entities`' mover lets any plugin move the player's own vehicle once the product registers it (`sco::game::services::RegisterPlayerVehicle` / `UnregisterPlayerVehicle`, `sco/game/services.h`, game thread). Entities a plugin spawned through `spawn_as` stay movable by that plugin only. Ordinary owner release (including unload or non-retained `crashed` cleanup) forgets their ownership; restart-required retained-provider faults skip the hook and retain that state.
- `game.actors` reads your player through the `teleport.*` rows and `spawn.handle_to_id`, spawns through `spawn.entities`' spawner (`spawn.helpers` rows), and removes NPCs as sc-offline's Clear NPCs did (`npc.clear` rows: RemoveEntity, checked 1.5 s later, then the `npc.direct_remove` fallback, then a move far out of range). It logs `[game] game.actors: <plugin> spawned <class> as NPC <id>` and `[game] game.actors: <plugin> unloaded: removing the <n> NPCs it spawned`. `spawn_npc` refuses when it couldn't remove the NPC again (no `npc.clear` rows, or no release hook).
- `game.world` casts through the physics world the `build.*` rows find (`build.ground_ray` capability rows; the ray block, slot `0x1C0` and grid release are sc-offline's build mode moved over) and reads the camera at the offsets `build.camera_fields` pins. The cast tries the zone you pass and up to two of its parents, never the world's root zone. It logs nothing on success. The first cast checks a live entity's vtable slots against the rows and answers `SCO_UNAVAILABLE` if the game changed them.
- `game.actors` 1.1's `health` finds an entity id's actor (yours, or through its `ISCItemUser` component as `game.vehicles` does) and reads the HealthPool stat through the `actor.*` rows. `state` stays off. To decide the dead/incapacitated mapping the game pack logs, with the `actor.state_*` rows OK, `[game] game.actors state probe (your player): P1 (status vtable slot 0x60) = <0|1>, P2 (CSCActorStatus::IsDeadConfirmed) = <0|1>, HealthPool = <n>` whenever either answer changes (at most every 250 ms), and `... state probe (died): ...` from the death hook. Get downed (incapacitated), then die: which of P1 and P2 is 1 in each state decides the mapping. The probe goes away when `state` ships.
- `game.entities` spawns through the same spawner (`spawn.helpers` rows) and removes with game.actors' removal (`npc.clear` rows), so `spawn` refuses (`SCO_UNAVAILABLE`) when it couldn't remove the entity again. `set_transform` moves what the plugin spawned (through `game.entities`, `spawn_as` or `game.actors`) and the player's own vehicles (`RegisterPlayerVehicle`); nothing else. `keep` (1.1) removes an entity from the plugin's ownership list without removing it from the world: the release hook and the stop sweep never see it, and `set_transform` and `despawn` by that plugin refuse it afterwards (no SDK removal authority survives; retained build undo instead uses game.owned). It logs `[game] game.entities: <plugin> kept <id>: it stays when the plugin unloads`, `[game] game.entities: <plugin> spawned <class> as <id>` and `[game] game.entities: <plugin> unloaded: removing the <n> entities it spawned`. `class_of` reads the entity's class name the way the game's own entity dump does (entity slot `0x20`, then that class's slot `0x18`, both pinned by the `entities.class_site` row) and answers `SCO_FAILED` rather than text that isn't a plausible class name.
- **`game.entities` watch and query_radius are off.** [Appendix B](design/game-world-spikes.md) found where the game does these things (B3: the entity walk; B9: `CEntitySystem::CallOnSpawnSinks` and `DeleteEntity`) but left what only a run can show: the thread the callbacks run on, the spawn batch's vector layout and the walk's flag byte. Until the maintainer's run confirms them, `game.entities.watch` and `game.entities.query_radius` are not ready and `watch` / `query_radius` answer `SCO_UNAVAILABLE` with a reason in `last_error`. To get that run's evidence, start the game once with the environment variable `SCO_ENTITIES_DIAG=1`: the game pack installs the two streaming hooks log-only and, 20 s after you've spawned, logs your own entity's class and runs the walk with each flag value. Read the `[game] game.entities diag:` lines in `mod.log` (callbacks per hook, the thread each ran on against the game thread, the vector's raw words and element count, the class and id of the first entities, and the walk's callback count right away, 1 s and 3 s later). Then set `kWatchConfirmed` / `kQueryRadiusConfirmed` in `sco/game/entities.h`. Diagnostics change nothing else, and nothing runs without the variable.
- The `watch` type is a class name (exact, case-sensitive) or `PREFIX*`. Refused with `SCO_BAD_ARG` and a reason: `NULL`, empty, a lone `*`, a `*` that isn't the last character (`A*B`, `**`, `*A`), more than 63 characters, and any byte that isn't printable ASCII (0x21 to 0x7E) or is `?`. The host matches before calling the plugin; events reach it a tick later, in order, up to 256 held per watch (the oldest are dropped and logged).
- The implementations are in `src/game/services/` (library `sco_game_services`, Windows only: game reads run under SEH). Builds without them log `[app] game services not built` and go on.

## Optional G5 world control (disabled)

`game.world.control` 1.0 ([header](../include/sc_world_control.h),
[contract](design/world-control.md)) supplies filtered aim rays and bounded,
owner-exclusive reversible camera leases via a separate table. Native C/C++
only; C#/Lua unsupported. Both `game.world.raycast_filtered` and
`game.world.free_camera` are OFF pending a verified offline engine adapter;
synthetic policy tests do not certify gameplay. Existing world reads unchanged.

## Events

The game pack puts low-volume events on the kernel event bus (design decision 8; entity streaming is the type-filtered watch on `game.entities` instead). Subscribe by name with `sco_api`'s `subscribe`, `Plugin::Subscribe`, C# `GameEvents` or Lua `sco.subscribe`. The data argument is a pointer to a size-prefixed struct from [`sc_game_events.h`](../include/sc_game_events.h); copy what you keep. Handlers run on the game thread, from the host's tick, a frame or two after the game did it: the game pack's hooks only record the occurrence into a small fixed ring (a mutex held for a few stores, no allocation, no calls out) and never run plugin code. A hook is installed only when its capability's rows are OK, and an event whose capability isn't ready never fires.

| Event | Payload | Capability | How | Status |
|---|---|---|---|---|
| `game.player.spawned` | `sc_game_player_spawned { entity_id, zone_id }` | `game.events.player_spawned` | hook on the game's `QueueEvent<SPI_Player_OnSpawn>` (`event.player_spawn`); published only for your player | on when its rows are OK |
| `game.player.died` | `sc_game_player_died { entity_id, killer_id }` (`killer_id` is always 0: the death record doesn't pin it) | `game.events.player_died` | hook on `QueueEvent<SPI_Player_OnDeath>` (`event.player_death`); only your player | on when its rows are OK |
| `game.actors.died` | `sc_game_actor_died { entity_id, killer_id }` (unknown ID and killer are 0) | `game.events.actor_died` | all-actor view of the same `event.player_death` hook; ID captured before original code runs | on when its rows and hook are OK; gameplay unverified |
| `game.zone.changed` | `sc_game_zone_changed { old_zone_id, new_zone_id }` | `game.events.zone_changed` | the host's tick reads your zone id every 100 ms on the `teleport.*` rows (no hook); not published while you aren't spawned | on when the `teleport.*` rows are OK |
| `game.vehicle.boarded` / `game.vehicle.exited` | `sc_game_vehicle_seat { seat_index, vehicle_entity_id }` (reserved) | `game.events.vehicle_seat` | hooks on `CSCActorResultStateLinked::Enter` / `::Exit` (`event.seat_enter`, `event.seat_exit`) | **off**: log-only until an in-game run confirms the payload |

`game.player.spawned` waits up to 10 s for your player to be readable before it gives up (a spawn record arrives before the actor is fully set up); a spawn is not a zone change (the spawn's zone is the baseline for `game.zone.changed`). Zone and entity ids are session handles.

**Vehicle seat probe.** The seat hooks install when their rows are OK and only log, because nothing in the exe says which actor a seat transition belongs to or that the link is a vehicle seat (it may be a chair or a bed). Each Enter and Exit logs `[game] vehicle_seat probe: Enter|Exit: state=0x… host=0x… localActor=0x… parents: <id> '<name>' > …` (your player's chain of parent entities, up to 4; at most 100 lines a session). Board a pilot seat, a turret, a station chair and a bed, and read which lines say which. That decides the actor filter and the vehicle test; then the capability goes on.

## Internal game-pack helpers

Not services: no table, no `query_service`. Functions in `src/game/services/` (library `sco_game_services`, Windows only) that a product links and calls, because they read live game objects a signature row can't express. They run under SEH, answer 0 on a fault, and give a stable reason string. The game thread only.

| Header | Function | What |
|---|---|---|
| [`sco/game/missions.h`](../include/sco/game/missions.h) | `sco::game::missions::ScriptLibrary(manager, &reason)` | The mission script library, from the live mission manager (slot `0x48` of its vtable; canary `48 8B 4B` at `+0x32` of that function; the field displacement is the byte at `+0x35`; all checked on every call). 0 with `kReasonNotFound` or `kReasonNotYet`. sc-offline's `missions` built-in calls it; the lookup used to be a raw scan there. |
