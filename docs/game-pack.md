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
| 0.3.0 | 4.10.196.36804 (PE timestamp `0x6ac64e6f`) | 316 rows, all OK (316/316; the redundant `contracts.actor_kill` row was removed with the actor-death change) | the actor-death change's post-removal run, in `sc-baseline/4.10.196.36804/` |
| 0.6.0 (pin bump, no release re-cut) | 4.10.197.2878 (PE timestamp `0x6accbae3`) — LIVE hotfix over 4.10.196, P4 CL 12913470 | 317/317 OK: every row of the game pack at this branch's base scans the hotfixed client unchanged (no row moves; includes `teleport.relocation` from #163; a 316/316 run on the pre-#163 table was recorded the same day — both all-OK) | native `sco-sigcheck` run on the hotfixed exe, 2026-10-15, report kept outside the repository (`sc-baseline/4.10.197.2878/` on the maintainer's machine) |
| 0.7.0 (candidate, unpublished) | 4.10.197.2878 (PE timestamp `0x6accbae3`) | 323/323 OK: a static run of the full table on the main commit that precedes the release preparation; no signature row, include or source file changed between that run and the candidate commit (the candidate edits version strings, documentation and generated pins only). The run reads the exe file on disk; the game was not started. | native `sco-sigcheck` run, 2026-10-16, report kept outside the repository |

**What "verified" means here: rows verified by sigcheck; in-game verification pending.** `sco-sigcheck` runs the signature tables against the exe file, so a green row says the locator matches once and the byte checks pass, not that the service built on it has been seen working. No game service has been run in a game session since it landed; [Status of every game.* table](game/status.md) lists, per table and capability, what is on, what is off and why. The same words are in the SDK zip's `VERSIONS.txt` (`SCO_GAME_PACK_VERIFICATION` in `sc_game_pack.h`).
