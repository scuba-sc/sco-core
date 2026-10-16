/* sc_game_pack.h: the Star Citizen game pack's version and the game builds it was verified on.
 * The game pack (sco-core's SCO_GAME_SC: signature rows, the game services under "game", DataCore)
 * has its own version line, separate from the kernel's sco_api 1.x and its sdk-vX.Y tags
 * (docs/design/game-services.md decision 2). Releases are tagged game-sc-vX.Y. A game patch that
 * moves rows gets a game-pack patch release; a game service's own table version changes only when
 * that table changes. docs/game-pack.md lists every verified build with its sigcheck report. */
#ifndef SC_GAME_PACK_H
#define SC_GAME_PACK_H

#define SCO_GAME_PACK_VERSION "0.6.0"
/* Numeric mirrors of the strings below; keep the pairs in step (game.build refuses to
 * start when they disagree). */
#define SCO_GAME_PACK_VERSION_MAJOR 0
#define SCO_GAME_PACK_VERSION_MINOR 6
#define SCO_GAME_PACK_VERSION_PATCH 0

/* The newest game build every row was checked on with sco-sigcheck (all rows OK). */
#define SCO_GAME_PACK_VERIFIED_BUILD "4.10.197.2878"
#define SCO_GAME_PACK_VERIFIED_BUILD_PARTS_0 4
#define SCO_GAME_PACK_VERIFIED_BUILD_PARTS_1 10
#define SCO_GAME_PACK_VERIFIED_BUILD_PARTS_2 197
#define SCO_GAME_PACK_VERIFIED_BUILD_PARTS_3 2878
/* That build's StarCitizen.exe PE timestamp (the file's identity: sco-sigcheck prints it). */
#define SCO_GAME_PACK_VERIFIED_PE_TIMESTAMP "0x6accbae3"
/* What "verified" means for this release. Rows are checked by sco-sigcheck against the exe file; the
 * services built on them have not been run in game yet (docs/game/status.md says per table). */
#define SCO_GAME_PACK_VERIFICATION "rows verified by sigcheck; in-game verification pending"

#endif
