# game.actor_stats: an actor's status stats

`sc_actor_stats.h` · service `game.actor_stats` · version 1.0 · game thread only ·
capability `game.actor_stats.read`.

Star Citizen tracks an actor's survival state as a set of numbered stats: hunger, thirst,
body and suit temperature, oxygen, stun, distortion, body and suit radiation, health and
others. `game.actors` already reads one of them (`health`, the HealthPool stat). This
service reads the others by name, so a plugin can show a vitals HUD, warn before a hazard
hurts, or log what a med pen, a meal or a radiation zone does.

The plugin ABI is the header. The rest of this page is for the game pack and hosts.

## What a plugin gets

`read(id, stat, &value)` returns the current value of one stat for the actor whose entity
id is `id`: your player (`game.actors` `local_player`'s entity id) or an NPC once it has
streamed in. `stat` is an `sc_actor_stat`:

| `sc_actor_stat` | the game's stat |
|---|---|
| `SC_ACTOR_STAT_HUNGER` | Hunger |
| `SC_ACTOR_STAT_THIRST` | Thirst |
| `SC_ACTOR_STAT_BODY_TEMPERATURE` | BodyTemperature |
| `SC_ACTOR_STAT_SUIT_TEMPERATURE` | SuitTemperature |
| `SC_ACTOR_STAT_STUN` | Stun |
| `SC_ACTOR_STAT_DISTORTION` | Distortion |
| `SC_ACTOR_STAT_OXYGEN` | GasSaturationO2 |
| `SC_ACTOR_STAT_BODY_RADIATION` | BodyRadiation |
| `SC_ACTOR_STAT_SUIT_RADIATION` | SuitRadiation |

Values are the game's own floats, in the game's own units. **Nothing here has been run in
game**, so units, range and direction are not documented yet: for example, whether hunger
counts up toward starving or down from full, or whether temperatures are degrees Celsius.
A plugin should log raw values and not hard-code thresholds until the in-game run below has
been done.

An actor that has no record of a stat (the game keeps a record per stat type, and not every
actor type has every one) answers `SCO_NOT_FOUND`, never a silent 0.

## How the game pack finds it

All of this is static evidence on the verified build (F-4.10.197.2878-006):

* The actor's status object and the game's `GetStat(status, index)` are the existing
  `actor.status_accessor` and `actor.get_stat` rows that already back `game.actors.health`.
* `GetStat` indexes an array of stat records by a stat-type enum. The game has a
  name function for that enum (index to name, a bounded jump table). The new row
  **`actor.stat_names`** finds that function by a unique pattern, follows its table and
  checks that every index the game pack uses still answers its expected name, plus
  HealthPool (the index `game.actors.health` reads) as a cross-check. If a patch reorders
  the enum, the row fails and `game.actor_stats.read` goes off. It does not read the wrong
  stat.
* The new row **`actor.stat_array`** checks the shape of `GetStat`'s body and gives the record
  array's offset from its own instruction. That way the service can tell "no record" from a
  real zero without a hard-coded offset.

Both rows resolve in a local `sco-sigcheck` run on 4.10.197.2878. The full table resolves
319/319 there.

## What an in-game run must confirm

Before any value semantics are documented, a maintainer run logs every stat for your
player each tick (the draft test plugin does this) while:

1. eating and drinking (hunger, thirst);
2. standing on a cold planet and a hot one, suited and unsuited (body and suit temperature);
3. losing and regaining helmet oxygen (oxygen);
4. being stunned (stun);
5. being hit by a distortion weapon (distortion);
6. standing in a radiation area (body and suit radiation).

That run records units, range and direction per stat. Until then
`game.actor_stats.read` is evidence level "rows resolved", not "verified in game".

## Writes: game.actor_stats_write

`sc_actor_stats_write.h` · service `game.actor_stats_write` · version 1.0 · game thread only ·
capability `game.actor_stats.write` (default **OFF**).

A separate table, so a plugin that only reads never sees a write route.

- `adjust(self, id, stat, delta)` adds `delta` to one stat.
- `set(self, id, stat, value)` reads the current value and adjusts by the difference.

Both go through the game's own stat-adjust function (row **`actor.adjust_stat`**,
F-4.10.197.2878-010). The game clamps the value and runs its own consequences: a stat change can
incapacitate. The game also ignores hunger, thirst and blood-drug changes while their
`pl_status.enable*` switch is off, so read the stat back to see what took effect.

What must hold before anything is written:

1. **The switch is on.** A product calls `sco::game::services::SetActorStatWritesEnabled(true)`
   from a player or host setting. The default is off. For a maintainer's in-game test without a
   product change, the environment variable `SCO_ACTOR_STATS_WRITE=1` turns it on when
   `game.actors` starts.
2. **The rows resolve.** `actor.adjust_stat` and every row `game.actor_stats.read` needs must be
   OK.
3. **The session is offline.** Every write first asks the host for fresh offline admission. Without
   it the answer is `SCO_UNAVAILABLE` and nothing is written (rule 11).
4. **It is your own player.** In 1.0, `id` must be your player's entity id. NPC writes are not
   offered.
5. **The step is bounded.** `|delta|`, and for `set` `|value - current|`, is at most
   `SC_ACTOR_STATS_WRITE_MAX_STEP`, and every input is finite.

Nothing is restored when a plugin unloads. These stats are gameplay state that the game itself
changes every tick (eating, breathing, weather, damage), not settings with a prior value to put
back.

The adjust function logs whether the caller is the authority or only predicting. **Whether a write
persists in an offline session or is overwritten by the game's own simulation is not known
statically.** That is the first thing the in-game run must confirm for each stat. Until then
`game.actor_stats.write` stays OFF in the status registry.

## Language routes

C and C++ call the tables directly (`sc_actor_stats_read`, `sc_actor_stats_adjust` and `sc_actor_stats_set` check the table's size first).
The C# and Lua game.actor_stats routes are unsupported in 1.0, for reads and writes.
