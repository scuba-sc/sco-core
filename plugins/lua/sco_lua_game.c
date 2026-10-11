/* sco-lua's Star Citizen adapter: sco.datacore and the read-only sco.game.* tables, and the Lua
 * payloads of the game.* events. Built only with the game pack (SCO_GAME_SC); a host registers it
 * with sco_lua_add_extension(&sco_lua_game_extension) before scripts load (sco_lua_game.h).
 *
 * It talks to the game only through the services the game pack publishes (query_service), the same
 * tables a native plugin gets, and it keeps sco-lua's rules: every table is read-only (no function
 * that changes the game, except sco.datacore's patches, which the host checks), every call is
 * charged to the script's step budget, and a table appears only when its service is published.
 * Script reference: sdk/docs/lua.md (sco.datacore, sco.game). */
#include "sco_lua_game.h"
#include "sco_lua_ext.h"
#include "lauxlib.h"
#include "sc_actors.h"
#include "sc_creative.h"
#include "sc_entities.h"
#include "sc_game_events.h"
#include "sc_vehicles.h"
#include "sc_world.h"
#include "sco_datacore.h"
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* The payload of a game.* event (sc_game_events.h) as a table: nil for any other event, and for a
 * payload whose size is shorter than the struct this build knows. Ids are the game's 64-bit ids,
 * carried in Lua integers bit for bit (one above 2^63 reads as negative). */
static void PushGameEventTable(lua_State* L, const char* name, const void* data) {
    uint32_t size = 0;
    if (!data || strncmp(name, "game.", 5) != 0) { lua_pushnil(L); return; }
    memcpy(&size, data, sizeof(size));
    if (strcmp(name, SC_GAME_EVENT_PLAYER_SPAWNED) == 0 && size >= sizeof(sc_game_player_spawned)) {
        const sc_game_player_spawned* e = (const sc_game_player_spawned*)data;
        lua_createtable(L, 0, 2);
        lua_pushinteger(L, (lua_Integer)e->entity_id); lua_setfield(L, -2, "entity_id");
        lua_pushinteger(L, (lua_Integer)e->zone_id);   lua_setfield(L, -2, "zone_id");
    } else if (strcmp(name, SC_GAME_EVENT_PLAYER_DIED) == 0 && size >= sizeof(sc_game_player_died)) {
        const sc_game_player_died* e = (const sc_game_player_died*)data;
        lua_createtable(L, 0, 2);
        lua_pushinteger(L, (lua_Integer)e->entity_id); lua_setfield(L, -2, "entity_id");
        lua_pushinteger(L, (lua_Integer)e->killer_id); lua_setfield(L, -2, "killer_id");
    } else if (strcmp(name, SC_GAME_EVENT_ZONE_CHANGED) == 0 && size >= sizeof(sc_game_zone_changed)) {
        const sc_game_zone_changed* e = (const sc_game_zone_changed*)data;
        lua_createtable(L, 0, 2);
        lua_pushinteger(L, (lua_Integer)e->old_zone_id); lua_setfield(L, -2, "old_zone_id");
        lua_pushinteger(L, (lua_Integer)e->new_zone_id); lua_setfield(L, -2, "new_zone_id");
    } else {
        lua_pushnil(L);
    }
}

/* ---- sco.datacore (the host service sco_datacore.h) ----------------------------------------- */

#define DC_PATCH "sco-lua.dcpatch"
#define DC_INST  "sco-lua.dcinst"

typedef struct DcPatch { uint64_t id; } DcPatch;
typedef struct DcInst  { uint64_t patch, id; } DcInst;

/* The service as the host publishes it now; NULL when it doesn't (the product didn't enable it). */
static const sco_datacore_v1* DataCore(lua_State* L) {
    const sco_api* api = sco_lua_api(L);
    const void* t = NULL;
    if (!api || api->size <= offsetof(sco_api, query_service) || !api->query_service) return NULL;
    if (api->query_service(SCO_DATACORE_NAME, SCO_DATACORE_VERSION_1_0, &t) != SCO_OK) return NULL;
    return (const sco_datacore_v1*)t;
}

/* A Lua value as a sco_dc_value: nil, boolean, integer, number, string, an instance from
 * add_instance, { guid = "..." }, { enum = "..." } or { ref = "record" }. 0 when it is none of those. Strings stay
 * on the Lua stack for the call. */
static int ToDcValue(lua_State* L, int i, sco_dc_value* v) {
    memset(v, 0, sizeof(*v));
    v->size = sizeof(*v);
    switch (lua_type(L, i)) {
        case LUA_TNONE: case LUA_TNIL: v->type = SCO_DC_NULL; return 1;
        case LUA_TBOOLEAN: v->type = SCO_DC_BOOL; v->i = lua_toboolean(L, i); return 1;
        case LUA_TNUMBER:
            if (lua_isinteger(L, i)) { v->type = SCO_DC_INT; v->i = (int64_t)lua_tointeger(L, i); }
            else { v->type = SCO_DC_FLOAT; v->f = (double)lua_tonumber(L, i); }
            return 1;
        case LUA_TSTRING: v->type = SCO_DC_STRING; v->s = lua_tostring(L, i); return 1;
        case LUA_TUSERDATA: {
            const DcInst* inst = (const DcInst*)luaL_testudata(L, i, DC_INST);
            if (!inst) return 0;
            v->type = SCO_DC_INSTANCE;
            v->u = inst->id;
            return 1;
        }
        case LUA_TTABLE:   /* the string stays on the stack for the call */
            if (lua_getfield(L, i, "guid") == LUA_TSTRING) { v->type = SCO_DC_GUID; v->s = lua_tostring(L, -1); return 1; }
            if (lua_getfield(L, i, "enum") == LUA_TSTRING) { v->type = SCO_DC_ENUM; v->s = lua_tostring(L, -1); return 1; }
            if (lua_getfield(L, i, "ref") == LUA_TSTRING) { v->type = SCO_DC_REF; v->s = lua_tostring(L, -1); return 1; }
            return 0;
        default: return 0;
    }
}

static DcPatch* CheckPatch(lua_State* L) { return (DcPatch*)luaL_checkudata(L, 1, DC_PATCH); }

static int L_dc_state(lua_State* L) {
    const sco_datacore_v1* dc = DataCore(L);
    if (!dc) return sco_lua_push_result(L, SCO_UNAVAILABLE);
    lua_pushstring(L, dc->state() == SCO_DC_OPEN ? "open" : "loaded");
    return 1;
}

static int L_dc_begin(lua_State* L) {
    sco_plugin* self = sco_lua_self(L);
    uint32_t flags = 0;
    if (lua_istable(L, 1)) {
        lua_getfield(L, 1, "atomic");
        if (lua_type(L, -1) == LUA_TBOOLEAN && !lua_toboolean(L, -1)) flags |= SCO_DC_NON_ATOMIC;
        lua_pop(L, 1);
    }
    const sco_datacore_v1* dc = DataCore(L);
    if (!dc) { lua_pushnil(L); lua_pushstring(L, sco_lua_result_name(SCO_UNAVAILABLE)); return 2; }
    uint64_t id = 0;
    const sco_result r = dc->begin(self, flags, &id);
    sco_lua_check_budget(L);
    if (r != SCO_OK) { lua_pushnil(L); lua_pushstring(L, sco_lua_result_name(r)); return 2; }
    DcPatch* p = (DcPatch*)lua_newuserdatauv(L, sizeof(DcPatch), 0);
    p->id = id;
    luaL_setmetatable(L, DC_PATCH);
    return 1;
}

static int L_dc_set(lua_State* L) {
    DcPatch* p = CheckPatch(L);
    const char* record = luaL_checkstring(L, 2);
    const char* field = luaL_checkstring(L, 3);
    sco_dc_value v;
    const sco_datacore_v1* dc = DataCore(L);
    if (!dc) return sco_lua_push_result(L, SCO_UNAVAILABLE);
    if (!ToDcValue(L, 4, &v)) return sco_lua_push_result(L, SCO_BAD_ARG);
    const sco_result r = dc->set(p->id, record, field, v.type == SCO_DC_NULL ? NULL : &v);
    sco_lua_check_budget(L);
    return sco_lua_push_result(L, r);
}

static int L_dc_append(lua_State* L) {
    DcPatch* p = CheckPatch(L);
    const char* record = luaL_checkstring(L, 2);
    const char* field = luaL_checkstring(L, 3);
    sco_dc_value v;
    const sco_datacore_v1* dc = DataCore(L);
    if (!dc) return sco_lua_push_result(L, SCO_UNAVAILABLE);
    if (!ToDcValue(L, 4, &v)) return sco_lua_push_result(L, SCO_BAD_ARG);
    const sco_result r = dc->append(p->id, record, field, v.type == SCO_DC_NULL ? NULL : &v);
    sco_lua_check_budget(L);
    return sco_lua_push_result(L, r);
}

static int L_dc_add_instance(lua_State* L) {
    DcPatch* p = CheckPatch(L);
    const char* type = luaL_checkstring(L, 2);
    const char* cloneRecord = luaL_optstring(L, 3, NULL);
    const char* cloneField = luaL_optstring(L, 4, NULL);
    const sco_datacore_v1* dc = DataCore(L);
    if (!dc) { lua_pushnil(L); lua_pushstring(L, sco_lua_result_name(SCO_UNAVAILABLE)); return 2; }
    uint64_t id = 0;
    const sco_result r = dc->add_instance(p->id, type, cloneRecord, cloneField, &id);
    sco_lua_check_budget(L);
    if (r != SCO_OK) { lua_pushnil(L); lua_pushstring(L, sco_lua_result_name(r)); return 2; }
    DcInst* inst = (DcInst*)lua_newuserdatauv(L, sizeof(DcInst), 0);
    inst->patch = p->id;
    inst->id = id;
    luaL_setmetatable(L, DC_INST);
    return 1;
}

static int L_dc_set_pointer(lua_State* L) {
    DcPatch* p = CheckPatch(L);
    const char* record = luaL_checkstring(L, 2);
    const char* field = luaL_checkstring(L, 3);
    const DcInst* inst = (const DcInst*)luaL_checkudata(L, 4, DC_INST);
    const sco_datacore_v1* dc = DataCore(L);
    if (!dc) return sco_lua_push_result(L, SCO_UNAVAILABLE);
    const sco_result r = dc->set_pointer(p->id, record, field, inst->id);
    sco_lua_check_budget(L);
    return sco_lua_push_result(L, r);
}

/* p:add_record(type, name, clone_record [, guid [, file_path]]): the new record's root as an
 * instance (a pointer value, and with set/append targets by name), or nil, err. "unavailable" on
 * a sco.datacore 1.0 host. */
static int L_dc_add_record(lua_State* L) {
    DcPatch* p = CheckPatch(L);
    const char* type = luaL_checkstring(L, 2);
    const char* name = luaL_checkstring(L, 3);
    const char* clone = luaL_checkstring(L, 4);
    const char* guid = luaL_optstring(L, 5, NULL);
    const char* file = luaL_optstring(L, 6, NULL);
    const sco_datacore_v1* dc = DataCore(L);
    if (!dc) { lua_pushnil(L); lua_pushstring(L, sco_lua_result_name(SCO_UNAVAILABLE)); return 2; }
    uint64_t id = 0;
    const sco_result r = dc->add_record(p->id, type, name, guid, clone, file, &id);
    sco_lua_check_budget(L);
    if (r != SCO_OK) { lua_pushnil(L); lua_pushstring(L, sco_lua_result_name(r)); return 2; }
    DcInst* inst = (DcInst*)lua_newuserdatauv(L, sizeof(DcInst), 0);
    inst->patch = p->id;
    inst->id = id;
    luaL_setmetatable(L, DC_INST);
    return 1;
}

static int L_dc_commit(lua_State* L) {
    DcPatch* p = CheckPatch(L);
    const sco_datacore_v1* dc = DataCore(L);
    const sco_result r = dc ? dc->commit(p->id) : SCO_UNAVAILABLE;
    sco_lua_check_budget(L);
    return sco_lua_push_result(L, r);
}

static int L_dc_discard(lua_State* L) {
    DcPatch* p = CheckPatch(L);
    const sco_datacore_v1* dc = DataCore(L);
    const sco_result r = dc ? dc->discard(p->id) : SCO_UNAVAILABLE;
    sco_lua_check_budget(L);
    return sco_lua_push_result(L, r);
}

/* A list of { state = "queued" | "applied" | "skipped" | "refused", op = n, reason = "..." }: one per
 * operation (op 1, 2, ...), then one for the patch (op 0). */
static int L_dc_report(lua_State* L) {
    static const char* const states[] = { "?", "queued", "applied", "skipped", "refused" };
    DcPatch* p = CheckPatch(L);
    const sco_datacore_v1* dc = DataCore(L);
    if (!dc) { lua_pushnil(L); lua_pushstring(L, sco_lua_result_name(SCO_UNAVAILABLE)); return 2; }
    lua_newtable(L);
    for (uint32_t i = 0; i <= SCO_DC_MAX_OPS; ++i) {
        sco_dc_report rep;
        memset(&rep, 0, sizeof(rep));
        rep.size = sizeof(rep);
        const sco_result r = dc->report(p->id, i, &rep);
        if (r == SCO_NOT_FOUND && i == 0) { lua_pop(L, 1); lua_pushnil(L); lua_pushstring(L, sco_lua_result_name(r)); return 2; }
        if (r != SCO_OK) break;
        sco_lua_step(L, 1);
        lua_createtable(L, 0, 3);
        lua_pushstring(L, rep.state < 5 ? states[rep.state] : "?");
        lua_setfield(L, -2, "state");
        lua_pushinteger(L, rep.op_index == SCO_DC_OP_PATCH ? 0 : (lua_Integer)rep.op_index + 1);
        lua_setfield(L, -2, "op");
        lua_pushstring(L, rep.reason);
        lua_setfield(L, -2, "reason");
        lua_rawseti(L, -2, (lua_Integer)i + 1);
    }
    return 1;
}

/* The metatables of patch and instance userdata: methods only, locked, no __gc (an uncommitted
 * patch is dropped by the host when the plugin unloads). */
static void DataCoreTypes(lua_State* L) {
    static const luaL_Reg methods[] = {
        { "set", L_dc_set }, { "add_instance", L_dc_add_instance }, { "set_pointer", L_dc_set_pointer },
        { "append", L_dc_append }, { "add_record", L_dc_add_record }, { "commit", L_dc_commit },
        { "discard", L_dc_discard }, { "report", L_dc_report }, { NULL, NULL } };
    luaL_newmetatable(L, DC_PATCH);
    luaL_newlib(L, methods);
    lua_setfield(L, -2, "__index");
    lua_pushboolean(L, 0);
    lua_setfield(L, -2, "__metatable");
    lua_pop(L, 1);
    luaL_newmetatable(L, DC_INST);
    lua_pushboolean(L, 0);
    lua_setfield(L, -2, "__metatable");
    lua_pop(L, 1);
}

/* ---- sco.game.actors (the game pack's game.actors, read-only: no function that takes self) ----- */

static const sc_actors_v1* ActorsTable(lua_State* L) {
    const sco_api* api = sco_lua_api(L);
    const void* t = NULL;
    if (!api || api->size <= offsetof(sco_api, query_service) || !api->query_service) return NULL;
    if (api->query_service(SC_ACTORS_NAME, SC_ACTORS_VERSION_1_0, &t) != SCO_OK) return NULL;
    return (const sc_actors_v1*)t;
}

/* local_player(): actor_id, entity_id (the game's 64-bit ids as Lua integers), or nil, err and
 * the host's message. */
static int L_actors_local_player(lua_State* L) {
    const sc_actors_v1* a = ActorsTable(L);
    uint64_t actor = 0, entity = 0;
    char msg[256];
    uint32_t size = sizeof(msg);
    const sco_result r = a ? a->local_player(&actor, &entity) : SCO_UNAVAILABLE;
    sco_lua_check_budget(L);
    if (r == SCO_OK) {
        lua_pushinteger(L, (lua_Integer)(int64_t)actor);
        lua_pushinteger(L, (lua_Integer)(int64_t)entity);
        return 2;
    }
    lua_pushnil(L);
    lua_pushstring(L, sco_lua_result_name(r));
    if (a && a->last_error(NULL, msg, &size) == SCO_OK && msg[0]) { lua_pushstring(L, msg); return 3; }
    return 2;
}

/* ---- sco.game.entities (the game pack's game.entities, read-only: alive, class_of, get_transform) ---- */

static const sc_entities_v1* Entities(lua_State* L) {
    const sco_api* api = sco_lua_api(L);
    const void* t = NULL;
    if (!api || api->size <= offsetof(sco_api, query_service) || !api->query_service) return NULL;
    if (api->query_service(SC_ENTITIES_NAME, SC_ENTITIES_VERSION_1_0, &t) != SCO_OK) return NULL;
    return (const sc_entities_v1*)t;
}

/* nil, the result's name and the host's message (last_error without a plugin handle). */
static int EntFail(lua_State* L, const sc_entities_v1* e, sco_result r) {
    char msg[256];
    uint32_t size = sizeof(msg);
    lua_pushnil(L);
    lua_pushstring(L, sco_lua_result_name(r));
    if (e && e->last_error(NULL, msg, &size) == SCO_OK && msg[0]) { lua_pushstring(L, msg); return 3; }
    return 2;
}

/* alive(id): true when the entity is streamed in. */
static int L_ent_alive(lua_State* L) {
    const sc_entities_v1* e = Entities(L);
    const uint64_t id = (uint64_t)luaL_checkinteger(L, 1);
    const int alive = e ? e->alive(id) : 0;
    sco_lua_check_budget(L);
    lua_pushboolean(L, alive);
    return 1;
}

/* class_of(id): the entity's class name, or nil, err and the host's message. */
static int L_ent_class_of(lua_State* L) {
    const sc_entities_v1* e = Entities(L);
    const uint64_t id = (uint64_t)luaL_checkinteger(L, 1);
    char name[256];
    uint32_t size = sizeof(name);
    const sco_result r = e ? e->class_of(id, name, &size) : SCO_UNAVAILABLE;
    sco_lua_check_budget(L);
    if (r != SCO_OK) return EntFail(L, e, r);
    lua_pushstring(L, name);
    return 1;
}

/* get_transform(id): x, y, z, qx, qy, qz, qw, zone_id (the entity's zone's local frame), or nil, err
 * and the host's message. */
static int L_ent_get_transform(lua_State* L) {
    const sc_entities_v1* e = Entities(L);
    const uint64_t id = (uint64_t)luaL_checkinteger(L, 1);
    double pos[3] = { 0, 0, 0 }, rot[4] = { 0, 0, 0, 0 };
    uint64_t zone = 0;
    const sco_result r = e ? e->get_transform(id, pos, rot, &zone) : SCO_UNAVAILABLE;
    sco_lua_check_budget(L);
    if (r != SCO_OK) return EntFail(L, e, r);
    for (int i = 0; i < 3; ++i) lua_pushnumber(L, pos[i]);
    for (int i = 0; i < 4; ++i) lua_pushnumber(L, rot[i]);
    lua_pushinteger(L, (lua_Integer)(int64_t)zone);
    return 8;
}

/* health(entity_id): the actor's health pool and its maximum (0: not read yet), or nil, err and the
 * host's message. Needs game.actors 1.1. */
static int L_actors_health(lua_State* L) {
    const sc_actors_v1* a = ActorsTable(L);
    const uint64_t id = (uint64_t)luaL_checkinteger(L, 1);
    float cur = 0, max = 0;
    char msg[256];
    uint32_t size = sizeof(msg);
    const sco_result r = a && a->size > offsetof(sc_actors_v1, health) ? a->health(id, &cur, &max) : SCO_UNAVAILABLE;
    if (r == SCO_OK) {
        lua_pushnumber(L, (lua_Number)cur);
        lua_pushnumber(L, (lua_Number)max);
        return 2;
    }
    lua_pushnil(L);
    lua_pushstring(L, sco_lua_result_name(r));
    if (a && a->last_error(NULL, msg, &size) == SCO_OK && msg[0]) { lua_pushstring(L, msg); return 3; }
    return 2;
}

/* ---- sco.game.vehicles: the game pack's game.vehicles, read-only (no seat, eject or power_on) ---- */

static const sc_vehicles_v1* Vehicles(lua_State* L) {
    const sco_api* api = sco_lua_api(L);
    const void* t = NULL;
    if (!api || api->size <= offsetof(sco_api, query_service) || !api->query_service) return NULL;
    if (api->query_service(SC_VEHICLES_SERVICE_NAME, SC_VEHICLES_SERVICE_VERSION, &t) != SCO_OK) return NULL;
    return (const sc_vehicles_v1*)t;
}

/* nil, the result's name and the game pack's reason (when it left one). */
static int VehFail(lua_State* L, sco_plugin* self, const sc_vehicles_v1* v, sco_result r) {
    char msg[256];
    uint32_t size = sizeof(msg);
    lua_pushnil(L);
    lua_pushstring(L, sco_lua_result_name(r));
    if (!v) { lua_pushliteral(L, "game.vehicles is not available on this host"); return 3; }
    if (v->last_error(self, msg, &size) == SCO_OK && msg[0]) { lua_pushstring(L, msg); return 3; }
    return 2;
}

/* Ids are the game's 64-bit entity ids, carried in a Lua integer bit for bit (one above 2^63 reads
 * as negative); pass them back unchanged. */
static uint64_t VehId(lua_State* L, int i) { return (uint64_t)luaL_checkinteger(L, i); }

/* player_ship(): the id of the ship you're aboard, or nil, err, message. */
static int L_veh_player_ship(lua_State* L) {
    sco_plugin* self = sco_lua_self(L);
    const sc_vehicles_v1* v = Vehicles(L);
    uint64_t ship = 0;
    const sco_result r = v ? v->player_ship(&ship) : SCO_UNAVAILABLE;
    if (r != SCO_OK) return VehFail(L, self, v, r);
    lua_pushinteger(L, (lua_Integer)ship);
    return 1;
}

static sc_vehicle_seat g_vehSeats[256];   /* game thread only, like every call into game.vehicles */

/* seats(ship): a list of { index, seat_id, occupant_id, priority, name, usable, usable_known,
 * occupied, pilot } (index is the game pack's, from 0) and whether the ship has more than were
 * listed; or nil, err, message. */
static int L_veh_seats(lua_State* L) {
    sco_plugin* self = sco_lua_self(L);
    const uint64_t ship = VehId(L, 1);
    const sc_vehicles_v1* v = Vehicles(L);
    uint32_t n = 0, more = 0;
    const sco_result r = v ? v->seats(ship, g_vehSeats, (uint32_t)(sizeof(g_vehSeats) / sizeof(g_vehSeats[0])), &n, &more)
                           : SCO_UNAVAILABLE;
    if (r != SCO_OK) return VehFail(L, self, v, r);
    lua_createtable(L, (int)n, 0);
    for (uint32_t i = 0; i < n; ++i) {
        const sc_vehicle_seat* e = &g_vehSeats[i];
        lua_createtable(L, 0, 9);
        lua_pushinteger(L, (lua_Integer)e->index);       lua_setfield(L, -2, "index");
        lua_pushinteger(L, (lua_Integer)e->seat_id);     lua_setfield(L, -2, "seat_id");
        lua_pushinteger(L, (lua_Integer)e->occupant_id); lua_setfield(L, -2, "occupant_id");
        lua_pushinteger(L, (lua_Integer)e->priority);    lua_setfield(L, -2, "priority");
        const char* end = (const char*)memchr(e->name, 0, sizeof(e->name));
        lua_pushlstring(L, e->name, end ? (size_t)(end - e->name) : sizeof(e->name)); lua_setfield(L, -2, "name");
        lua_pushboolean(L, (e->flags & SC_SEAT_USABLE) != 0);       lua_setfield(L, -2, "usable");
        lua_pushboolean(L, (e->flags & SC_SEAT_USABLE_KNOWN) != 0); lua_setfield(L, -2, "usable_known");
        lua_pushboolean(L, (e->flags & SC_SEAT_OCCUPIED) != 0);     lua_setfield(L, -2, "occupied");
        lua_pushboolean(L, (e->flags & SC_SEAT_PILOT) != 0);        lua_setfield(L, -2, "pilot");
        lua_rawseti(L, -2, (lua_Integer)i + 1);
    }
    lua_pushboolean(L, more != 0);
    return 2;
}

/* seat_occupant(ship, index): the actor's id, 0 when the seat is empty; or nil, err, message. */
static int L_veh_seat_occupant(lua_State* L) {
    sco_plugin* self = sco_lua_self(L);
    const uint64_t ship = VehId(L, 1);
    const lua_Integer index = luaL_checkinteger(L, 2);
    const sc_vehicles_v1* v = Vehicles(L);
    if (index < 0 || index > (lua_Integer)UINT32_MAX) {
        lua_pushnil(L);
        lua_pushliteral(L, "bad_arg");
        lua_pushliteral(L, "index is a seat's index from seats()");
        return 3;
    }
    uint64_t actor = 0;
    const sco_result r = v ? v->seat_occupant(ship, (uint32_t)index, &actor) : SCO_UNAVAILABLE;
    if (r != SCO_OK) return VehFail(L, self, v, r);
    lua_pushinteger(L, (lua_Integer)actor);
    return 1;
}


/* ---- sco.game.creative: the game pack's game.creative, read-only (only state(); no toggles) -------- */

static const sc_creative_v1* Creative(lua_State* L) {
    const sco_api* api = sco_lua_api(L);
    const void* t = NULL;
    if (!api || api->size <= offsetof(sco_api, query_service) || !api->query_service) return NULL;
    if (api->query_service(SC_CREATIVE_SERVICE_NAME, SC_CREATIVE_SERVICE_VERSION, &t) != SCO_OK) return NULL;
    return (const sc_creative_v1*)t;
}

/* state(): { god_mode, fly, infinite_ammo, infinite_ship_ammo } (booleans: which toggles are on,
 * whoever switched them on), or nil, err, message. */
static int L_creative_state(lua_State* L) {
    sco_plugin* self = sco_lua_self(L);
    const sc_creative_v1* c = Creative(L);
    uint32_t flags = 0;
    const sco_result r = c ? c->state(self, &flags) : SCO_UNAVAILABLE;
    if (r != SCO_OK) {
        char msg[256];
        uint32_t size = sizeof(msg);
        lua_pushnil(L);
        lua_pushstring(L, sco_lua_result_name(r));
        if (!c) { lua_pushliteral(L, "game.creative is not available on this host"); return 3; }
        if (c->last_error(self, msg, &size) == SCO_OK && msg[0]) { lua_pushstring(L, msg); return 3; }
        return 2;
    }
    lua_createtable(L, 0, 4);
    lua_pushboolean(L, (flags & SC_CREATIVE_GOD_MODE) != 0);          lua_setfield(L, -2, "god_mode");
    lua_pushboolean(L, (flags & SC_CREATIVE_FLY) != 0);               lua_setfield(L, -2, "fly");
    lua_pushboolean(L, (flags & SC_CREATIVE_INFINITE_AMMO) != 0);     lua_setfield(L, -2, "infinite_ammo");
    lua_pushboolean(L, (flags & SC_CREATIVE_INFINITE_SHIP_AMMO) != 0); lua_setfield(L, -2, "infinite_ship_ammo");
    return 1;
}

/* ---- sco.game.world: the game pack's game.world, read-only queries (no self) ---------------------- */

static const sc_world_v1* WorldTable(lua_State* L) {
    const sco_api* api = sco_lua_api(L);
    const void* t = NULL;
    if (!api || api->size <= offsetof(sco_api, query_service) || !api->query_service) return NULL;
    if (api->query_service(SC_WORLD_NAME, SC_WORLD_VERSION_1_0, &t) != SCO_OK) return NULL;
    return (const sc_world_v1*)t;
}

/* nil, the result's name and the game pack's reason (when it left one). */
static int WorldFail(lua_State* L, const sc_world_v1* w, sco_result r) {
    char msg[256];
    uint32_t size = sizeof(msg);
    lua_pushnil(L);
    lua_pushstring(L, sco_lua_result_name(r));
    if (!w) { lua_pushliteral(L, "game.world is not available on this host"); return 3; }
    if (w->last_error(NULL, msg, &size) == SCO_OK && msg[0]) { lua_pushstring(L, msg); return 3; }
    return 2;
}

/* camera(): { x, y, z, qx, qy, qz, qw, zone }: the camera's position (metres) and rotation in the
 * world frame, and the zone your player is in; or nil, err, message. No field of view. */
static int L_world_camera(lua_State* L) {
    const sc_world_v1* w = WorldTable(L);
    double pos[3] = { 0, 0, 0 }, rot[4] = { 0, 0, 0, 0 };
    uint64_t zone = 0;
    const sco_result r = w ? w->camera(pos, rot, &zone) : SCO_UNAVAILABLE;
    if (r != SCO_OK) return WorldFail(L, w, r);
    lua_createtable(L, 0, 8);
    lua_pushnumber(L, pos[0]); lua_setfield(L, -2, "x");
    lua_pushnumber(L, pos[1]); lua_setfield(L, -2, "y");
    lua_pushnumber(L, pos[2]); lua_setfield(L, -2, "z");
    lua_pushnumber(L, rot[0]); lua_setfield(L, -2, "qx");
    lua_pushnumber(L, rot[1]); lua_setfield(L, -2, "qy");
    lua_pushnumber(L, rot[2]); lua_setfield(L, -2, "qz");
    lua_pushnumber(L, rot[3]); lua_setfield(L, -2, "qw");
    lua_pushinteger(L, (lua_Integer)zone); lua_setfield(L, -2, "zone");
    return 1;
}

/* raycast(zone, fx, fy, fz, dx, dy, dz, max_dist): { x, y, z, distance } where the ray hit, in zone's
 * local frame; or nil, err, message ("not_found" when nothing was hit). */
static int L_world_raycast(lua_State* L) {
    const sc_world_v1* w = WorldTable(L);
    const uint64_t zone = (uint64_t)luaL_checkinteger(L, 1);
    double from[3], dir[3];
    for (int i = 0; i < 3; ++i) from[i] = (double)luaL_checknumber(L, 2 + i);
    for (int i = 0; i < 3; ++i) dir[i] = (double)luaL_checknumber(L, 5 + i);
    const double max = (double)luaL_checknumber(L, 8);
    sc_world_hit hit;
    memset(&hit, 0, sizeof(hit));
    hit.size = sizeof(hit);
    const sco_result r = w ? w->raycast(zone, from, dir, max, &hit) : SCO_UNAVAILABLE;
    if (r != SCO_OK) return WorldFail(L, w, r);
    lua_createtable(L, 0, 4);
    lua_pushnumber(L, hit.pos[0]); lua_setfield(L, -2, "x");
    lua_pushnumber(L, hit.pos[1]); lua_setfield(L, -2, "y");
    lua_pushnumber(L, hit.pos[2]); lua_setfield(L, -2, "z");
    lua_pushnumber(L, hit.distance); lua_setfield(L, -2, "distance");
    return 1;
}

/* ---- the extension ---------------------------------------------------------------------------- */

static void Open(lua_State* L) {
    DataCoreTypes(L);
    if (DataCore(L)) {                                /* sco.datacore: only when the host publishes it */
        static const luaL_Reg dcfns[] = { { "begin", L_dc_begin }, { "state", L_dc_state }, { NULL, NULL } };
        luaL_newlib(L, dcfns);
        lua_setfield(L, -2, "datacore");
    }
    /* sco.game: the game pack's services, read-only, each only when published */
    const int actors = ActorsTable(L) != NULL, vehicles = Vehicles(L) != NULL, world = WorldTable(L) != NULL;
    const int entities = Entities(L) != NULL, creative = Creative(L) != NULL;
    if (!(actors || vehicles || world || entities || creative)) return;
    lua_newtable(L);
    if (actors) {                                     /* sco.game.actors */
        static const luaL_Reg gafns[] = { { "local_player", L_actors_local_player }, { "health", L_actors_health }, { NULL, NULL } };
        luaL_newlib(L, gafns);
        lua_setfield(L, -2, "actors");
    }
    if (vehicles) {                                   /* sco.game.vehicles */
        static const luaL_Reg vehfns[] = {
            { "player_ship", L_veh_player_ship }, { "seats", L_veh_seats },
            { "seat_occupant", L_veh_seat_occupant }, { NULL, NULL } };
        luaL_newlib(L, vehfns);
        lua_setfield(L, -2, "vehicles");
    }
    if (creative) {                                   /* sco.game.creative */
        static const luaL_Reg crfns[] = { { "state", L_creative_state }, { NULL, NULL } };
        luaL_newlib(L, crfns);
        lua_setfield(L, -2, "creative");
    }
    if (world) {                                      /* sco.game.world */
        static const luaL_Reg wfns[] = { { "camera", L_world_camera }, { "raycast", L_world_raycast }, { NULL, NULL } };
        luaL_newlib(L, wfns);
        lua_setfield(L, -2, "world");
    }
    if (entities) {                                   /* sco.game.entities */
        static const luaL_Reg entfns[] = {
            { "alive", L_ent_alive }, { "class_of", L_ent_class_of }, { "get_transform", L_ent_get_transform },
            { NULL, NULL } };
        luaL_newlib(L, entfns);
        lua_setfield(L, -2, "entities");
    }
    lua_setfield(L, -2, "game");
}

/* game.* events get a table (or nil for a payload shorter than this build's struct); anything else
 * is not this extension's. */
static int PushEvent(lua_State* L, const char* event, const void* data) {
    if (strncmp(event, "game.", 5) != 0) return 0;
    PushGameEventTable(L, event, data);
    return 1;
}

const sco_lua_extension sco_lua_game_extension = { "game-sc", Open, PushEvent };
