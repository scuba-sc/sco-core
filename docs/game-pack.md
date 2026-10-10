# The Star Citizen game pack

The game pack is the part of sco-core that knows Star Citizen: the signature rows in `src/game`, the game services published under the owner `game` ([game services](game-services.md)), the CryPak adapter and DataCore. It builds with `SCO_GAME_SC` (on by default; [architecture](architecture.md#kernel-and-game-pack)).

## Versions

The game pack has its own version line ([design decision 2](design/game-services.md)):

| Line | Where | Tags |
|---|---|---|
| Kernel, `sco_api` 1.x | `sdk/VERSION`, `SCO_API_MAJOR`/`MINOR` in `sco_api.h` | `sdk-vX.Y.Z` |
| Game pack | `SCO_GAME_PACK_VERSION` in [`sc_game_pack.h`](../include/sc_game_pack.h) | `game-sc-vX.Y` |
| Each game service table | its `*_VERSION` macro, passed to `query_service` | none: changes only with the table |

A game patch that moves rows gets a game-pack patch release; a release that adds or grows a game service raises the minor (0.1.0 carried `teleport.spatial` alone, 0.2.0 carries every service in [game services](game-services.md)). The SDK zip states all three in its `VERSIONS.txt`, generated from `sdk/VERSION`, `sco_api.h` and `include/sc_game_pack.h`; this page's list of builds is the long form.

## Verified game builds

Every row OK in `sco-sigcheck` on the build's `StarCitizen.exe`. The full reports are kept outside the repository (`sc-baseline/<build>/` on the maintainer's machine); nothing extracted from the game is committed.

| Game pack | Game build | Rows | Report |
|---|---|---|---|
| 0.1.0 | 4.10.196.36804 (PE timestamp `0x6ac64e6f`) | 70/70 OK | `sc-baseline/4.10.196.36804/sigcheck-game-pack-0.1.0.txt` |
| 0.2.0 | 4.10.196.36804 (PE timestamp `0x6ac64e6f`) | 317 distinct rows, all OK, from two reports (316/316 and 311/311; none covers the release commit alone) | `sigcheck-entities-merged.txt`, `sigcheck-mining.txt` |

**What "verified" means here: rows verified by sigcheck; in-game verification pending.** `sco-sigcheck` runs the signature tables against the exe file, so a green row says the locator matches once and the byte checks pass, not that the service built on it has been seen working. No game service has been run in a game session since it landed; [Status of every game.* table](game/status.md) lists, per table and capability, what is on, what is off and why. The same words are in the SDK zip's `VERSIONS.txt` (`SCO_GAME_PACK_VERIFICATION` in `sc_game_pack.h`).
