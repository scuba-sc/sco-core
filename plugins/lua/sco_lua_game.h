/* sco-lua's Star Citizen adapter (plugins/lua/sco_lua_game.c): sco.datacore, the read-only
 * sco.game.* tables and the game.* event payloads. Built with the game pack only (SCO_GAME_SC).
 *
 *   sco_lua_add_extension(&sco_lua_game_extension);   // once, before scripts load
 */
#ifndef SCO_LUA_GAME_H
#define SCO_LUA_GAME_H

#include "sco_lua_ext.h"

#ifdef __cplusplus
extern "C" {
#endif

extern const sco_lua_extension sco_lua_game_extension;

#ifdef __cplusplus
}
#endif

#endif /* SCO_LUA_GAME_H */
