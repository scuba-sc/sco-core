/* sco-lua's private extension interface: how an optional adapter adds tables to a script's `sco`
 * table without sco_lua.c knowing their headers. Not part of the plugin SDK; nothing here crosses
 * sco_api.h, whose layout is unchanged.
 *
 * sco_lua.c stays game-neutral: it includes only sco_api.h, the generic host services
 * (sco_storage.h, sco_settings.h, sco_ui.h) and Lua. A host that wants more registers an adapter
 * once, before any script loads:
 *
 *   #include "sco_lua_game.h"                      // the Star Citizen adapter (SCO_GAME_SC builds)
 *   sco_lua_add_extension(&sco_lua_game_extension);
 *
 * Every adapter function runs on the game thread, inside the script's protected call and under its
 * step budget, exactly like sco_lua.c's own functions. */
#ifndef SCO_LUA_EXT_H
#define SCO_LUA_EXT_H

#include "sco_api.h"
#ifdef __cplusplus
#include "lua.hpp"   /* the C API with C linkage */
#else
#include "lua.h"
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define SCO_LUA_MAX_EXTENSIONS 8

typedef struct sco_lua_extension {
    const char* name;   /* for messages: "game-sc" */
    /* Runs once per script while sco_lua_load builds the `sco` table (on top of the stack). Adds
     * fields to it and metatables to the registry; leaves the stack as it found it. */
    void (*open)(lua_State* L);
    /* The Lua value of an event's data, for an event sco_lua.c doesn't know: push one value (nil
     * when this extension doesn't know the event either) and return 1, or return 0 to leave the
     * event to the next extension. */
    int (*push_event)(lua_State* L, const char* event, const void* data);
} sco_lua_extension;

/* Registers ext for every script loaded from now on (scripts already loaded keep their tables).
 * SCO_OK; SCO_BAD_ARG for NULL or an extension already registered; SCO_TOO_MANY past
 * SCO_LUA_MAX_EXTENSIONS. Game thread, before scripts load. */
sco_result sco_lua_add_extension(const sco_lua_extension* ext);

/* ---- what an extension may call (from inside its functions, on the script's own lua_State) ---- */

/* The plugin API and handle of the script that owns L. */
const sco_api* sco_lua_api(lua_State* L);
sco_plugin* sco_lua_self(lua_State* L);

/* Back from the host: if the shared step budget ran out meanwhile, raise like the VM would. */
void sco_lua_check_budget(lua_State* L);

/* "ok", "unavailable", "not_found", "bad_arg", "crashed", "wrong_thread", "too_many", "failed". */
const char* sco_lua_result_name(sco_result r);

/* sco-lua's convention for a call that returns no value: true, or false and the result's name.
 * Returns the number of values pushed. */
int sco_lua_push_result(lua_State* L, sco_result r);

#ifdef __cplusplus
}
#endif

#endif /* SCO_LUA_EXT_H */
