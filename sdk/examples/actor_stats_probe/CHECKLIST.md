# actor_stats_probe: in-game checklist

This is the maintainer's run that turns `game.actor_stats` from "rows resolved" into "verified in
game", and decides whether `game.actor_stats.write` may ever default on. **Nothing below has been
run yet.** Record every result, with the build and the date, before any capability changes state
(rule 8).

## Before you start

1. Use a build of the game pack with `game.actor_stats` and `game.actor_stats_write`, on the game
   build its rows were checked on (4.10.197.2878). Check the startup log for these lines:
   - `game.actor_stats.read` ready (no "not ready" line);
   - `game.actor_stats.write is ...`, which says ON only if step 2 was done.
2. To test writes, set `SCO_ACTOR_STATS_WRITE=1` in the environment the game starts with. Without
   it, run only the read steps.
3. Install `actor_stats_probe` (build: `cmake -S sdk -B build && cmake --build build`, or this
   folder on its own against an unpacked SDK). Load into an **offline** session.
4. The probe logs one `stats id=... hunger=... thirst=... body_temp=... suit_temp=... stun=...
   distortion=... oxygen=... body_rad=... suit_rad=...` line per second. A `*` marks a value that
   changed since the line before. `n/a(2)` means the actor has no record of that stat; `n/a(1)`
   means the capability is off.

## Reads: one row per stat

For each step, note the value before, the value during and after, and which way it moved.

| # | Do | Watch | Settles |
|---|---|---|---|
| R1 | Stand still for 5 minutes, then eat one food item | `hunger` | units, range, direction (does it rise toward starving or fall from full?), and whether eating moves it |
| R2 | Stand still for 5 minutes, then drink one drink | `thirst` | the same, for thirst |
| R3 | Go to a cold planet surface (for example a moon at night) with no helmet, then with a sealed suit | `body_temp`, `suit_temp` | whether the units are degrees C, and which value the suit protects |
| R4 | Go to a hot surface (a desert day), as in R3 | `body_temp`, `suit_temp` | the same, upward |
| R5 | Take the helmet off where there is no atmosphere, then put it back on | `oxygen` | range and direction of the oxygen saturation |
| R6 | Get stunned (a stun weapon, or a fall hard enough to stun) | `stun` | units and decay; cross-check with the game's own "Stun: %.4f" log line if it appears |
| R7 | Get hit by a distortion weapon | `distortion` | whether it accumulates on the actor at all |
| R8 | Stand in a radiation area with and without a rated suit | `body_rad`, `suit_rad` | which one is dose and which is suit load, and the decay after leaving |

## Writes: only with step 2, and only offline

First run `actor_stats_probe.modes` (or the panel's **Log write modes**). For each stat it logs
what the game would do with a change: `authoritative` (your entity has authority), `local_only`
(no authority, but the stat allows a local change) or `discarded` (the game would ignore it, and
the write is refused with result 1). **Record this line first. It answers the authority question
for the offline session directly.**

Arm the probe first: toggle **Arm writes** in its panel, or run `actor_stats_probe.arm 1`. Each
write logs `adjust|set <stat> <n> -> result <r> (mode <m>, ...); read back <stat>=<v>`. Then watch the next
5-10 per-second lines to see whether the value **stays** or snaps back.

| # | Do | Pass when |
|---|---|---|
| W1 | `actor_stats_probe.adjust hunger 10`, with `pl_status.enableHunger` on | result 0, the read-back moves by about 10 and the next lines keep it (modulo normal decay) |
| W2 | The same with `pl_status.enableHunger 0` | result 0 but no change (the game ignores it). This confirms the doc's caveat |
| W3 | `actor_stats_probe.set thirst <a value inside the R2 range>` | it lands on the value and stays |
| W4 | `actor_stats_probe.set body_temp <current + 5>` | it lands, then drifts back toward the environment at the game's own rate |
| W5 | `actor_stats_probe.adjust stun <a small step>` | the stun effect shows, or the value moves without the effect: record which |
| W6 | `actor_stats_probe.adjust body_rad <a small step>` | the radiation effect shows: record which |
| W7 | Unset `SCO_ACTOR_STATS_WRITE` and restart | the log says `game.actor_stats.write is off`; every write answers 1 (UNAVAILABLE) |
| W8 | Connect to anything that is not an offline session (if you can do so safely; otherwise skip and note it) | writes answer 1 (UNAVAILABLE), with no admission |

## Record

- Write the per-stat units, range and direction into `docs/game/actor-stats.md`, replacing the
  "not pinned" text, and flip `game.actor_stats.read` to `in_game_verified` with the date.
- Writes: if W1-W6 persist, propose a product switch (the sc-offline settings page) and keep the
  capability default OFF until you rule otherwise. If they snap back, record "predicted only,
  overwritten" and leave the write table OFF.
- Open items for a later minor: the per-stat maximum and minimum (two unnamed float fields next to
  the value), and NPC reads.
