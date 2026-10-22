# Actor status and game event rows

Which of these capabilities are on, and how far each has been checked: [status](status.md) (`game.actors.state` and `game.events.vehicle_seat` are off until an in-game run; `game.events.mission_changed` is on
by default, by owner ruling, and fires only where a game.missions backend reports active missions).

The rows in [`src/game/events_sigs.cpp`](../../src/game/events_sigs.cpp) come from the spikes in [game world spikes](../design/game-world-spikes.md) (B4, B5, B6, B8), each locator and byte check as written there and proven unique in the .text of 4.10.196.36804. Accessor: [`sco/game/events.h`](../../include/sco/game/events.h).

| Capability | Rows |
|---|---|
| `game.actors.health` | `actor.health_site`, `actor.status_accessor`, `actor.get_stat` |
| `game.actors.state` | `actor.state_site`, `actor.status_accessor`, `actor.is_dead_confirmed` |
| `game.events.player_spawned` | `event.player_spawn` |
| `game.events.player_died` | `event.player_death` (local-player view) |
| `game.events.actor_died` | `event.player_death` (all-actor view of the same hook) |
| `game.events.vehicle_seat` | `event.seat_enter`, `event.seat_exit` |

| Row | What it finds and checks |
|---|---|
| `actor.health_site` | One unique pattern in the code that logs "...HealthPool: %.4f, Stun: %.4f": `+0x00` calls the status accessor, `+0x0D` `GetStat(status, 6)`, `+0x1D` and `+0x2A` the accessor and `GetStat(status, 0x0B)` again. Checks the accessor is `mov rax,[rcx+208h]; add rax,74A0h; ret` and that both pairs of calls reach the same functions |
| `actor.status_accessor` / `actor.get_stat` | The call targets at `actor.health_site +0x00` and `+0x0D` |
| `actor.state_site` | One unique pattern in the "not fully alive" test, `P2(status) \|\| P1(status)`: the accessor call at `+0x00`, `P2` called at `+0x0B`, `mov rdx,[rcx+60h]` (`P1`, status vtable slot `0x60`) at `+0x17` |
| `actor.is_dead_confirmed` | `P2`: checked to be the function that labels itself `CSCActorStatus::IsDeadConfirmed` (one label load, its .pdata function). Which of `P1` and `P2` means dead and which incapacitated is not pinned by any byte |
| `event.player_spawn` | `SCigEventDispatcher::QueueEvent<SPI_Player_OnSpawn>`: one unique prologue; `+0x2C` calls entity slot `0x7A8`, `+0x60` loads its label (and the label's function is the match) |
| `event.player_death` | Function containing the inline `QueueEvent<SPI_Player_OnDeath>` label: one load at `+0x414` and its containing .pdata function; prologue, `+0x22` (`rbx = rdx; r15 = rcx`) and `+0x69` (the actor's entity handle) checked |
| `event.seat_enter` / `event.seat_exit` | `CSCActorResultStateLinked::Enter` / `::Exit`: one label load each and .pdata; prologues checked |


## game.loadout.respawn_completed (no native row yet)

The loadout owner provides an occurrence-only event, deferred to the game-thread
tick after a fresh offline admission check. Its capability
`game.events.loadout_respawn_completed` defaults OFF; native dispatch site and
calling convention remain unverified (F-INTAKE-004 / T-3). The host producer seam
is tested with the real event bus and a native subscriber; not run in game.
See [loadout lifecycle](loadout.md) for coalescing and Stop behavior.

## game.mission.changed (no rows)

`game.events.mission_changed` gates on no signature rows. It polls the game.missions service's accepted-mission statuses
(offline::PollStatuses) on the tick and publishes state/phase/objective changes, so it fires only
when a game.missions backend is bound and admitted (production adapter; offline sessions). Payload
`sc_game_mission_changed` is pinned by `tests/abi_game_events.c`; see `sc_game_events.h` for stream semantics.
