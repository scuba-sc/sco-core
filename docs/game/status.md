# Status of every game.* table

SDK 2.1.0 (published 2026-10-13 as `sdk-v2.1.0`) uses game pack **0.4.0**. That package coordinate includes additive contracts; it does not establish new game-build verification.

The dated signature evidence below was recorded at sco-core `main` `2197dfb` (2026-10-11, with `game.actors.died`), where the game pack was still **0.2.0** (0.3.0 arrived in `95a0d34` and first shipped in `sdk-v2.0.0`), against Star Citizen build **4.10.196.36804** (`StarCitizen.exe` PE timestamp `0x6ac64e6f`). One line per service table and per capability: its version, whether it is on by default, and how far it has been checked. Later sections retain their own synthetic-only and unverified limits.

> **Nothing has been verified in game since these services landed.** Every "ON" below means the signature rows behind it resolve on the build and the code is allowed to run; it does not mean anyone has seen it work in a game session. The maintainer deferred the in-game runs to one batch after the conversion ([ledger](#sources), 2026-10-10). Nothing on this page says "verified in game" for that reason; when a run happens, this page is where its date goes.

## How to read it

**Default.** *ON*: the capability is set when its rows are OK on the running build, and the service answers (it is published only when the product sets `Platform::gameServices`; [game services](../game-services.md#for-products)). *OFF*: the code holds the capability off whatever the rows say, so the function answers `SCO_UNAVAILABLE` with a reason in `last_error`. *none*: no capability of its own gates the line (a composition or conversion table), or, with BLOCKED, nothing is published.

**Source.** The service table below is generated from `sdk/status_registry.json` by `python3 tools/status_registry.py --write`, and CI fails when the two differ (`--check`). Change the registry, never the table; its evidence levels restate what the linked pages already record and never claim an in-game run.

**Evidence level**, exactly one of:

| Level | Means |
|---|---|
| `row resolved on 4.10.196.36804 (sigcheck)` | Every row the capability reads is `OK` in a `sco-sigcheck` report of that exe. The byte checks pass and each locator matches once. Nothing was executed in the game |
| `verified in game (date)` | Someone ran it in a game session on that date and the result matched. **No entry has this level yet** |
| `OFF pending in-game confirmation` | The rows resolve, but something only a run can show is open (the thread a callback runs on, a vector layout, which of two checks means dead). The capability stays off until that run |
| `BLOCKED` | No locator we can trust, so nothing is published |

## Typed loadout extension (G6)

`game.loadout` 1.1 / `game.loadout.apply`: table published by Windows game-services
composition, capability **OFF — typed engine methods unverified**. Synthetic real
host/provider/native-module checks only; no trusted per-slot equip mapping and no
offline admission adapter installed. See [loadout](loadout.md). Not run in game.

`game.loadout.weapons` (new in 1.1, unreleased) gates the weapon and multitool slots 8-21
of `apply` and is **OFF by default**, vocabulary only: production starts the service
with no engine backend, so those slots answer unavailable or unsupported until a
verified backend declares weapon support. Synthetic evidence only; no per-slot port
mapping is verified and nothing was run in game. The service version macro
`SC_LOADOUT_SERVICE_VERSION` stays 1.0 while 1.1 is published, as `game.world` 1.1 does.

## Quantum control activation (G8, unreleased R1)

`game.quantum` 1.0 / `game.quantum.control` is **owner-approved and conditionally
ON** after successful registration of a complete private host backend with the
shared owner-release dispatcher installed. All enable/hold calls still require
fresh positive offline/EAC-absent admission, owned drive, game thread and bounded
values. No additional public table/header or kernel mutation route is added.

**Default production adapter absent — capability OFF.** Retained product layouts
lack a safe owned-drive association and complete first-prior/input/charge/audio/
effect restoration contract. Approval does not establish those facts. Private
host-source configuration/start is explicit, with no signature-only fallback;
see [quantum integration contract](../quantum.md). The portable configured
provider/real host test is synthetic; **Windows/MSVC, actual drive input/charge/
effects and gameplay are NOT VERIFIED; not run in game**.

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
firewall policy and gameplay are NOT VERIFIED.** G2 relocation stays disabled.

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
| `game.world.control` ([`sc_world_control.h`](../../include/sc_world_control.h)) | 1.0 | `game.world.raycast_filtered` (`raycast_filtered`) | OFF | Synthetic policy only; no verified offline engine adapter, no Windows/game proof |
| | | `game.world.free_camera` (`camera_acquire`) | OFF | Synthetic policy only; no verified offline engine adapter, no Windows/game proof |
| `game.world` ([`sc_world.h`](../../include/sc_world.h)) | 1.1 | `game.world.raycast` | ON | row resolved on 4.10.196.36804 (sigcheck) |
| | | `game.world.camera` (position, rotation, zone) | ON | row resolved on 4.10.196.36804 (sigcheck) |
| | | `game.world.free_camera_flag` (`free_camera_flag`, read-only) | OFF | OFF pending in-game confirmation |
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
| mining rows ([`mining.md`](mining.md)) | row | `mining.cell` (legacy host row) | ON | row resolved on 4.10.196.36804 (sigcheck) |
| `game.creative` ([`sc_creative.h`](../../include/sc_creative.h)) | 1.0 | `game.creative.god_mode` (`set_god_mode`) | ON | row resolved on 4.10.196.36804 (sigcheck) |
| | | `game.creative.fly` (`set_fly_mode`, `set_fly_speed`) | ON | row resolved on 4.10.196.36804 (sigcheck) |
| | | `game.creative.ammo` (`set_infinite_ammo`) | ON | row resolved on 4.10.196.36804 (sigcheck) |
| | | `game.creative.ship_ammo` (`set_infinite_ship_ammo`, the ship you're aboard) | ON | row resolved on 4.10.196.36804 (sigcheck) |
| `game.hangar` ([`sc_hangar.h`](../../include/sc_hangar.h)) | 1.0 | `game.hangar.lift` (`lift_of_landing_area`, `lift_request`) | OFF | OFF pending in-game confirmation |
| | | `game.hangar.atc_cancel` (`atc_cancel_request`) | OFF | OFF pending in-game confirmation |
| `game.reputation` ([`sc_reputation.h`](../../include/sc_reputation.h)) | 1.0 | `game.reputation.checks` (`checks`, read-only) | OFF | OFF pending in-game confirmation |
| `game.asop` ([`sc_asop.h`](../../include/sc_asop.h)) | 1.0 | `game.asop.claim_timeout` (`claim_timeout`) | OFF | OFF pending in-game confirmation |
| | | `game.asop.shard_persisted` (`shard_persisted`) | OFF | OFF pending in-game confirmation |
| `game.contracts` ([`sc_contracts.h`](../../include/sc_contracts.h)) | 1.0 | `game.contracts.stream_radius` (`stream_radius`, read-only) | OFF | OFF pending in-game confirmation |
| `game.prefabs` ([`sc_prefabs.h`](../../include/sc_prefabs.h)) | 1.0 | `game.entities.prefabs` (`spawn`) | ON | ON by owner approval for R1; synthetic admission/ABI fixtures only; Windows/MSVC, native SEH and gameplay NOT VERIFIED |
| `game.owned` ([`sc_owned.h`](../../include/sc_owned.h)) | 1.0 | no capability of its own: composes the `game.entities` and `game.actors` removal and seat reads | none | Synthetic provider/ownership fixtures and header layout checks only; not run in game or on Windows/MSVC runtime |
| `game.loadout` ([`sc_loadout.h`](../../include/sc_loadout.h)) | 1.1 | `game.loadout.apply` (`apply`) | OFF | Synthetic/native-boundary checks only; typed engine methods unverified; not run in game |
| | | `game.loadout.weapons` (weapon and multitool slots 8-21 of `apply`) | OFF | Synthetic policy only; no verified engine backend, no in-game proof |
| `game.quantum` ([`sc_quantum.h`](../../include/sc_quantum.h)) | 1.0 | `game.quantum.control` | OFF | Default production adapter absent (ON only with a configured private host backend); synthetic provider/host tests only; Windows/MSVC and gameplay NOT VERIFIED |
| `game.private_session` ([`sc_private_session.h`](../../include/sc_private_session.h)) | 1.0 | `game.private_session` | ON | ON by owner approval for R1 (loopback broker, no socket until Host/Join); synthetic service and ABI checks only; Windows/MSVC, private LAN, EAC inspection and gameplay NOT VERIFIED |
| `game.cvars` ([`sc_cvars.h`](../../include/sc_cvars.h)) | 1.0 | `game.cvars.read` (`get_bool`) | OFF | Default production adapter absent; synthetic engine callbacks only; engine types NOT VERIFIED |
| `game.cvars_write` ([`sc_cvars_write.h`](../../include/sc_cvars_write.h)) | 1.0 | `game.cvars.write` (`set_bool`) | OFF | Default production adapter absent; synthetic write fixtures only; engine types NOT VERIFIED |
| `game.economy` ([`sc_economy.h`](../../include/sc_economy.h)) | 1.0 | `game.economy.read` | OFF | Default provider unavailable pending an owner-approved Windows adapter; synthetic callbacks only |
| | | `game.economy.apply` | OFF | Default provider unavailable pending an owner-approved Windows adapter; synthetic callbacks only |
| `game.missions` ([`sc_missions.h`](../../include/sc_missions.h)) | 1.0 | `game.missions.read` | OFF | Default provider unavailable pending an owner-approved Windows adapter; synthetic callbacks only |
| | | `game.missions.accept` | OFF | Default provider unavailable pending an owner-approved Windows adapter; synthetic callbacks only |
| | | `game.missions.progress` | OFF | Default provider unavailable pending an owner-approved Windows adapter; synthetic callbacks only |
| | | `game.missions.scripts` | OFF | Default provider unavailable pending an owner-approved Windows adapter; synthetic callbacks only |
| `game.mining` ([`sc_mining.h`](../../include/sc_mining.h)) | 1.0 | `game.mining` (the service's mining activation) | OFF | Off by default; native adapter not verified on Windows or in game; synthetic provider tests only |
| `game.relocation` ([`sc_relocation.h`](../../include/sc_relocation.h)) | 1.0 | `game.relocation` | OFF | Production mutation unavailable (no certified host admission, system classifier or validated transport); synthetic provider tests only |
| `game.spatial` ([`sc_game_spatial.h`](../../include/sc_game_spatial.h)) | 1.0 | no capability of its own: full-pose conversion and copied observed zones | none | Synthetic fixtures only; not verified on Windows or in game |
| CryPak adapter rows ([`api.md`](../api.md)) | row | `game.pak.all_files` (`sco::game::pak` `Scope::AllFiles`: persistent CryPak slots, `files/` replacement mounts) | OFF | OFF pending in-game confirmation |
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
