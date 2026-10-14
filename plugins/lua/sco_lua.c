/* sco-lua: runs Lua plugins in a sandbox, through the plain-C plugin API only. See sco_lua.h.
 *
 * Every entry from the host (load, an event, a command, a task) runs in protected mode, so a
 * Lua error or an allocation failure never escapes into the game. Each entry has a step budget:
 * the VM's count hook charges HOOK_EVERY steps per HOOK_EVERY instructions, and the patched
 * string/table library loops charge one step per iteration (SCO_LUA_STEP, sco_lua_user.h).
 * The budget belongs to the outermost entry: a script invoking another script's command spends
 * from the same budget.
 * Past the budget every further instruction raises, so a script's own pcall can't hold on, and
 * the script is disabled. Finalizers (__gc) are refused: Lua runs them with hooks off. */
#include "sco_lua.h"
#include "sco_lua_user.h"
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
#include "sco_lua_ext.h"
#include "sco_settings.h"
#include "sco_storage.h"
#include "sco_ui.h"
#include "sco_widgets.h"
#include "sco_panel_builder.h"
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define HOOK_EVERY  1000
#define MAX_ERRORS  3    /* errors in callbacks before the script is disabled */
#define MAX_DEPTH   8    /* nested host -> script entries (a command invoking a command ...) */
#define MAX_ARGS    16
#define NAME_MAX_   64
#define REPLY_SIZE  256
#define SUBS_KEY    "sco-lua.subs"

typedef struct Script Script;

typedef struct EventSub { Script* s; int live; char name[NAME_MAX_]; } EventSub;
typedef struct CmdCtx   { Script* s; int ref; char name[NAME_MAX_]; } CmdCtx;
typedef struct Task     { Script* s; int ref; struct Task* prev; struct Task* next; } Task;
typedef struct WidgetCtx { Script* s; int ref; char id[SCO_WIDGETS_MAX_ID + 1]; } WidgetCtx;

struct Script {
    int used, alive;
    sco_plugin* self;
    const sco_api* api;
    lua_State* L;
    size_t mem;
    int over;      /* past the budget in the current entry */
    int depth;     /* entries of this script on the C stack */
    int errors;
    char chunk[NAME_MAX_];
    EventSub events[SCO_LUA_MAX_EVENTS];
    CmdCtx cmds[SCO_LUA_MAX_COMMANDS];
    int ncmds;
    Task* tasks;   /* queued run_on_game_thread calls */
    int ntasks;    /* how many: each runs with a fresh budget, so they are capped */
    WidgetCtx widgets[SCO_WIDGETS_MAX_PANELS]; /* stable contexts; registry roots owned functions */
};

void sco_lua_event_(const char* event, const void* data, void* ctx);   /* every Lua subscription */

static Script g_scripts[SCO_LUA_MAX_SCRIPTS];
static int g_depth;        /* entries of any script on the C stack */
static uint64_t g_steps;   /* steps since the outermost entry: one budget for every script it
                            * reaches, so a script can't multiply it by invoking another's commands */
static int g_over;         /* that budget ran out: every script still on the stack stops */
static size_t g_mem;       /* bytes held by every script together: SCO_LUA_TOTAL_MEMORY_LIMIT */

static Script* Of(lua_State* L) { return *(Script**)lua_getextraspace(L); }

static Script* Find(const sco_plugin* self) {
    for (int i = 0; i < SCO_LUA_MAX_SCRIPTS; ++i)
        if (g_scripts[i].used && g_scripts[i].self == self) return &g_scripts[i];
    return NULL;
}

static void Copy(char* dst, size_t n, const char* src) {
    if (!n) return;
    snprintf(dst, n, "%s", src ? src : "");
}

/* The error object on top of the stack as text, without allocating: outside protected mode a
 * failed allocation (lua_tostring converting a number at the memory cap) would abort. */
static const char* ErrorText(lua_State* L, char* buf, size_t n) {
    switch (lua_type(L, -1)) {
        case LUA_TSTRING: return lua_tostring(L, -1);   /* already a string: no conversion */
        case LUA_TNUMBER:
            if (lua_isinteger(L, -1)) snprintf(buf, n, "%lld", (long long)lua_tointeger(L, -1));
            else snprintf(buf, n, "%.14g", (double)lua_tonumber(L, -1));
            return buf;
        default: return "error object is not a string";
    }
}

static const char* ResultName(sco_result r) {
    switch (r) {
        case SCO_OK:           return "ok";
        case SCO_UNAVAILABLE:  return "unavailable";
        case SCO_NOT_FOUND:    return "not_found";
        case SCO_BAD_ARG:      return "bad_arg";
        case SCO_CRASHED:      return "crashed";
        case SCO_WRONG_THREAD: return "wrong_thread";
        case SCO_TOO_MANY:     return "too_many";
        case SCO_FAILED:       return "failed";
        default:               return "error";
    }
}

static const char* const kTypeNames[] = { "int", "float", "string", "bool" };

/* ---- extensions (sco_lua_ext.h) ------------------------------------------------------------- */

static const sco_lua_extension* g_extensions[SCO_LUA_MAX_EXTENSIONS];
static int g_nextensions;

sco_result sco_lua_add_extension(const sco_lua_extension* ext) {
    if (!ext || !ext->open) return SCO_BAD_ARG;
    for (int i = 0; i < g_nextensions; ++i) if (g_extensions[i] == ext) return SCO_BAD_ARG;
    if (g_nextensions >= SCO_LUA_MAX_EXTENSIONS) return SCO_TOO_MANY;
    g_extensions[g_nextensions++] = ext;
    return SCO_OK;
}

const sco_api* sco_lua_api(lua_State* L) { const Script* s = Of(L); return s ? s->api : NULL; }
sco_plugin* sco_lua_self(lua_State* L) { const Script* s = Of(L); return s ? s->self : NULL; }
const char* sco_lua_result_name(sco_result r) { return ResultName(r); }

/* The Lua value of an event's data: the first extension that knows the event pushes it; nil when
 * none does. */
static void PushEventData(lua_State* L, const char* name, const void* data) {
    for (int i = 0; i < g_nextensions; ++i)
        if (g_extensions[i]->push_event && g_extensions[i]->push_event(L, name, data)) return;
    lua_pushnil(L);
}

/* ---- memory and steps ---------------------------------------------------------------------- */

static void* Alloc(void* ud, void* ptr, size_t osize, size_t nsize) {
    Script* s = (Script*)ud;
    const size_t old = ptr ? osize : 0;
    if (nsize == 0) {
        free(ptr);
        s->mem -= old;
        g_mem -= old;
        return NULL;
    }
    if (nsize > old && (s->mem - old + nsize > SCO_LUA_MEMORY_LIMIT ||
                        g_mem - old + nsize > SCO_LUA_TOTAL_MEMORY_LIMIT)) return NULL;
    void* p = realloc(ptr, nsize);
    if (!p) return NULL;
    s->mem = s->mem - old + nsize;
    g_mem = g_mem - old + nsize;
    return p;
}

static void Hook(lua_State* L, lua_Debug* ar);

void sco_lua_step(lua_State* L, int n) {
    Script* s = Of(L);
    if (!s || !s->depth) return;
    g_steps += (uint64_t)n;
    if (!g_over && g_steps <= SCO_LUA_STEP_BUDGET) return;
    g_over = 1;
    if (!s->over) {
        s->over = 1;
        lua_sethook(L, Hook, LUA_MASKCOUNT, 1);   /* from now on every instruction raises */
    }
    luaL_error(L, "ran past its step budget");
}

static void Hook(lua_State* L, lua_Debug* ar) {
    (void)ar;
    sco_lua_step(L, HOOK_EVERY);
}

/* ---- entering the script -------------------------------------------------------------------- */

/* Calls body(ud) in protected mode under the budget. LUA_OK, or an error status with the message
 * on top of the stack. Allocates nothing before the protected call. */
static int Enter(Script* s, lua_CFunction body, void* ud) {
    lua_State* L = s->L;
    if (g_depth == 0) { g_steps = 0; g_over = 0; }
    if (s->depth == 0) {
        s->over = 0;
        lua_sethook(L, Hook, LUA_MASKCOUNT, HOOK_EVERY);
    }
    ++s->depth;
    ++g_depth;
    lua_pushcfunction(L, body);
    lua_pushlightuserdata(L, ud);
    const int st = lua_pcall(L, 1, 0, 0);
    --g_depth;
    --s->depth;
    return st;
}

/* Back in script code after calling into the host (sco.invoke can run other scripts): if the
 * shared budget ran out meanwhile, this script stops too. */
static void CheckBudget(lua_State* L) { sco_lua_step(L, 0); }
void sco_lua_check_budget(lua_State* L) { CheckBudget(L); }

static void Disable(Script* s, const char* why) {
    if (!s->alive) return;
    s->alive = 0;
    for (int i = 0; i < SCO_LUA_MAX_EVENTS; ++i) {
        EventSub* e = &s->events[i];
        if (e->live) {
            s->api->unsubscribe(s->self, e->name, sco_lua_event_);
            e->live = 0;
        }
    }
    char line[160];
    snprintf(line, sizeof(line), "script disabled: %s", why);
    s->api->log(s->self, SCO_LOG_ERROR, line);
    s->api->status(s->self, line);
}

/* Logs the error on top of the stack and pops it; disables the script on a budget overrun, a
 * memory error or the MAX_ERRORS-th error. */
static void Failed(Script* s, const char* where, int st) {
    char num[48];
    const char* msg = ErrorText(s->L, num, sizeof(num));
    char line[320];
    snprintf(line, sizeof(line), "%s: %s", where, msg);
    s->api->log(s->self, SCO_LOG_ERROR, line);
    lua_pop(s->L, 1);
    if (s->over) Disable(s, "ran past its step budget");
    else if (st == LUA_ERRMEM) Disable(s, "out of memory");
    else if (++s->errors >= MAX_ERRORS) Disable(s, "3 errors");
}

/* ---- events --------------------------------------------------------------------------------- */

typedef struct EventCall { EventSub* e; const void* data; } EventCall;

/* True when the function at index fn is still in subs[name] (index list). */
static int StillSubscribed(lua_State* L, int list, int fn) {
    const lua_Integer n = (lua_Integer)lua_rawlen(L, list);
    for (lua_Integer i = 1; i <= n; ++i) {
        lua_rawgeti(L, list, i);
        const int same = lua_rawequal(L, -1, fn);
        lua_pop(L, 1);
        if (same) return 1;
    }
    return 0;
}

static int EventBody(lua_State* L) {
    EventCall* k = (EventCall*)lua_touserdata(L, 1);
    Script* s = k->e->s;
    lua_getfield(L, LUA_REGISTRYINDEX, SUBS_KEY);
    lua_getfield(L, -1, k->e->name);
    if (!lua_istable(L, -1)) return 0;
    const int live = lua_gettop(L);
    const lua_Integer n = (lua_Integer)lua_rawlen(L, live);
    lua_createtable(L, (int)n, 0);                /* snapshot: subscribe applies from the next one */
    const int snap = lua_gettop(L);
    for (lua_Integer i = 1; i <= n; ++i) { lua_rawgeti(L, live, i); lua_rawseti(L, snap, i); }
    for (lua_Integer i = 1; i <= n && s->alive; ++i) {
        lua_rawgeti(L, snap, i);
        const int fn = lua_gettop(L);
        if (!StillSubscribed(L, live, fn)) { lua_pop(L, 1); continue; }   /* unsubscribe applies at once */
        lua_pushstring(L, k->e->name);
        if (strcmp(k->e->name, "tick") == 0 && k->data) lua_pushinteger(L, *(const uint32_t*)k->data);
        else PushEventData(L, k->e->name, k->data);   /* an extension's table for its events, else nil */
        const int st = lua_pcall(L, 2, 0, 0);
        if (st != LUA_OK) {
            char where[NAME_MAX_ + 16];
            snprintf(where, sizeof(where), "event %s", k->e->name);
            Failed(s, where, st);
        }
        lua_settop(L, snap);
    }
    return 0;
}

void sco_lua_event_(const char* event, const void* data, void* ctx) {
    (void)event;
    EventSub* e = (EventSub*)ctx;
    Script* s = e->s;
    if (!s->alive || !s->L || g_depth >= MAX_DEPTH || !lua_checkstack(s->L, 4)) return;
    const int top = lua_gettop(s->L);
    EventCall k = { e, data };
    const int st = Enter(s, EventBody, &k);
    if (st != LUA_OK) {
        char where[NAME_MAX_ + 16];
        snprintf(where, sizeof(where), "event %s", e->name);
        Failed(s, where, st);
    }
    lua_settop(s->L, top);
}

/* ---- commands ------------------------------------------------------------------------------- */

typedef struct CmdCall {
    CmdCtx* c;
    const sco_arg* args;
    uint32_t nargs;
    char* reply;
    uint32_t replySize;
} CmdCall;

static int CmdBody(lua_State* L) {
    CmdCall* k = (CmdCall*)lua_touserdata(L, 1);
    luaL_checkstack(L, (int)k->nargs + 2, "too many arguments");
    lua_rawgeti(L, LUA_REGISTRYINDEX, k->c->ref);
    for (uint32_t i = 0; i < k->nargs; ++i) {
        const sco_arg* a = &k->args[i];
        switch (a->type) {
            case SCO_ARG_INT:    lua_pushinteger(L, (lua_Integer)a->v.i); break;
            case SCO_ARG_FLOAT:  lua_pushnumber(L, a->v.f); break;
            case SCO_ARG_STRING: lua_pushstring(L, a->v.s); break;
            case SCO_ARG_BOOL:   lua_pushboolean(L, a->v.i != 0); break;
            default:             lua_pushnil(L); break;
        }
    }
    lua_call(L, (int)k->nargs, 1);
    if (lua_isnil(L, -1)) Copy(k->reply, k->replySize, "");
    else if (lua_type(L, -1) == LUA_TSTRING) Copy(k->reply, k->replySize, lua_tostring(L, -1));
    else return luaL_error(L, "reply must be a string or nil, not %s", luaL_typename(L, -1));
    return 0;
}

static sco_result CmdThunk(const sco_arg* args, uint32_t nargs, void* ctx, char* reply, uint32_t replySize) {
    CmdCtx* c = (CmdCtx*)ctx;
    Script* s = c->s;
    if (!s->alive || !s->L) { Copy(reply, replySize, "script disabled"); return SCO_UNAVAILABLE; }
    if (g_depth >= MAX_DEPTH || !lua_checkstack(s->L, 4)) { Copy(reply, replySize, "calls nested too deep"); return SCO_TOO_MANY; }
    const int top = lua_gettop(s->L);
    CmdCall k = { c, args, nargs, reply, replySize };
    const int st = Enter(s, CmdBody, &k);
    if (st == LUA_OK) { lua_settop(s->L, top); return SCO_OK; }
    char num[48];
    Copy(reply, replySize, ErrorText(s->L, num, sizeof(num)));
    const sco_result r = s->over ? SCO_CRASHED : st == LUA_ERRMEM ? SCO_TOO_MANY : SCO_BAD_ARG;
    Failed(s, c->name, st);
    lua_settop(s->L, top);
    return r;
}

/* ---- tasks ---------------------------------------------------------------------------------- */

static void Unlink(Task* t) {
    if (t->prev) t->prev->next = t->next; else t->s->tasks = t->next;
    if (t->next) t->next->prev = t->prev;
    --t->s->ntasks;
}

static int TaskBody(lua_State* L) {
    Task* t = (Task*)lua_touserdata(L, 1);
    lua_rawgeti(L, LUA_REGISTRYINDEX, t->ref);
    lua_call(L, 0, 0);
    return 0;
}

static void TaskThunk(void* ctx) {
    Task* t = (Task*)ctx;
    Script* s = t->s;
    Unlink(t);
    if (s->L) {
        const int top = lua_gettop(s->L);
        if (s->alive && g_depth < MAX_DEPTH && lua_checkstack(s->L, 4)) {
            const int st = Enter(s, TaskBody, t);
            if (st != LUA_OK) Failed(s, "task", st);
        }
        lua_settop(s->L, top);
        luaL_unref(s->L, LUA_REGISTRYINDEX, t->ref);
    }
    free(t);
}

/* ---- the sco table -------------------------------------------------------------------------- */

static int PushResult(lua_State* L, sco_result r) {
    if (r == SCO_OK) { lua_pushboolean(L, 1); return 1; }
    lua_pushboolean(L, 0);
    lua_pushstring(L, ResultName(r));
    return 2;
}
int sco_lua_push_result(lua_State* L, sco_result r) { return PushResult(L, r); }

static int L_host_version(lua_State* L) {
    const char* v = Of(L)->api->host_version();
    lua_pushstring(L, v ? v : "");
    return 1;
}

static int L_has(lua_State* L) {
    lua_pushboolean(L, Of(L)->api->has(luaL_checkstring(L, 1)) != 0);
    return 1;
}

static int L_status(lua_State* L) {
    Script* s = Of(L);
    s->api->status(s->self, luaL_checkstring(L, 1));
    return 0;
}

static int L_log(lua_State* L) {
    static const char* const levels[] = { "info", "warn", "error", NULL };
    const int level = luaL_checkoption(L, 1, NULL, levels);
    Script* s = Of(L);
    s->api->log(s->self, (sco_log_level)level, luaL_checkstring(L, 2));
    return 0;
}

static int L_print(lua_State* L) {
    const int n = lua_gettop(L);
    luaL_Buffer b;
    luaL_buffinit(L, &b);
    for (int i = 1; i <= n; ++i) {
        if (i > 1) luaL_addchar(&b, '\t');
        luaL_tolstring(L, i, NULL);
        luaL_addvalue(&b);
    }
    luaL_pushresult(&b);
    Script* s = Of(L);
    s->api->log(s->self, SCO_LOG_INFO, lua_tostring(L, -1));
    return 0;
}

/* subs[event], created when create is set; pushes it (or nil). */
static void PushSubs(lua_State* L, const char* event, int create) {
    lua_getfield(L, LUA_REGISTRYINDEX, SUBS_KEY);
    lua_getfield(L, -1, event);
    if (lua_isnil(L, -1) && create) {
        lua_pop(L, 1);
        lua_newtable(L);
        lua_pushvalue(L, -1);
        lua_setfield(L, -3, event);
    }
    lua_remove(L, -2);
}

static int L_subscribe(lua_State* L) {
    Script* s = Of(L);
    if (!s->alive) return PushResult(L, SCO_UNAVAILABLE);   /* disabled, an outer frame still running */
    const char* event = luaL_checkstring(L, 1);
    luaL_checktype(L, 2, LUA_TFUNCTION);
    if (strlen(event) >= NAME_MAX_ || !*event) return PushResult(L, SCO_BAD_ARG);
    PushSubs(L, event, 1);
    const int list = lua_gettop(L);
    if (StillSubscribed(L, list, 2)) return PushResult(L, SCO_BAD_ARG);   /* same key twice */
    /* Dispatch rescans the list before each callback, uncharged: keep it short. */
    if (lua_rawlen(L, list) >= SCO_LUA_MAX_SUBSCRIBERS) return PushResult(L, SCO_TOO_MANY);
    EventSub* e = NULL;
    for (int i = 0; i < SCO_LUA_MAX_EVENTS && !e; ++i)
        if (s->events[i].live && strcmp(s->events[i].name, event) == 0) e = &s->events[i];
    if (!e) {
        for (int i = 0; i < SCO_LUA_MAX_EVENTS && !e; ++i) if (!s->events[i].live) e = &s->events[i];
        if (!e) return PushResult(L, SCO_TOO_MANY);
        e->s = s;
        Copy(e->name, sizeof(e->name), event);
        const sco_result r = s->api->subscribe(s->self, event, sco_lua_event_, e);
        if (r != SCO_OK) return PushResult(L, r);
        e->live = 1;
    }
    lua_pushvalue(L, 2);
    lua_rawseti(L, list, (lua_Integer)lua_rawlen(L, list) + 1);
    return PushResult(L, SCO_OK);
}

static int L_unsubscribe(lua_State* L) {
    Script* s = Of(L);
    const char* event = luaL_checkstring(L, 1);
    luaL_checktype(L, 2, LUA_TFUNCTION);
    PushSubs(L, event, 0);
    if (!lua_istable(L, -1)) return PushResult(L, SCO_NOT_FOUND);
    const int list = lua_gettop(L);
    const lua_Integer n = (lua_Integer)lua_rawlen(L, list);
    lua_Integer at = 0;
    for (lua_Integer i = 1; i <= n && !at; ++i) {
        lua_rawgeti(L, list, i);
        if (lua_rawequal(L, -1, 2)) at = i;
        lua_pop(L, 1);
    }
    if (!at) return PushResult(L, SCO_NOT_FOUND);
    for (lua_Integer i = at; i < n; ++i) { lua_rawgeti(L, list, i + 1); lua_rawseti(L, list, i); }
    lua_pushnil(L);
    lua_rawseti(L, list, n);
    if (n == 1) {
        for (int i = 0; i < SCO_LUA_MAX_EVENTS; ++i) {
            EventSub* e = &s->events[i];
            if (e->live && strcmp(e->name, event) == 0) {
                s->api->unsubscribe(s->self, event, sco_lua_event_);
                e->live = 0;
            }
        }
        lua_getfield(L, LUA_REGISTRYINDEX, SUBS_KEY);
        lua_pushnil(L);
        lua_setfield(L, -2, event);
    }
    return PushResult(L, SCO_OK);
}

static int L_run_on_game_thread(lua_State* L) {
    Script* s = Of(L);
    if (!s->alive) return PushResult(L, SCO_UNAVAILABLE);
    luaL_checktype(L, 1, LUA_TFUNCTION);
    if (s->ntasks >= SCO_LUA_MAX_TASKS) return PushResult(L, SCO_TOO_MANY);
    lua_pushvalue(L, 1);
    const int ref = luaL_ref(L, LUA_REGISTRYINDEX);   /* first: it raises on no memory, before any malloc */
    Task* t = (Task*)malloc(sizeof(Task));
    if (!t) {
        luaL_unref(L, LUA_REGISTRYINDEX, ref);
        return PushResult(L, SCO_TOO_MANY);
    }
    t->s = s;
    t->ref = ref;
    const sco_result r = s->api->run_on_game_thread(s->self, TaskThunk, t);
    if (r != SCO_OK) {
        luaL_unref(L, LUA_REGISTRYINDEX, t->ref);
        free(t);
        return PushResult(L, r);
    }
    t->prev = NULL;
    t->next = s->tasks;
    if (s->tasks) s->tasks->prev = t;
    s->tasks = t;
    ++s->ntasks;
    return PushResult(L, SCO_OK);
}

/* t[key] as an optional string (NULL when nil); pushes the value. */
static const char* OptField(lua_State* L, int t, const char* key, int* bad) {
    const int ty = lua_getfield(L, t, key);
    if (ty == LUA_TNIL) return NULL;
    if (ty != LUA_TSTRING) { *bad = 1; return NULL; }
    return lua_tostring(L, -1);
}

static int TypeOf(const char* name) {
    for (int i = 0; i < 4; ++i) if (name && strcmp(name, kTypeNames[i]) == 0) return i;
    return -1;
}

static int L_register_command(lua_State* L) {
    Script* s = Of(L);
    if (!s->alive) return PushResult(L, SCO_UNAVAILABLE);
    luaL_checktype(L, 1, LUA_TTABLE);
    luaL_checkstack(L, 8 + 4 * MAX_ARGS, "register_command");
    int bad = 0;
    const char* name = OptField(L, 1, "name", &bad);
    const char* title = OptField(L, 1, "title", &bad);
    const char* help = OptField(L, 1, "help", &bad);
    const char* cap = OptField(L, 1, "capability", &bad);
    if (lua_getfield(L, 1, "fn") != LUA_TFUNCTION) bad = 1;
    const int fn = lua_gettop(L);
    sco_arg_def defs[MAX_ARGS];
    uint32_t nargs = 0;
    const int ty = lua_getfield(L, 1, "args");
    if (ty == LUA_TTABLE) {
        const int args = lua_gettop(L);
        const lua_Integer n = (lua_Integer)lua_rawlen(L, args);
        if (n > MAX_ARGS) bad = 1;
        for (lua_Integer i = 1; i <= n && !bad; ++i) {
            if (lua_rawgeti(L, args, i) != LUA_TTABLE) { bad = 1; break; }
            const int a = lua_gettop(L);
            defs[nargs].name = OptField(L, a, "name", &bad);
            const int type = TypeOf(OptField(L, a, "type", &bad));
            defs[nargs].help = OptField(L, a, "help", &bad);
            if (!defs[nargs].name || type < 0) bad = 1;
            defs[nargs].type = (uint32_t)(type < 0 ? 0 : type);
            defs[nargs]._pad = 0;
            ++nargs;
        }
    } else if (ty != LUA_TNIL) {
        bad = 1;
    }
    if (bad || !name || strlen(name) >= NAME_MAX_) return PushResult(L, SCO_BAD_ARG);
    if (s->ncmds == SCO_LUA_MAX_COMMANDS) return PushResult(L, SCO_TOO_MANY);
    CmdCtx* c = &s->cmds[s->ncmds];
    lua_pushvalue(L, fn);
    c->s = s;
    c->ref = luaL_ref(L, LUA_REGISTRYINDEX);
    Copy(c->name, sizeof(c->name), name);
    sco_command cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.size = sizeof(sco_command);
    cmd.name = name;
    cmd.title = title;
    cmd.help = help;
    cmd.capability = cap;
    cmd.args = nargs ? defs : NULL;
    cmd.nargs = nargs;
    cmd.arg_def_size = sizeof(sco_arg_def);
    cmd.fn = CmdThunk;
    cmd.ctx = c;
    const sco_result r = s->api->register_command(s->self, &cmd);
    if (r != SCO_OK) {
        luaL_unref(L, LUA_REGISTRYINDEX, c->ref);
        return PushResult(L, r);
    }
    ++s->ncmds;
    return PushResult(L, SCO_OK);
}

static const sco_command* FindCommand(const sco_api* api, const char* name) {
    const sco_command* list[512];
    const uint32_t n = api->list_commands(list, 512);
    for (uint32_t i = 0; i < n && i < 512; ++i)
        if (list[i] && list[i]->name && strcmp(list[i]->name, name) == 0) return list[i];
    return NULL;
}

typedef struct Done { int called; sco_result r; char reply[REPLY_SIZE]; } Done;

static void DoneCb(sco_result r, const char* reply, void* ctx) {
    Done* d = (Done*)ctx;
    d->called = 1;
    d->r = r;
    Copy(d->reply, sizeof(d->reply), reply);
}

static int L_invoke(lua_State* L) {
    Script* s = Of(L);
    if (!s->alive) return PushResult(L, SCO_UNAVAILABLE);   /* disabled, an outer frame still running */
    const char* name = luaL_checkstring(L, 1);
    const int nargs = lua_gettop(L) - 1;
    const sco_command* c = FindCommand(s->api, name);
    if (!c) return PushResult(L, SCO_NOT_FOUND);
    if (nargs > MAX_ARGS || (uint32_t)nargs != c->nargs) return PushResult(L, SCO_BAD_ARG);
    sco_arg args[MAX_ARGS];
    const unsigned char* at = (const unsigned char*)c->args;
    for (int i = 0; i < nargs; ++i, at += c->arg_def_size) {
        const sco_arg_def* d = (const sco_arg_def*)at;
        const int v = i + 2;
        sco_arg* a = &args[i];
        memset(a, 0, sizeof(*a));
        a->type = d->type;
        int ok = 0;
        switch (d->type) {
            case SCO_ARG_INT:    { int isint = 0; a->v.i = lua_tointegerx(L, v, &isint); ok = isint && lua_isinteger(L, v); break; }   /* 2.0 is a float */
            case SCO_ARG_FLOAT:  ok = lua_type(L, v) == LUA_TNUMBER; a->v.f = lua_tonumber(L, v); break;
            case SCO_ARG_STRING: ok = lua_type(L, v) == LUA_TSTRING; a->v.s = lua_tostring(L, v); break;
            case SCO_ARG_BOOL:   ok = lua_type(L, v) == LUA_TBOOLEAN; a->v.i = lua_toboolean(L, v); break;
            default: break;
        }
        if (!ok) return PushResult(L, SCO_BAD_ARG);
    }
    Done d;
    memset(&d, 0, sizeof(d));
    const sco_result r = s->api->invoke(s->self, name, nargs ? args : NULL, (uint32_t)nargs, DoneCb, &d);
    CheckBudget(L);
    if (r != SCO_OK && !d.called) return PushResult(L, r);
    if (d.r == SCO_OK) { lua_pushboolean(L, 1); lua_pushstring(L, d.reply); return 2; }
    lua_pushboolean(L, 0);
    lua_pushstring(L, ResultName(d.r));
    lua_pushstring(L, d.reply);
    return 3;
}

static void SetString(lua_State* L, const char* key, const char* v) {
    if (!v) return;
    lua_pushstring(L, v);
    lua_setfield(L, -2, key);
}

static int L_list_commands(lua_State* L) {
    Script* s = Of(L);
    const sco_command* list[512];
    uint32_t n = s->api->list_commands(list, 512);
    if (n > 512) n = 512;
    lua_createtable(L, (int)n, 0);
    for (uint32_t i = 0; i < n; ++i) {
        const sco_command* c = list[i];
        if (!c) continue;
        lua_createtable(L, 0, 5);
        SetString(L, "name", c->name);
        SetString(L, "title", c->title);
        SetString(L, "help", c->help);
        SetString(L, "capability", c->capability);
        lua_createtable(L, (int)c->nargs, 0);
        const unsigned char* at = (const unsigned char*)c->args;
        for (uint32_t k = 0; k < c->nargs; ++k, at += c->arg_def_size) {
            const sco_arg_def* d = (const sco_arg_def*)at;
            lua_createtable(L, 0, 3);
            SetString(L, "name", d->name);
            SetString(L, "type", d->type < 4 ? kTypeNames[d->type] : "?");
            SetString(L, "help", d->help);
            lua_rawseti(L, -2, (lua_Integer)k + 1);
        }
        lua_setfield(L, -2, "args");
        lua_rawseti(L, -2, (lua_Integer)i + 1);
    }
    return 1;
}

/* ---- sco.settings (the host service sco_settings.h): read-only ------------------------------ */

#define SETTINGS_STEPS 20   /* steps per sco.settings.get */

static const sco_settings_v1* Settings(const Script* s) {
    const void* t = NULL;
    if (s->api->size <= offsetof(sco_api, query_service) || !s->api->query_service) return NULL;
    if (s->api->query_service(SCO_SETTINGS_NAME, SCO_SETTINGS_VERSION_1_0, &t) != SCO_OK) return NULL;
    return (const sco_settings_v1*)t;
}

/* get(name): the value of one of this plugin's declared settings (a boolean, an integer, a
 * number, or a string for string and enum settings), or nil, err, message: "not_found" when the
 * plugin declares no such setting, "unavailable" on a host without the service. There is no set:
 * the player changes values in the product's menu. */
static int L_settings_get(lua_State* L) {
    Script* s = Of(L);
    size_t n = 0;
    const char* name = luaL_checklstring(L, 1, &n);
    const sco_settings_v1* st = Settings(s);
    sco_result r = SCO_UNAVAILABLE;
    sco_lua_step(L, SETTINGS_STEPS);
    if (st && strlen(name) != n) {
        r = SCO_BAD_ARG;                          /* a NUL in the name */
    } else if (st) {
        int32_t b = 0;
        int64_t i = 0;
        double f = 0;
        char buf[SCO_SETTINGS_MAX_STRING + 1];
        uint32_t size = sizeof(buf);
        /* The host has no type query: try each getter; only a type mismatch (bad_arg) moves on. */
        if ((r = st->get_bool(s->self, name, &b)) == SCO_OK) { lua_pushboolean(L, b); return 1; }
        if (r == SCO_BAD_ARG && (r = st->get_int(s->self, name, &i)) == SCO_OK) { lua_pushinteger(L, (lua_Integer)i); return 1; }
        if (r == SCO_BAD_ARG && (r = st->get_float(s->self, name, &f)) == SCO_OK) { lua_pushnumber(L, (lua_Number)f); return 1; }
        if (r == SCO_BAD_ARG && (r = st->get_string(s->self, name, buf, &size)) == SCO_OK) { lua_pushlstring(L, buf, size - 1); return 1; }
    }
    lua_pushnil(L);
    lua_pushstring(L, ResultName(r));
    if (!st) {
        lua_pushliteral(L, "sco.settings is not available on this host");
    } else {
        char msg[256];
        uint32_t size = sizeof(msg);
        if (r == SCO_BAD_ARG && strlen(name) != n) lua_pushliteral(L, "a setting name with a NUL byte");
        else if (st->last_error(s->self, msg, &size) == SCO_OK && msg[0]) lua_pushstring(L, msg);
        else return 2;
    }
    return 3;
}

/* ---- sco.store (the host service sco_storage.h) --------------------------------------------- */

#define STORE_CURSOR   "sco-lua.cursor"
#define STORE_STEPS    100    /* steps per storage call, and per row or key read */
#define STORE_MAX_ROWS 1000   /* rows sco.store.sql returns as a table */
#define STORE_MAX_KEYS 1000   /* keys one sco.store.keys call returns */
#define STORE_MAX_PARAMS 32

typedef struct StoreCursor { uint64_t id; int open; } StoreCursor;

/* The service as the host publishes it now; NULL on a host without it (a 1.0 host, or a product
 * without a data folder). */
static const sco_storage_v1* Storage(const Script* s) {
    const void* t = NULL;
    if (s->api->size <= offsetof(sco_api, query_service) || !s->api->query_service) return NULL;
    if (s->api->query_service(SCO_STORAGE_NAME, SCO_STORAGE_VERSION_1_0, &t) != SCO_OK) return NULL;
    return (const sco_storage_v1*)t;
}

/* nil, the result's name and the host's message (when it left one): every sco.store error. */
static int StoreFail(lua_State* L, const Script* s, const sco_storage_v1* st, sco_result r) {
    char msg[256];
    uint32_t size = sizeof(msg);
    lua_pushnil(L);
    lua_pushstring(L, ResultName(r));
    if (!st) { lua_pushliteral(L, "sco.storage is not available on this host"); return 3; }
    if (st->last_error(s->self, msg, &size) == SCO_OK && msg[0]) { lua_pushstring(L, msg); return 3; }
    return 2;
}

/* nil, "bad_arg", why: a call sco-lua refuses before the host sees it. */
static int StoreBad(lua_State* L) {
    lua_pushnil(L);
    lua_pushstring(L, ResultName(SCO_BAD_ARG));
    lua_pushliteral(L, "a key or SQL with a NUL byte, a value over 1 MiB, or a bad parameter list or limit");
    return 3;
}

/* Argument i as a string without embedded NULs (keys and SQL are C strings). NULL: it has one. */
static const char* CString(lua_State* L, int i) {
    size_t n = 0;
    const char* p = luaL_checklstring(L, i, &n);
    return strlen(p) == n ? p : NULL;
}

/* Every call starts here: one charge to the step budget, then the table (NULL: unavailable). */
static const sco_storage_v1* StoreCall(lua_State* L, Script** out) {
    *out = Of(L);
    sco_lua_step(L, STORE_STEPS);
    return Storage(*out);
}

static int L_store_available(lua_State* L) {
    lua_pushboolean(L, Storage(Of(L)) != NULL);
    return 1;
}

/* get(key): the value as a string, nil when there is none, or nil, err, message. */
static int L_store_get(lua_State* L) {
    Script* s;
    const sco_storage_v1* st = StoreCall(L, &s);
    const char* key = CString(L, 1);
    if (!st) return StoreFail(L, s, st, SCO_UNAVAILABLE);
    if (!key) return StoreBad(L);
    for (int tries = 0; tries < 3; ++tries) {   /* the size can change between the two calls */
        uint32_t size = 0;
        sco_result r = st->get(s->self, key, NULL, &size);
        if (r == SCO_NOT_FOUND) { lua_pushnil(L); return 1; }
        if (r == SCO_OK) { lua_pushliteral(L, ""); return 1; }
        if (r != SCO_TOO_MANY) return StoreFail(L, s, st, r);
        luaL_Buffer b;
        char* buf = luaL_buffinitsize(L, &b, size);
        r = st->get(s->self, key, buf, &size);
        if (r == SCO_OK) { luaL_pushresultsize(&b, size); return 1; }
        lua_pop(L, 1);                          /* the buffer */
        if (r == SCO_NOT_FOUND) { lua_pushnil(L); return 1; }
        if (r != SCO_TOO_MANY) return StoreFail(L, s, st, r);
    }
    return StoreFail(L, s, st, SCO_FAILED);
}

/* put(key, value): true, or nil, err, message. */
static int L_store_put(lua_State* L) {
    Script* s;
    const sco_storage_v1* st = StoreCall(L, &s);
    const char* key = CString(L, 1);
    size_t n = 0;
    luaL_checktype(L, 2, LUA_TSTRING);
    const char* value = lua_tolstring(L, 2, &n);
    if (!st) return StoreFail(L, s, st, SCO_UNAVAILABLE);
    if (!key || n > SCO_STORAGE_MAX_VALUE) return StoreBad(L);
    const sco_result r = st->put(s->self, key, value, (uint32_t)n);
    if (r != SCO_OK) return StoreFail(L, s, st, r);
    lua_pushboolean(L, 1);
    return 1;
}

/* delete(key): true when it removed a value, false when there was none, or nil, err, message. */
static int L_store_delete(lua_State* L) {
    Script* s;
    const sco_storage_v1* st = StoreCall(L, &s);
    const char* key = CString(L, 1);
    if (!st) return StoreFail(L, s, st, SCO_UNAVAILABLE);
    if (!key) return StoreBad(L);
    const sco_result r = st->del(s->self, key);
    if (r != SCO_OK && r != SCO_NOT_FOUND) return StoreFail(L, s, st, r);
    lua_pushboolean(L, r == SCO_OK);
    return 1;
}

/* keys([prefix [, after [, limit]]]): a list of at most limit (default and most STORE_MAX_KEYS)
 * keys starting with prefix, sorting after `after`, in byte order; pass the last one back as
 * after for the next page. Or nil, err, message. */
static int L_store_keys(lua_State* L) {
    Script* s;
    const sco_storage_v1* st = StoreCall(L, &s);
    const char* prefix = lua_isnoneornil(L, 1) ? NULL : CString(L, 1);
    const char* after = lua_isnoneornil(L, 2) ? NULL : CString(L, 2);
    const lua_Integer limit = luaL_optinteger(L, 3, STORE_MAX_KEYS);
    if (!st) return StoreFail(L, s, st, SCO_UNAVAILABLE);
    if ((!prefix && !lua_isnoneornil(L, 1)) || (!after && !lua_isnoneornil(L, 2)) ||
        limit < 1 || limit > STORE_MAX_KEYS)
        return StoreBad(L);
    char key[SCO_STORAGE_MAX_KEY + 1];
    lua_createtable(L, 0, 0);
    const int list = lua_gettop(L);
    for (lua_Integer n = 0; n < limit; ++n) {
        uint32_t size = sizeof(key);
        const sco_result r = st->next_key(s->self, prefix, after, key, &size);
        if (r == SCO_NOT_FOUND) break;
        if (r != SCO_OK) return StoreFail(L, s, st, r);
        lua_pushstring(L, key);
        after = lua_tostring(L, -1);           /* kept alive by the list */
        lua_rawseti(L, list, n + 1);
        if (n + 1 < limit) sco_lua_step(L, STORE_STEPS);
    }
    lua_settop(L, list);
    return 1;
}

static int StoreTx(lua_State* L, sco_result (*fn)(sco_plugin*), const sco_storage_v1* st, Script* s) {
    if (!st) return StoreFail(L, s, st, SCO_UNAVAILABLE);
    const sco_result r = fn(s->self);
    if (r != SCO_OK) return StoreFail(L, s, st, r);
    lua_pushboolean(L, 1);
    return 1;
}

static int L_store_begin(lua_State* L) {
    Script* s;
    const sco_storage_v1* st = StoreCall(L, &s);
    return StoreTx(L, st ? st->begin : NULL, st, s);
}

static int L_store_commit(lua_State* L) {
    Script* s;
    const sco_storage_v1* st = StoreCall(L, &s);
    return StoreTx(L, st ? st->commit : NULL, st, s);
}

static int L_store_rollback(lua_State* L) {
    Script* s;
    const sco_storage_v1* st = StoreCall(L, &s);
    return StoreTx(L, st ? st->rollback : NULL, st, s);
}

/* The parameter list at index i (nil, or a list: integer, number, string, boolean as 0/1; its
 * field n, when set, counts trailing nils as NULL). Strings stay referenced by the list. 0 when
 * a value has another type or there are too many. */
static int ToParams(lua_State* L, int i, sco_sql_value* p, uint32_t* n) {
    *n = 0;
    if (lua_isnoneornil(L, i)) return 1;
    luaL_checktype(L, i, LUA_TTABLE);
    lua_Integer count = (lua_Integer)lua_rawlen(L, i);
    lua_getfield(L, i, "n");
    if (lua_isinteger(L, -1)) count = lua_tointeger(L, -1);
    lua_pop(L, 1);
    if (count < 0 || count > STORE_MAX_PARAMS) return 0;
    for (lua_Integer k = 1; k <= count; ++k) {
        sco_sql_value* v = &p[k - 1];
        memset(v, 0, sizeof(*v));
        const int ty = lua_rawgeti(L, i, k);
        switch (ty) {
            case LUA_TNIL: v->type = SCO_SQL_NULL; break;
            case LUA_TBOOLEAN: v->type = SCO_SQL_INT; v->v.i = lua_toboolean(L, -1); break;
            case LUA_TNUMBER:
                if (lua_isinteger(L, -1)) { v->type = SCO_SQL_INT; v->v.i = (int64_t)lua_tointeger(L, -1); }
                else { v->type = SCO_SQL_FLOAT; v->v.f = (double)lua_tonumber(L, -1); }
                break;
            case LUA_TSTRING: {
                size_t len = 0;
                v->type = SCO_SQL_TEXT;
                v->v.p = lua_tolstring(L, -1, &len);   /* the list keeps the string alive */
                if (len > UINT32_MAX) { lua_pop(L, 1); return 0; }
                v->size = (uint32_t)len;
                break;
            }
            default: lua_pop(L, 1); return 0;
        }
        lua_pop(L, 1);
    }
    *n = (uint32_t)count;
    return 1;
}

/* exec(sql [, params]): the number of rows changed, or nil, err, message. */
static int L_store_exec(lua_State* L) {
    Script* s;
    const sco_storage_v1* st = StoreCall(L, &s);
    const char* sql = CString(L, 1);
    sco_sql_value params[STORE_MAX_PARAMS];
    uint32_t n = 0;
    const int ok = ToParams(L, 2, params, &n);
    if (!st) return StoreFail(L, s, st, SCO_UNAVAILABLE);
    if (!sql || !ok) return StoreBad(L);
    int64_t changes = 0;
    const sco_result r = st->exec(s->self, sql, n ? params : NULL, n, &changes);
    if (r != SCO_OK) return StoreFail(L, s, st, r);
    lua_pushinteger(L, (lua_Integer)changes);
    return 1;
}

/* The cursor's __close: runs when sql returns and when an error unwinds through it (a script
 * error in the row callback, the step budget, no memory), so no path leaves a cursor open. */
static int L_cursor_close(lua_State* L) {
    StoreCursor* c = (StoreCursor*)lua_touserdata(L, 1);
    if (c && c->open) {
        c->open = 0;
        const sco_storage_v1* st = Storage(Of(L));
        if (st) st->close(Of(L)->self, c->id);
    }
    return 0;
}

/* Pushes the current row as { column = value, ... } (NULL columns are absent). names: the list
 * of column names. */
static sco_result PushRow(lua_State* L, const sco_storage_v1* st, const Script* s, uint64_t cur,
                          int names, uint32_t ncols) {
    lua_createtable(L, 0, (int)ncols);
    for (uint32_t i = 0; i < ncols; ++i) {
        sco_sql_value v;
        memset(&v, 0, sizeof(v));
        sco_result r = st->column(s->self, cur, i, &v, NULL, NULL);
        if (r != SCO_OK) return r;
        switch (v.type) {
            case SCO_SQL_NULL: continue;
            case SCO_SQL_INT: lua_pushinteger(L, (lua_Integer)v.v.i); break;
            case SCO_SQL_FLOAT: lua_pushnumber(L, (lua_Number)v.v.f); break;
            case SCO_SQL_TEXT: case SCO_SQL_BLOB: {
                uint32_t size = v.size + (v.type == SCO_SQL_TEXT ? 1u : 0u);
                luaL_Buffer b;
                char* buf = luaL_buffinitsize(L, &b, size ? size : 1);
                r = size ? st->column(s->self, cur, i, &v, buf, &size) : SCO_OK;
                if (r != SCO_OK) return r;
                luaL_pushresultsize(&b, v.size);
                break;
            }
            default: return SCO_FAILED;
        }
        lua_rawgeti(L, names, (lua_Integer)i + 1);
        lua_insert(L, -2);
        lua_rawset(L, -3);
    }
    return SCO_OK;
}

/* sql(sql [, params [, fn]]): without fn, a list of rows (at most STORE_MAX_ROWS; more is
 * too_many); with fn, fn(row) for each row until it returns false, then true. Or nil, err,
 * message. Each row counts STORE_STEPS against the step budget. */
static int L_store_sql(lua_State* L) {
    Script* s;
    const sco_storage_v1* st = StoreCall(L, &s);
    const char* sql = CString(L, 1);
    sco_sql_value params[STORE_MAX_PARAMS];
    uint32_t n = 0;
    const int ok = ToParams(L, 2, params, &n);
    const int fn = !lua_isnoneornil(L, 3);
    if (fn) luaL_checktype(L, 3, LUA_TFUNCTION);
    if (!st) return StoreFail(L, s, st, SCO_UNAVAILABLE);
    if (!sql || !ok) return StoreBad(L);
    lua_settop(L, 3);
    /* The guard first: allocating it can fail, opening the cursor after it can't leak. */
    StoreCursor* c = (StoreCursor*)lua_newuserdatauv(L, sizeof(StoreCursor), 0);
    c->id = 0;
    c->open = 0;
    luaL_setmetatable(L, STORE_CURSOR);
    lua_toclose(L, 4);
    lua_createtable(L, 0, 0);                   /* 5: column names */
    lua_createtable(L, fn ? 0 : 8, 0);          /* 6: rows */
    sco_result r = st->query(s->self, sql, n ? params : NULL, n, &c->id);
    if (r != SCO_OK) return StoreFail(L, s, st, r);
    c->open = 1;
    uint32_t ncols = 0;
    r = st->column_count(s->self, c->id, &ncols);
    for (uint32_t i = 0; i < ncols && r == SCO_OK; ++i) {
        char name[256];
        uint32_t size = sizeof(name);
        r = st->column_name(s->self, c->id, i, name, &size);
        if (r == SCO_TOO_MANY) {                /* a long name: ask its size */
            luaL_Buffer b;
            char* buf = luaL_buffinitsize(L, &b, size);
            r = st->column_name(s->self, c->id, i, buf, &size);
            if (r == SCO_OK) luaL_pushresultsize(&b, size ? size - 1 : 0);
        } else if (r == SCO_OK) {
            lua_pushstring(L, name);
        }
        if (r == SCO_OK) lua_rawseti(L, 5, (lua_Integer)i + 1);
    }
    lua_Integer rows = 0;
    while (r == SCO_OK) {
        sco_lua_step(L, STORE_STEPS);
        r = st->step(s->self, c->id);
        if (r == SCO_NOT_FOUND) { r = SCO_OK; break; }
        if (r != SCO_OK) break;
        if (!fn && rows == STORE_MAX_ROWS) {
            lua_closeslot(L, 4);
            lua_pushnil(L);
            lua_pushstring(L, ResultName(SCO_TOO_MANY));
            lua_pushfstring(L, "more than %d rows: add a LIMIT, or pass a function", STORE_MAX_ROWS);
            return 3;
        }
        if (fn) lua_pushvalue(L, 3);
        r = PushRow(L, st, s, c->id, 5, ncols);
        if (r != SCO_OK) break;
        if (!fn) { lua_rawseti(L, 6, ++rows); continue; }
        lua_call(L, 1, 1);                      /* an error here unwinds through the guard */
        const int stop = lua_type(L, -1) == LUA_TBOOLEAN && !lua_toboolean(L, -1);
        lua_settop(L, 6);
        if (stop) break;
    }
    if (r != SCO_OK) {
        const int got = StoreFail(L, s, st, r);   /* last_error before close overwrites it */
        lua_closeslot(L, 4);
        return got;
    }
    lua_closeslot(L, 4);
    if (fn) lua_pushboolean(L, 1);
    else lua_pushvalue(L, 6);
    return 1;
}

static void StoreTypes(lua_State* L) {
    luaL_newmetatable(L, STORE_CURSOR);
    lua_pushcfunction(L, L_cursor_close);
    lua_setfield(L, -2, "__close");
    lua_pushboolean(L, 0);
    lua_setfield(L, -2, "__metatable");
    lua_pop(L, 1);
}

/* ---- sco.ui hotkeys (tabs and overlays need draw callbacks from Lua: G018) ------------------ */

static const sco_ui_v1* UiTable(const Script* s) {
    const void* t = NULL;
    if (s->api->size <= offsetof(sco_api, query_service)) return NULL;
    if (s->api->query_service(SCO_UI_NAME, SCO_UI_VERSION_1_0, &t) != SCO_OK) return NULL;
    return (const sco_ui_v1*)t;
}

/* true, or false, the result's name and the host's message when it left one. */
static int PushUiResult(lua_State* L, const Script* s, const sco_ui_v1* ui, sco_result r) {
    char msg[256];
    uint32_t size = sizeof(msg);
    if (r == SCO_OK) return PushResult(L, r);
    lua_pushboolean(L, 0);
    lua_pushstring(L, ResultName(r));
    if (ui->last_error(s->self, msg, &size) == SCO_OK && msg[0]) {
        lua_pushstring(L, msg);
        return 3;
    }
    return 2;
}

/* sco.bind_hotkey(chord, command, ...): the extra values are the command's arguments, typed by
 * the command's arg defs when it is registered, else by their Lua types (integer, float, string,
 * boolean). */
static int L_bind_hotkey(lua_State* L) {
    Script* s = Of(L);
    if (!s->alive) return PushResult(L, SCO_UNAVAILABLE);
    const char* chord = luaL_checkstring(L, 1);
    const char* name = luaL_checkstring(L, 2);
    const int nargs = lua_gettop(L) - 2;
    const sco_ui_v1* ui = UiTable(s);
    if (!ui) return PushResult(L, SCO_UNAVAILABLE);
    if (nargs > MAX_ARGS) return PushResult(L, SCO_BAD_ARG);
    const sco_command* c = FindCommand(s->api, name);
    if (c && (uint32_t)nargs != c->nargs) return PushResult(L, SCO_BAD_ARG);
    sco_arg args[MAX_ARGS];
    const unsigned char* at = c ? (const unsigned char*)c->args : NULL;
    for (int i = 0; i < nargs; ++i) {
        const int v = i + 3;
        sco_arg* a = &args[i];
        memset(a, 0, sizeof(*a));
        uint32_t type = SCO_ARG_INT;
        if (c) {
            type = ((const sco_arg_def*)at)->type;
            at += c->arg_def_size;
        } else {
            switch (lua_type(L, v)) {
                case LUA_TNUMBER:  type = lua_isinteger(L, v) ? SCO_ARG_INT : SCO_ARG_FLOAT; break;
                case LUA_TSTRING:  type = SCO_ARG_STRING; break;
                case LUA_TBOOLEAN: type = SCO_ARG_BOOL; break;
                default: return PushResult(L, SCO_BAD_ARG);
            }
        }
        a->type = type;
        int ok = 0;
        switch (type) {
            case SCO_ARG_INT:    ok = lua_isinteger(L, v); a->v.i = lua_tointeger(L, v); break;
            case SCO_ARG_FLOAT:  ok = lua_type(L, v) == LUA_TNUMBER; a->v.f = lua_tonumber(L, v); break;
            case SCO_ARG_STRING: ok = lua_type(L, v) == LUA_TSTRING; a->v.s = lua_tostring(L, v); break;
            case SCO_ARG_BOOL:   ok = lua_type(L, v) == LUA_TBOOLEAN; a->v.i = lua_toboolean(L, v); break;
            default: break;
        }
        if (!ok) return PushResult(L, SCO_BAD_ARG);
    }
    const sco_result r = ui->bind_hotkey(s->self, chord, name, nargs ? args : NULL, (uint32_t)nargs);
    return PushUiResult(L, s, ui, r);
}

/* sco.unbind_hotkey(chord) */
static int L_unbind_hotkey(lua_State* L) {
    Script* s = Of(L);
    if (!s->alive) return PushResult(L, SCO_UNAVAILABLE);
    const char* chord = luaL_checkstring(L, 1);
    const sco_ui_v1* ui = UiTable(s);
    if (!ui) return PushResult(L, SCO_UNAVAILABLE);
    return PushUiResult(L, s, ui, ui->unbind_hotkey(s->self, chord));
}

/* ---- sco.widgets: copied declarative panels, no draw callback/renderer ABI ---------------- */

#define WIDGET_HAS(t, member) ((t) && (t)->size >= offsetof(sco_widgets_v1, member) + sizeof((t)->member) && (t)->member)
static const sco_widgets_v1* WidgetsTable(const Script* s) {
    const void* t = NULL;
    if (s->api->size < offsetof(sco_api, query_service) + sizeof(s->api->query_service) ||
        !s->api->query_service) return NULL;
    if (s->api->query_service(SCO_WIDGETS_NAME, SCO_WIDGETS_VERSION_1_0, &t) != SCO_OK) return NULL;
    return (const sco_widgets_v1*)t;
}

static int L_widgets_available(lua_State* L) {
    const Script* s = Of(L);
    const sco_widgets_v1* t = s->alive ? WidgetsTable(s) : NULL;
    lua_pushboolean(L, WIDGET_HAS(t, set_panel) && WIDGET_HAS(t, remove_panel));
    return 1;
}

/* Never coerce numbers to text or silently truncate embedded NUL bytes. */
static sco_result WidgetString(lua_State* L, int at, size_t max, sco_result long_result, const char** out) {
    if (lua_type(L, at) != LUA_TSTRING) return SCO_BAD_ARG;
    size_t len = 0;
    const char* text = lua_tolstring(L, at, &len);
    if (len > max) return long_result; /* Bound the scan before inspecting bytes. */
    if (memchr(text, 0, len)) return SCO_BAD_ARG;
    *out = text;
    return SCO_OK;
}

static const char* const kWidgetKinds[] = {
    "label", "button", "toggle", "slider", "combo", "list", "text_input", "table", "row", "column"
};

static void WidgetField(lua_State* L, int table, const char* key) {
    lua_pushstring(L, key);
    lua_rawget(L, table);
}

static sco_result WidgetInteger(lua_State* L, int table, const char* key, int64_t min, int64_t max, int64_t* out) {
    WidgetField(L, table, key);
    if (!lua_isnil(L, -1)) {
        if (!lua_isinteger(L, -1)) { lua_pop(L, 1); return SCO_BAD_ARG; }
        const lua_Integer v = lua_tointeger(L, -1);
        if (v < min || v > max) { lua_pop(L, 1); return SCO_BAD_ARG; }
        *out = v;
    }
    lua_pop(L, 1);
    return SCO_OK;
}

static sco_result WidgetNumber(lua_State* L, int table, const char* key, double* out) {
    WidgetField(L, table, key);
    if (!lua_isnil(L, -1)) {
        if (lua_type(L, -1) != LUA_TNUMBER) { lua_pop(L, 1); return SCO_BAD_ARG; }
        *out = lua_tonumber(L, -1);
    }
    lua_pop(L, 1);
    return SCO_OK;
}

/* Raw, dense sequences only: do not invoke __len/__index or accept hidden tails. */
static sco_result WidgetSequence(lua_State* L, int at, size_t max, size_t* count) {
    if (!lua_istable(L, at)) return SCO_BAD_ARG;
    at = lua_absindex(L, at);
    *count = lua_rawlen(L, at);
    if (*count > max) return SCO_TOO_MANY;
    size_t seen = 0;
    lua_pushnil(L);
    while (lua_next(L, at)) {
        sco_lua_step(L, 1);
        const int valid = lua_isinteger(L, -2) && lua_tointeger(L, -2) > 0 &&
                          (uint64_t)lua_tointeger(L, -2) <= *count;
        lua_pop(L, 1);
        if (!valid) { lua_pop(L, 1); return SCO_BAD_ARG; }
        ++seen;
    }
    return seen == *count ? SCO_OK : SCO_BAD_ARG;
}

static void WidgetRoot(lua_State* L, int roots) {
    lua_pushvalue(L, -1);
    lua_rawseti(L, roots, (lua_Integer)lua_rawlen(L, roots) + 1);
}

/* Node/array buffers are budget-charged Lua userdata. Strong temporary roots
 * retain every borrowed string even when the input tree contains weak tables. */
static sco_result WidgetNode(lua_State* L, int node, int roots, sco_widget_node* n) {
    static const char* const keys[] = { "id", "parent", "kind", "disabled", "label", "text",
        "value", "minimum", "maximum", "index", "columns", "items" };
    lua_pushnil(L);
    while (lua_next(L, node)) {
        sco_lua_step(L, 1);
        int known = 0;
        const char* key = NULL;
        if (WidgetString(L, -2, 16, SCO_BAD_ARG, &key) == SCO_OK)
            for (size_t i = 0; i < sizeof(keys) / sizeof(keys[0]); ++i)
                if (!strcmp(key, keys[i])) { known = 1; break; }
        lua_pop(L, 1);
        if (!known) { lua_pop(L, 1); return SCO_BAD_ARG; }
    }
    int64_t v = 0;
    sco_result r = WidgetInteger(L, node, "id", 1, UINT32_MAX, &v);
    if (r != SCO_OK || !v) return SCO_BAD_ARG;
    n->id = (uint32_t)v;
    v = 0; r = WidgetInteger(L, node, "parent", 0, UINT32_MAX, &v);
    if (r != SCO_OK) return r;
    n->parent = (uint32_t)v;
    WidgetField(L, node, "kind");
    const char* kind = NULL;
    r = WidgetString(L, -1, 16, SCO_BAD_ARG, &kind);
    if (r == SCO_OK) for (uint32_t k = 0; k < 10; ++k)
        if (!strcmp(kind, kWidgetKinds[k])) { n->kind = k + 1; break; }
    lua_pop(L, 1);
    if (r != SCO_OK || !n->kind) return SCO_BAD_ARG;
    WidgetField(L, node, "disabled");
    if (!lua_isnil(L, -1) && lua_type(L, -1) != LUA_TBOOLEAN) { lua_pop(L, 1); return SCO_BAD_ARG; }
    n->flags = lua_toboolean(L, -1) ? SCO_WIDGET_DISABLED : 0;
    lua_pop(L, 1);
    const char** fields[] = { &n->label, &n->text };
    const char* names[] = { "label", "text" };
    for (int i = 0; i < 2; ++i) {
        WidgetField(L, node, names[i]);
        if (!lua_isnil(L, -1)) {
            r = WidgetString(L, -1, SCO_WIDGETS_MAX_TEXT, SCO_TOO_MANY, fields[i]);
            if (r == SCO_OK) WidgetRoot(L, roots);
        }
        lua_pop(L, 1);
        if (r != SCO_OK) return r;
    }
    if (n->kind == SCO_WIDGET_TOGGLE) {
        WidgetField(L, node, "value");
        if (!lua_isnil(L, -1) && lua_type(L, -1) != LUA_TBOOLEAN) { lua_pop(L, 1); return SCO_BAD_ARG; }
        n->value = lua_toboolean(L, -1) ? 1 : 0;
        lua_pop(L, 1);
    } else {
        r = WidgetNumber(L, node, "value", &n->value);
        if (r != SCO_OK) return r;
    }
    r = WidgetNumber(L, node, "minimum", &n->minimum);
    if (r == SCO_OK) r = WidgetNumber(L, node, "maximum", &n->maximum);
    if (r != SCO_OK) return r;
    v = 0; r = WidgetInteger(L, node, "index", INT32_MIN, INT32_MAX, &v);
    if (r != SCO_OK) return r;
    n->index = (int32_t)v;
    v = 0; r = WidgetInteger(L, node, "columns", 0, UINT32_MAX, &v);
    if (r != SCO_OK) return r;
    n->columns = (uint32_t)v;
    WidgetField(L, node, "items");
    if (!lua_isnil(L, -1)) {
        size_t count = 0;
        r = WidgetSequence(L, -1, SCO_WIDGETS_MAX_ITEMS, &count);
        if (r != SCO_OK) { lua_pop(L, 1); return r; }
        const int items = lua_gettop(L);
        n->item_count = (uint32_t)count;
        if (count) {
            const char** ptrs = (const char**)lua_newuserdatauv(L, count * sizeof(*ptrs), 0);
            WidgetRoot(L, roots);
            lua_pop(L, 1);
            n->items = ptrs;
            for (size_t i = 0; i < count; ++i) {
                sco_lua_step(L, 1);
                lua_rawgeti(L, items, (lua_Integer)i + 1);
                r = WidgetString(L, -1, SCO_WIDGETS_MAX_TEXT, SCO_TOO_MANY, &ptrs[i]);
                if (r == SCO_OK) WidgetRoot(L, roots);
                lua_pop(L, 1);
                if (r != SCO_OK) { lua_pop(L, 1); return r; }
            }
        }
    }
    lua_pop(L, 1);
    return SCO_OK;
}

static WidgetCtx* WidgetFind(Script* s, const char* id) {
    for (uint32_t i = 0; i < SCO_WIDGETS_MAX_PANELS; ++i)
        if (s->widgets[i].id[0] && !strcmp(s->widgets[i].id, id)) return &s->widgets[i];
    return NULL;
}

static void WidgetForget(lua_State* L, WidgetCtx* ctx) {
    if (!ctx) return;
    luaL_unref(L, LUA_REGISTRYINDEX, ctx->ref);
    memset(ctx, 0, sizeof(*ctx));
}

typedef struct WidgetCall { WidgetCtx* ctx; const char* panel; const sco_widget_event* event; } WidgetCall;
static int WidgetBody(lua_State* L) {
    WidgetCall* call = (WidgetCall*)lua_touserdata(L, 1);
    const sco_widget_event* e = call->event;
    /* Root on stack before Lua runs: reentrant replace/remove may unref this function. */
    lua_rawgeti(L, LUA_REGISTRYINDEX, call->ctx->ref);
    lua_pushstring(L, call->panel);
    lua_createtable(L, 0, 3);
    lua_pushinteger(L, e->node_id); lua_setfield(L, -2, "node_id");
    lua_pushstring(L, kWidgetKinds[e->kind - 1]); lua_setfield(L, -2, "kind");
    switch (e->kind) {
        case SCO_WIDGET_TOGGLE: lua_pushboolean(L, e->value != 0); lua_setfield(L, -2, "value"); break;
        case SCO_WIDGET_SLIDER: lua_pushnumber(L, e->value); lua_setfield(L, -2, "value"); break;
        case SCO_WIDGET_COMBO: case SCO_WIDGET_LIST:
            lua_pushinteger(L, e->index); lua_setfield(L, -2, "index"); break;
        case SCO_WIDGET_TEXT_INPUT: lua_pushstring(L, e->text); lua_setfield(L, -2, "text"); break;
        default: break;
    }
    lua_call(L, 2, 0);
    return 0;
}

static void WidgetThunk(const char* panel, const sco_widget_event* e, void* userdata) {
    WidgetCtx* ctx = (WidgetCtx*)userdata;
    Script* s = ctx ? ctx->s : NULL;
    if (!s || !s->used || !s->alive || !s->L || !ctx->id[0] || !panel || strcmp(panel, ctx->id) ||
        !e || e->size < sizeof(*e) || e->kind < SCO_WIDGET_BUTTON || e->kind > SCO_WIDGET_TEXT_INPUT ||
        !e->node_id || (e->kind == SCO_WIDGET_TEXT_INPUT && !e->text) ||
        g_depth >= MAX_DEPTH || !lua_checkstack(s->L, 4)) return;
    const int top = lua_gettop(s->L);
    WidgetCall call = { ctx, panel, e };
    const int st = Enter(s, WidgetBody, &call);
    if (st != LUA_OK) Failed(s, "widget event", st);
    lua_settop(s->L, top);
}

static int L_widgets_set_panel(lua_State* L) {
    Script* s = Of(L);
    const sco_widgets_v1* t = s->alive ? WidgetsTable(s) : NULL;
    if (!WIDGET_HAS(t, set_panel)) return PushResult(L, SCO_UNAVAILABLE);
    const char *id = NULL, *title = NULL;
    sco_result r = WidgetString(L, 1, SCO_WIDGETS_MAX_ID, SCO_BAD_ARG, &id);
    if (r == SCO_OK) r = WidgetString(L, 2, SCO_WIDGETS_MAX_TITLE, SCO_BAD_ARG, &title);
    if (r != SCO_OK || !lua_istable(L, 3)) return PushResult(L, SCO_BAD_ARG);
    if (!lua_isnoneornil(L, 4) && !lua_isfunction(L, 4)) return PushResult(L, SCO_BAD_ARG);
    size_t count = 0;
    r = WidgetSequence(L, 3, SCO_WIDGETS_MAX_NODES, &count);
    if (r != SCO_OK) return PushResult(L, r);
    if (!count) return PushResult(L, SCO_BAD_ARG);
    luaL_checkstack(L, 8, "widget tree");
    lua_settop(L, 4);
    sco_widget_node* nodes = (sco_widget_node*)lua_newuserdatauv(L, count * sizeof(*nodes), 0);
    memset(nodes, 0, count * sizeof(*nodes));
    lua_createtable(L, (int)count, 0);
    const int roots = lua_gettop(L);
    for (size_t i = 0; i < count; ++i) {
        sco_lua_step(L, 1);
        lua_rawgeti(L, 3, (lua_Integer)i + 1);
        if (!lua_istable(L, -1)) return PushResult(L, SCO_BAD_ARG);
        const int node = lua_gettop(L);
        nodes[i].size = sizeof(*nodes);
        r = WidgetNode(L, node, roots, &nodes[i]);
        if (r != SCO_OK) return PushResult(L, r);
        lua_pop(L, 1);
    }
    WidgetCtx* old = WidgetFind(s, id);
    WidgetCtx* ctx = old;
    const int callback = lua_isfunction(L, 4);
    if (callback && !ctx) for (uint32_t i = 0; i < SCO_WIDGETS_MAX_PANELS; ++i)
        if (!s->widgets[i].id[0]) { ctx = &s->widgets[i]; break; }
    if (callback && !ctx) return PushResult(L, SCO_TOO_MANY);
    int ref = LUA_NOREF;
    if (callback) { lua_pushvalue(L, 4); ref = luaL_ref(L, LUA_REGISTRYINDEX); }
    r = t->set_panel(s->self, id, title, nodes, (uint32_t)count, sizeof(*nodes),
                     callback ? WidgetThunk : NULL, callback ? ctx : NULL);
    if (r == SCO_OK) {
        WidgetForget(L, old);
        if (callback) { ctx->s = s; ctx->ref = ref; Copy(ctx->id, sizeof(ctx->id), id); }
    } else if (callback) luaL_unref(L, LUA_REGISTRYINDEX, ref);
    CheckBudget(L);
    return PushResult(L, r);
}

static int L_widgets_remove_panel(lua_State* L) {
    Script* s = Of(L);
    const sco_widgets_v1* t = s->alive ? WidgetsTable(s) : NULL;
    if (!WIDGET_HAS(t, remove_panel)) return PushResult(L, SCO_UNAVAILABLE);
    const char* id = NULL;
    sco_result r = WidgetString(L, 1, SCO_WIDGETS_MAX_ID, SCO_BAD_ARG, &id);
    if (r == SCO_OK) r = t->remove_panel(s->self, id);
    if (r == SCO_OK) WidgetForget(L, WidgetFind(s, id));
    CheckBudget(L);
    return PushResult(L, r);
}

/* setmetatable without finalizers: Lua runs __gc with hooks off, out of the budget's reach. */
static int L_setmetatable(lua_State* L) {
    if (lua_istable(L, 2)) {
        lua_pushliteral(L, "__gc");
        if (lua_rawget(L, 2) != LUA_TNIL) return luaL_error(L, "__gc is not allowed in sco-lua");
        lua_pop(L, 1);
    }
    lua_pushvalue(L, lua_upvalueindex(1));
    lua_insert(L, 1);
    lua_call(L, lua_gettop(L) - 1, 1);
    return 1;
}

/* ---- setup ---------------------------------------------------------------------------------- */

typedef struct LoadCall { const char* source; size_t size; const char* chunk; } LoadCall;

static int SetupBody(lua_State* L) {
    LoadCall* k = (LoadCall*)lua_touserdata(L, 1);
    Script* s = Of(L);

    luaL_requiref(L, LUA_GNAME, luaopen_base, 1);
    lua_pop(L, 1);
    luaL_requiref(L, LUA_STRLIBNAME, luaopen_string, 1);
    lua_pop(L, 1);
    luaL_requiref(L, LUA_TABLIBNAME, luaopen_table, 1);
    lua_pop(L, 1);
    luaL_requiref(L, LUA_MATHLIBNAME, luaopen_math, 1);
    lua_pop(L, 1);
    luaL_requiref(L, LUA_UTF8LIBNAME, luaopen_utf8, 1);
    lua_pop(L, 1);

    /* Globals: keep the safe base functions and the four libraries, drop the rest. */
    static const char* const keep[] = {
        "assert", "error", "ipairs", "next", "pairs", "pcall", "select", "tonumber", "tostring",
        "type", "xpcall", "rawequal", "rawget", "rawlen", "rawset", "getmetatable", "_G",
        "_VERSION", LUA_STRLIBNAME, LUA_TABLIBNAME, LUA_MATHLIBNAME, LUA_UTF8LIBNAME, NULL };
    lua_pushglobaltable(L);
    const int g = lua_gettop(L);
    lua_getfield(L, g, "setmetatable");
    lua_pushcclosure(L, L_setmetatable, 1);
    lua_newtable(L);                                  /* names to drop */
    const int drop = lua_gettop(L);
    lua_pushnil(L);
    while (lua_next(L, g)) {
        lua_pop(L, 1);
        int kept = 0;
        if (lua_type(L, -1) == LUA_TSTRING)
            for (int i = 0; keep[i] && !kept; ++i) kept = strcmp(lua_tostring(L, -1), keep[i]) == 0;
        if (!kept) { lua_pushvalue(L, -1); lua_rawseti(L, drop, (lua_Integer)lua_rawlen(L, drop) + 1); }
    }
    for (lua_Integer i = 1, n = (lua_Integer)lua_rawlen(L, drop); i <= n; ++i) {
        lua_rawgeti(L, drop, i);
        lua_pushnil(L);
        lua_rawset(L, g);
    }
    lua_pop(L, 1);                                    /* drop */
    lua_setfield(L, g, "setmetatable");               /* the __gc-refusing wrapper */
    lua_pushcfunction(L, L_print);
    lua_setfield(L, g, "print");
    lua_getfield(L, g, LUA_STRLIBNAME);
    lua_pushnil(L);
    lua_setfield(L, -2, "dump");                      /* bytecode out: nothing can load it, but no */
    lua_pop(L, 1);
    lua_getfield(L, g, LUA_MATHLIBNAME);              /* math.random stays; seeding is harmless */
    lua_pop(L, 1);

    static const luaL_Reg fns[] = {
        { "host_version", L_host_version }, { "has", L_has }, { "status", L_status },
        { "log", L_log }, { "subscribe", L_subscribe }, { "unsubscribe", L_unsubscribe },
        { "run_on_game_thread", L_run_on_game_thread }, { "register_command", L_register_command },
        { "invoke", L_invoke }, { "list_commands", L_list_commands },
        { "bind_hotkey", L_bind_hotkey }, { "unbind_hotkey", L_unbind_hotkey }, { NULL, NULL } };
    luaL_newlib(L, fns);
    lua_pushinteger(L, s->api->major);
    lua_setfield(L, -2, "api_major");
    lua_pushinteger(L, s->api->minor);
    lua_setfield(L, -2, "api_minor");
    StoreTypes(L);
    {                                                 /* sco.store: always there; unavailable without sco.storage */
        static const luaL_Reg stfns[] = {
            { "available", L_store_available }, { "get", L_store_get }, { "put", L_store_put },
            { "delete", L_store_delete }, { "keys", L_store_keys }, { "begin", L_store_begin },
            { "commit", L_store_commit }, { "rollback", L_store_rollback }, { "exec", L_store_exec },
            { "sql", L_store_sql }, { NULL, NULL } };
        luaL_newlib(L, stfns);
        lua_setfield(L, -2, "store");
    }
    {                                                 /* sco.settings: always there; unavailable without the service */
        static const luaL_Reg sefns[] = { { "get", L_settings_get }, { NULL, NULL } };
        luaL_newlib(L, sefns);
        lua_setfield(L, -2, "settings");
    }
    {
        static const luaL_Reg wfns[] = {
            { "available", L_widgets_available }, { "set_panel", L_widgets_set_panel },
            { "remove_panel", L_widgets_remove_panel }, { NULL, NULL } };
        luaL_newlib(L, wfns);
        lua_setfield(L, -2, "widgets");
    }
    {
        luaL_Buffer source;
        luaL_buffinit(L, &source);
        for (size_t i = 0; i < sizeof(kPanelBuilderSource) / sizeof(kPanelBuilderSource[0]); ++i)
            luaL_addstring(&source, kPanelBuilderSource[i]);
        luaL_pushresult(&source);
        size_t size = 0;
        const char* text = lua_tolstring(L, -1, &size);
        const int st = luaL_loadbufferx(L, text, size, "@sco.panel_builder", "t");
        if (st != LUA_OK) return lua_error(L);
        lua_call(L, 0, 1);
        lua_remove(L, -2);
        lua_setfield(L, -2, "panel_builder");
    }
    for (int i = 0; i < g_nextensions; ++i)          /* optional adapters (sco_lua_ext.h): sco.datacore, sco.game */
        g_extensions[i]->open(L);
    lua_setfield(L, g, "sco");

    lua_newtable(L);
    lua_setfield(L, LUA_REGISTRYINDEX, SUBS_KEY);

    lua_pushfstring(L, "@%s", k->chunk);
    const int st = luaL_loadbufferx(L, k->source, k->size, lua_tostring(L, -1), "t");
    if (st != LUA_OK) return lua_error(L);
    lua_call(L, 0, 0);
    return 0;
}

static void Close(Script* s) {
    while (s->tasks) { Task* t = s->tasks; s->tasks = t->next; free(t); }
    if (s->L) lua_close(s->L);
    memset(s, 0, sizeof(*s));
}

sco_result sco_lua_load(const sco_api* api, sco_plugin* self, const char* chunkname,
                        const char* source, size_t size, char* err, size_t err_size) {
    if (err && err_size) err[0] = 0;
    if (!api || !self || !chunkname || !source) { Copy(err, err_size, "null argument"); return SCO_BAD_ARG; }
    if (size > SCO_LUA_MAX_SOURCE) { Copy(err, err_size, "script too big"); return SCO_BAD_ARG; }
    if (Find(self)) { Copy(err, err_size, "this plugin already has a script"); return SCO_BAD_ARG; }
    Script* s = NULL;
    for (int i = 0; i < SCO_LUA_MAX_SCRIPTS && !s; ++i) if (!g_scripts[i].used) s = &g_scripts[i];
    if (!s) { Copy(err, err_size, "too many scripts"); return SCO_TOO_MANY; }
    memset(s, 0, sizeof(*s));
    s->used = 1;
    s->api = api;
    s->self = self;
    Copy(s->chunk, sizeof(s->chunk), chunkname);
    s->L = lua_newstate(Alloc, s);
    if (!s->L) { Close(s); Copy(err, err_size, "out of memory"); return SCO_TOO_MANY; }
    *(Script**)lua_getextraspace(s->L) = s;
    s->alive = 1;
    LoadCall k = { source, size, s->chunk };
    const int st = Enter(s, SetupBody, &k);
    if (st == LUA_OK && s->alive) return SCO_OK;
    sco_result r = SCO_BAD_ARG;
    if (st == LUA_OK) {
        Copy(err, err_size, "script disabled while loading");   /* the log says why */
    } else {
        char num[48];
        Copy(err, err_size, ErrorText(s->L, num, sizeof(num)));
        if (st == LUA_ERRMEM) r = SCO_TOO_MANY;
    }
    for (int i = 0; i < SCO_LUA_MAX_EVENTS; ++i)
        if (s->events[i].live) api->unsubscribe(self, s->events[i].name, sco_lua_event_);
    Close(s);
    return r;
}

void sco_lua_unload(sco_plugin* self) {
    Script* s = Find(self);
    if (s && !s->depth) Close(s);
}

int sco_lua_alive(const sco_plugin* self) {
    const Script* s = Find(self);
    return s && s->alive;
}
