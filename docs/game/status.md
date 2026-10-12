# Status of every game.* table

As of sco-core `main` at `2197dfb` (2026-10-11, with `game.actors.died`), game pack **0.3.0**, Star Citizen build **4.10.196.36804** (`StarCitizen.exe` PE timestamp `0x6ac64e6f`). One line per service table and per capability: its version, whether it is on by default, and how far it has been checked.

> **Nothing has been verified in game since these services landed.** Every "ON" below means the signature rows behind it resolve on the build and the code is allowed to run; it does not mean anyone has seen it work in a game session. The maintainer deferred the in-game runs to one batch after the conversion ([ledger](#sources), 2026-10-10). Nothing on this page says "verified in game" for that reason; when a run happens, this page is where its date goes.

## How to read it

**Default.** *ON*: the capability is set when its rows are OK on the running build, and the service answers (it is published only when the product sets `Platform::gameServices`; [game services](../game-services.md#for-products)). *OFF*: the code holds the capability off whatever the rows say, so the function answers `SCO_UNAVAILABLE` with a reason in `last_error`.

**Evidence level**, exactly one of:

| Level | Means |
|---|---|
| `row resolved on 4.10.196.36804 (sigcheck)` | Every row the capability reads is `OK` in a `sco-sigcheck` report of that exe. The byte checks pass and each locator matches once. Nothing was executed in the game |
| `verified in game (date)` | Someone ran it in a game session on that date and the result matched. **No entry has this level yet** |
| `OFF pending in-game confirmation` | The rows resolve, but something only a run can show is open (the thread a callback runs on, a vector layout, which of two checks means dead). The capability stays off until that run |
| `BLOCKED` | No locator we can trust, so nothing is published |

## Service tables and capabilities

| Table | Version | Capability | Default | Evidence |
|---|---|---|---|---|
| `teleport.spatial` ([`sc_spatial.h`](../../include/sc_spatial.h)) | 1.0 | the four `teleport.*` rows (the product's capability `teleport`) | ON | row resolved on 4.10.196.36804 (sigcheck) |
| `spawn.entities` ([`sc_spawn.h`](../../include/sc_spawn.h)) | 1.2 | the `spawn.helpers` rows (the product's capability `spawn.ship`) | ON | row resolved on 4.10.196.36804 (sigcheck) |
| `game.actors` ([`sc_actors.h`](../../include/sc_actors.h)) | 1.1 | `game.actors.local_player` | ON | row resolved on 4.10.196.36804 (sigcheck) |
| | | `game.actors.spawn_npc` | ON | row resolved on 4.10.196.36804 (sigcheck) |
| | | `game.actors.despawn` | ON | row resolved on 4.10.196.36804 (sigcheck) |
| | | `game.actors.health` | ON | row resolved on 4.10.196.36804 (sigcheck) |
| | | `game.actors.state` | OFF | OFF pending in-game confirmation |
| `game.vehicles` ([`sc_vehicles.h`](../../include/sc_vehicles.h)) | 1.0 | `game.vehicles.seats` | ON | row resolved on 4.10.196.36804 (sigcheck) |
| | | `game.vehicles.seat` | ON | row resolved on 4.10.196.36804 (sigcheck) |
| | | `game.vehicles.flight_ready` | ON | row resolved on 4.10.196.36804 (sigcheck) |
| `game.world.control` ([`sc_world_control.h`](../../include/sc_world_control.h)) | 1.0 | `game.world.raycast_filtered`, `game.world.free_camera` | OFF | Synthetic policy only; no verified offline engine adapter, no Windows/game proof |
| `game.world` ([`sc_world.h`](../../include/sc_world.h)) | 1.0 | `game.world.raycast` | ON | row resolved on 4.10.196.36804 (sigcheck) |
| | | `game.world.camera` (position, rotation, zone) | ON | row resolved on 4.10.196.36804 (sigcheck) |
| | | camera field of view | none | BLOCKED |
| `game.entities` ([`sc_entities.h`](../../include/sc_entities.h)) | 1.1 | `game.entities.transform` (`alive`, `get_transform`, `set_transform`) | ON | row resolved on 4.10.196.36804 (sigcheck) |
| | | `game.entities.spawn` (`spawn`, `despawn`, `keep`) | ON | row resolved on 4.10.196.36804 (sigcheck) |
| | | `game.entities.class_of` | ON | row resolved on 4.10.196.36804 (sigcheck) |
| | | `game.entities.query_radius` | OFF | OFF pending in-game confirmation |
| | | `game.entities.watch` (`watch`, `unwatch`) | OFF | OFF pending in-game confirmation |
| `game.*` events ([`sc_game_events.h`](../../include/sc_game_events.h)) | bus | `game.events.player_spawned` (`game.player.spawned`) | ON | row resolved on 4.10.196.36804 (sigcheck) |
| | | `game.events.player_died` (`game.player.died`) | ON | row resolved on 4.10.196.36804 (sigcheck) |
| | | `game.events.actor_died` (`game.actors.died`) | ON | row resolved on 4.10.196.36804 (sigcheck); the same row as player_died, one hook two events |
| | | `game.events.zone_changed` (`game.zone.changed`) | ON | row resolved on 4.10.196.36804 (sigcheck) |
| | | `game.events.vehicle_seat` (`game.vehicle.boarded` / `exited`) | OFF | OFF pending in-game confirmation |
| mining rows ([`mining.md`](mining.md)) | row | `game.mining` | ON | row resolved on 4.10.196.36804 (sigcheck) |

Fields a published table leaves empty, because nothing in the game's code pins them:

| Field | Where | Evidence |
|---|---|---|
| camera field of view | `game.world.camera` | BLOCKED (spike B2: one candidate, `CalculateFOV`, whose returned value is not pinned) |
| the entity and normal of a hit | `game.world.raycast`'s `sc_world_hit` (`entity_id` and `normal` stay 0, `flags` has neither bit) | BLOCKED (the game's hit record doesn't pin where they are) |
| the killer | `game.player.died`'s `killer_id` (always 0) | BLOCKED (the death record doesn't pin it) |
| `game.creative` ([`sc_creative.h`](../../include/sc_creative.h)) | 1.0 | `game.creative.god_mode` (`set_god_mode`) | ON | row resolved on 4.10.196.36804 (sigcheck) |
| `game.creative` | 1.0 | `game.creative.fly` (`set_fly_mode`, `set_fly_speed`) | ON | row resolved on 4.10.196.36804 (sigcheck) |
| `game.creative` | 1.0 | `game.creative.ammo` (`set_infinite_ammo`) | ON | row resolved on 4.10.196.36804 (sigcheck) |
| `game.creative` | 1.0 | `game.creative.ship_ammo` (`set_infinite_ship_ammo`, the ship you're aboard) | ON | row resolved on 4.10.196.36804 (sigcheck) |

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
