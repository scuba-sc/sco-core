# The Star Citizen game pack

The game pack is the part of sco-core that knows Star Citizen: the signature rows in `src/game`, the game services published under the owner `game` ([game services](game-services.md)), the CryPak adapter and DataCore. It builds with `SCO_GAME_SC`, which is on by default ([architecture](architecture.md#kernel-and-game-pack)).

## Versions

The game pack has its own version line, separate from the kernel's.

| Line | Where | Tags |
|---|---|---|
| Kernel, `sco_api` 1.x | `sdk/VERSION`, `SCO_API_MAJOR` and `SCO_API_MINOR` in `sco_api.h` | `sdk-vX.Y.Z` |
| Game pack | `SCO_GAME_PACK_VERSION` in [`sc_game_pack.h`](../include/sc_game_pack.h) | `game-sc-vX.Y` |
| Each game service table | its `*_VERSION` macro, passed to `query_service` | none: it changes only with the table |

A game patch that moves rows gets a game-pack patch release. A release that adds or grows a game service raises the minor version. The SDK zip states all three versions in its `VERSIONS.txt`, generated from `sdk/VERSION`, `sco_api.h` and `include/sc_game_pack.h`.

SDK 2.4.1 ships game pack 0.8.1. Each SDK release and the game pack it carries are listed in [game builds](game/builds.md).

## Verified game builds

The current verified build is **4.10.200.58746** (PE timestamp `0x6af4861d`). All 347 rows are OK in a static `sco-sigcheck` run on that build's `StarCitizen.exe`, and the same 347 rows are OK on 4.10.197.2878. The check shows that each locator matches once and its byte checks pass. It does not show gameplay or semantic equivalence.

The reports stay on the maintainer's machine. Nothing extracted from the game is committed.

Rows OK in `sco-sigcheck` by game pack and build:

| Game pack | Game build | Rows |
|---|---|---|
| 0.1.0 | 4.10.196.36804 (`0x6ac64e6f`) | 70/70 |
| 0.2.0 | 4.10.196.36804 | 317 distinct rows, from two reports (316/316 and 311/311) |
| 0.3.0 | 4.10.196.36804 | 316/316 |
| 0.6.0 | 4.10.197.2878 (`0x6accbae3`) | 317/317 |
| 0.7.0 (not released) | 4.10.197.2878 | 332/332 |
| 0.8.1 | 4.10.200.58746 (`0x6af4861d`) | 347/347 |

For each build's patch name, changelist and the rows that moved, see [game builds](game/builds.md).

## What "verified" means

"Verified" here means the rows resolve in `sco-sigcheck`. `sco-sigcheck` runs the signature tables against the exe file, so a green row says the locator matches once and the byte checks pass. It does not say that a service built on the row works. Not run in game. See [game capability status](game/status.md) for what is on, what is off and why, per table and capability.

The SDK zip's `VERSIONS.txt` carries the string "rows verified by sigcheck; in-game verification pending" (`SCO_GAME_PACK_VERIFICATION` in `sc_game_pack.h`).

## See also

- [Exact-build profile selection](exact-build-profiles.md): the internal selector for exact-build profiles; not a plugin API.
