/*
 * actor_stats_probe: the in-game test for game.actor_stats and game.actor_stats_write.
 * Scaffolded with sco-new-plugin (sco-core MCP generate_plugin_scaffold), then filled in.
 *
 * - Every interval (default 1000 ms, on the tick) it logs all nine named stats of your player in
 *   one line: "stats id=<entity> hunger=<v> thirst=<v> ...", or the result code where a read
 *   failed. A value that changed since the last line is marked with '*'.
 * - Writes are off twice over: the game pack's capability game.actor_stats.write (product switch
 *   or SCO_ACTOR_STATS_WRITE=1, plus offline admission) and this plugin's own "Arm writes" toggle.
 *   With both on, the panel's buttons or the commands below change one stat and log the result,
 *   the write mode (authoritative / local_only / discarded) and the value the game pack read back
 *   right after the write.
 *
 * Commands (game thread):
 *   actor_stats_probe.dump                       log every stat now
 *   actor_stats_probe.arm on                     arm (1) or disarm (0) writes
 *   actor_stats_probe.adjust stat delta          e.g. "hunger 10"
 *   actor_stats_probe.set stat value             e.g. "body_temp 37"
 *   actor_stats_probe.interval ms                logging interval, 100..60000
 *   actor_stats_probe.modes                      log what the game would do with a write, per stat
 * stat names: hunger thirst body_temp suit_temp stun distortion oxygen body_rad suit_rad
 *
 * Nothing here has been run in game. License: GPL-3.0 (built against sco_api.h).
 */
#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include "sco_widgets.h"
#include "sc_actors.h"
#include "sc_actor_stats.h"
#include "sc_actor_stats_write.h"

static const sco_api* api;
static sco_plugin*    me;

static const sco_plugin_info info = {
    sizeof(sco_plugin_info), SCO_API_MAJOR, SCO_API_MINOR, "actor_stats_probe", "0.1.0", "sco-core example",
};

#define STAT_COUNT 9u
static const char* const stat_names[STAT_COUNT] = {
    "hunger", "thirst", "body_temp", "suit_temp", "stun", "distortion", "oxygen", "body_rad", "suit_rad",
};
static const uint32_t stat_ids[STAT_COUNT] = {
    SC_ACTOR_STAT_HUNGER, SC_ACTOR_STAT_THIRST, SC_ACTOR_STAT_BODY_TEMPERATURE, SC_ACTOR_STAT_SUIT_TEMPERATURE,
    SC_ACTOR_STAT_STUN, SC_ACTOR_STAT_DISTORTION, SC_ACTOR_STAT_OXYGEN, SC_ACTOR_STAT_BODY_RADIATION,
    SC_ACTOR_STAT_SUIT_RADIATION,
};

static const sc_actors_v1*            actors;
static const sc_actor_stats_v1*       stats;
static const sc_actor_stats_write_v1* writer;

static uint32_t interval_ms = 1000;
static uint32_t last_log_ms;
static int      have_last;
static float    last_values[STAT_COUNT];
static int      armed;          /* the plugin's own write arm, off at load */
static int      selected;       /* panel: index into stat_names */
static double   amount = 10.0;  /* panel: the step for +/- and the target for "Set" */

static void logs(sco_log_level level, const char* text) { api->log(me, level, text); }

static int api_covers(size_t offset, size_t size) { return api && api->size >= offset + size; }

static const void* lookup(const char* name, uint32_t version) {
    const void* table = NULL;
    if (!api_covers(offsetof(sco_api, query_service), sizeof(api->query_service)) || !api->query_service) return NULL;
    if (api->query_service(name, version, &table) != SCO_OK || !table || *(const uint32_t*)table < 8) return NULL;
    return table;
}

static void lookup_services(void) {
    char line[200];
    actors = (const sc_actors_v1*)lookup(SC_ACTORS_NAME, SC_ACTORS_VERSION_1_0);
    stats  = (const sc_actor_stats_v1*)lookup(SC_ACTOR_STATS_NAME, SC_ACTOR_STATS_VERSION_1_0);
    writer = (const sc_actor_stats_write_v1*)lookup(SC_ACTOR_STATS_WRITE_NAME, SC_ACTOR_STATS_WRITE_VERSION_1_0);
    snprintf(line, sizeof line, "services: game.actors %s, game.actor_stats %s, game.actor_stats_write %s; "
             "caps: read %s, write %s", actors ? "found" : "missing", stats ? "found" : "missing",
             writer ? "found" : "missing", api->has && api->has("game.actor_stats.read") ? "ON" : "off",
             api->has && api->has("game.actor_stats.write") ? "ON" : "off");
    logs(SCO_LOG_INFO, line);
}

/* Your player's entity id, 0 when you haven't spawned or game.actors is missing. */
static uint64_t player_id(void) {
    uint64_t actor = 0, entity = 0;
    if (!actors || actors->size < offsetof(sc_actors_v1, spawn_npc) || !actors->local_player) return 0;
    return actors->local_player(&actor, &entity) == SCO_OK ? entity : 0;
}

static void log_stats(int force) {
    char     line[640];
    size_t   n;
    uint32_t i;
    int      changed = 0;
    uint64_t id = player_id();
    if (!stats) { if (force) logs(SCO_LOG_WARN, "game.actor_stats is not available"); return; }
    if (!id) { if (force) logs(SCO_LOG_INFO, "not spawned yet: no stats"); return; }
    n = (size_t)snprintf(line, sizeof line, "stats id=%llu", (unsigned long long)id);
    for (i = 0; i < STAT_COUNT && n < sizeof line; ++i) {
        float      v = 0;
        sco_result r = sc_actor_stats_read(stats, id, stat_ids[i], &v);
        if (r == SCO_OK) {
            int diff = have_last && v != last_values[i];
            changed |= diff;
            last_values[i] = v;
            n += (size_t)snprintf(line + n, sizeof line - n, " %s=%.4f%s", stat_names[i], (double)v, diff ? "*" : "");
        } else {
            n += (size_t)snprintf(line + n, sizeof line - n, " %s=n/a(%u)", stat_names[i], (unsigned)r);
        }
    }
    have_last = 1;
    (void)changed;
    logs(SCO_LOG_INFO, line);
}

static int stat_index(const char* name) {
    uint32_t i;
    if (!name) return -1;
    for (i = 0; i < STAT_COUNT; ++i)
        if (strcmp(name, stat_names[i]) == 0) return (int)i;
    return -1;
}

static const char* mode_name(uint32_t mode) {
    return mode == SC_ACTOR_STAT_WRITE_AUTHORITATIVE ? "authoritative"
         : mode == SC_ACTOR_STAT_WRITE_LOCAL_ONLY ? "local_only"
         : mode == SC_ACTOR_STAT_WRITE_DISCARDED ? "discarded" : "?";
}

/* One write; the game pack reads the stat back right after it. is_set: number is a target, else a delta. */
static sco_result do_write(int index, double number, int is_set, char* reply, uint32_t reply_size) {
    char       line[240];
    float      after = 0;
    uint32_t   mode = SC_ACTOR_STAT_WRITE_DISCARDED;
    sco_result r, rm;
    uint64_t   id = player_id();
    if (!armed) { snprintf(reply, reply_size, "writes are not armed (actor_stats_probe.arm 1)"); return SCO_UNAVAILABLE; }
    if (index < 0 || !isfinite(number)) return SCO_BAD_ARG;
    if (!writer) { snprintf(reply, reply_size, "game.actor_stats_write is not available"); return SCO_UNAVAILABLE; }
    if (!id) { snprintf(reply, reply_size, "not spawned yet"); return SCO_NOT_FOUND; }
    rm = sc_actor_stats_write_mode(writer, me, id, stat_ids[index], &mode);
    r = is_set ? sc_actor_stats_set(writer, me, id, stat_ids[index], (float)number, &after)
               : sc_actor_stats_adjust(writer, me, id, stat_ids[index], (float)number, &after);
    snprintf(line, sizeof line, "%s %s %.4f -> result %u (mode %s, result %u); read back %s=%.4f", is_set ? "set" : "adjust",
             stat_names[index], number, (unsigned)r, rm == SCO_OK ? mode_name(mode) : "n/a", (unsigned)rm, stat_names[index],
             (double)after);
    logs(r == SCO_OK ? SCO_LOG_INFO : SCO_LOG_WARN, line);
    if (reply && reply_size) snprintf(reply, reply_size, "%s", line);
    return r;
}

/* Logs, per stat, what the game would do with a change right now. Writes nothing. */
static void log_modes(void) {
    char     line[400];
    size_t   n;
    uint32_t i;
    uint64_t id = player_id();
    if (!writer) { logs(SCO_LOG_WARN, "game.actor_stats_write is not available"); return; }
    if (!id) { logs(SCO_LOG_INFO, "not spawned yet: no write modes"); return; }
    n = (size_t)snprintf(line, sizeof line, "write modes id=%llu", (unsigned long long)id);
    for (i = 0; i < STAT_COUNT && n < sizeof line; ++i) {
        uint32_t   mode = SC_ACTOR_STAT_WRITE_DISCARDED;
        sco_result r = sc_actor_stats_write_mode(writer, me, id, stat_ids[i], &mode);
        if (r == SCO_OK)
            n += (size_t)snprintf(line + n, sizeof line - n, " %s=%s", stat_names[i], mode_name(mode));
        else
            n += (size_t)snprintf(line + n, sizeof line - n, " %s=n/a(%u)", stat_names[i], (unsigned)r);
    }
    logs(SCO_LOG_INFO, line);
}

static void on_ready(const char* event, const void* data, void* ctx) {
    (void)event; (void)data; (void)ctx;
    lookup_services();
    api->status(me, "Actor stats probe: logging");
}

static void on_tick(const char* event, const void* data, void* ctx) {
    uint32_t now;
    (void)event; (void)ctx;
    if (!data) return;
    now = *(const uint32_t*)data;
    if (have_last && now - last_log_ms < interval_ms) return;
    last_log_ms = now;
    log_stats(0);
}

/* ---- commands ---------------------------------------------------------------------------------- */

static sco_result cmd_dump(const sco_arg* args, uint32_t nargs, void* ctx, char* reply, uint32_t reply_size) {
    (void)args; (void)nargs; (void)ctx;
    log_stats(1);
    if (reply && reply_size) snprintf(reply, reply_size, "logged");
    return SCO_OK;
}

static sco_result cmd_arm(const sco_arg* args, uint32_t nargs, void* ctx, char* reply, uint32_t reply_size) {
    (void)ctx;
    if (nargs != 1 || args[0].type != SCO_ARG_BOOL) return SCO_BAD_ARG;
    armed = args[0].v.i != 0;
    if (reply && reply_size) snprintf(reply, reply_size, "writes %s", armed ? "armed" : "disarmed");
    logs(SCO_LOG_INFO, armed ? "writes armed" : "writes disarmed");
    return SCO_OK;
}

static sco_result cmd_write(const sco_arg* args, uint32_t nargs, void* ctx, char* reply, uint32_t reply_size) {
    if (nargs != 2 || args[0].type != SCO_ARG_STRING || args[1].type != SCO_ARG_FLOAT) return SCO_BAD_ARG;
    return do_write(stat_index(args[0].v.s), args[1].v.f, ctx != NULL, reply, reply_size);
}

static sco_result cmd_interval(const sco_arg* args, uint32_t nargs, void* ctx, char* reply, uint32_t reply_size) {
    (void)ctx;
    if (nargs != 1 || args[0].type != SCO_ARG_INT || args[0].v.i < 100 || args[0].v.i > 60000) return SCO_BAD_ARG;
    interval_ms = (uint32_t)args[0].v.i;
    if (reply && reply_size) snprintf(reply, reply_size, "interval %u ms", (unsigned)interval_ms);
    return SCO_OK;
}

static sco_result add_command(const char* name, const char* title, const char* help, const sco_arg_def* defs,
                              uint32_t nargs, sco_command_fn fn, void* ctx) {
    sco_command cmd;
    memset(&cmd, 0, sizeof cmd);
    cmd.size = sizeof cmd;
    cmd.name = name;
    cmd.title = title;
    cmd.help = help;
    cmd.args = defs;
    cmd.nargs = nargs;
    cmd.arg_def_size = sizeof(sco_arg_def);
    cmd.fn = fn;
    cmd.ctx = ctx;
    return api->register_command(me, &cmd);
}

static const sco_arg_def arm_args[] = { { "on", SCO_ARG_BOOL, 0, "1 arms writes, 0 disarms" } };
static const sco_arg_def adjust_args[] = {
    { "stat", SCO_ARG_STRING, 0, "hunger thirst body_temp suit_temp stun distortion oxygen body_rad suit_rad" },
    { "delta", SCO_ARG_FLOAT, 0, "added to the stat" },
};
static const sco_arg_def set_args[] = {
    { "stat", SCO_ARG_STRING, 0, "hunger thirst body_temp suit_temp stun distortion oxygen body_rad suit_rad" },
    { "value", SCO_ARG_FLOAT, 0, "the target value" },
};
static const sco_arg_def interval_args[] = { { "ms", SCO_ARG_INT, 0, "100..60000" } };

static sco_result cmd_modes(const sco_arg* args, uint32_t nargs, void* ctx, char* reply, uint32_t reply_size) {
    (void)args; (void)nargs; (void)ctx;
    log_modes();
    if (reply && reply_size) snprintf(reply, reply_size, "logged");
    return SCO_OK;
}
static int set_marker;   /* its address marks cmd_write's ctx as "set" */

/* ---- panel ------------------------------------------------------------------------------------- */

#define PANEL_ID "actor_stats_probe.main"
enum { N_ROOT = 1, N_ARM, N_STAT, N_AMOUNT, N_MINUS, N_PLUS, N_SET, N_DUMP, N_MODES, N_COUNT = N_MODES };
static const sco_widgets_v1* widgets;
static void show_panel(void);

static void on_widget(const char* panel, const sco_widget_event* event, void* ctx) {
    char reply[200];
    (void)panel; (void)ctx;
    if (!event || event->size < sizeof(*event)) return;
    switch (event->node_id) {
    case N_ARM: armed = event->value != 0; logs(SCO_LOG_INFO, armed ? "writes armed" : "writes disarmed"); break;
    case N_STAT: if (event->index >= 0 && event->index < (int32_t)STAT_COUNT) selected = event->index; break;
    case N_AMOUNT: amount = event->value; break;
    case N_MINUS: do_write(selected, -amount, 0, reply, sizeof reply); break;
    case N_PLUS: do_write(selected, amount, 0, reply, sizeof reply); break;
    case N_SET: do_write(selected, amount, 1, reply, sizeof reply); break;
    case N_DUMP: log_stats(1); break;
    case N_MODES: log_modes(); break;
    default: break;
    }
}

static void show_panel(void) {
    sco_widget_node nodes[N_COUNT];
    uint32_t        i;
    widgets = (const sco_widgets_v1*)lookup(SCO_WIDGETS_NAME, SCO_WIDGETS_VERSION_1_0);
    if (!widgets || widgets->size < offsetof(sco_widgets_v1, remove_panel) + sizeof(widgets->remove_panel) ||
        !widgets->set_panel || !widgets->remove_panel) {
        widgets = NULL;
        logs(SCO_LOG_INFO, "sco.widgets not available: commands only");
        return;
    }
    memset(nodes, 0, sizeof nodes);
    for (i = 0; i < N_COUNT; ++i) { nodes[i].size = sizeof(nodes[i]); nodes[i].id = i + 1; nodes[i].parent = N_ROOT; }
    nodes[N_ROOT - 1].kind = SCO_WIDGET_COLUMN; nodes[N_ROOT - 1].parent = 0;
    nodes[N_ARM - 1].kind = SCO_WIDGET_TOGGLE; nodes[N_ARM - 1].label = "Arm writes"; nodes[N_ARM - 1].value = armed;
    nodes[N_STAT - 1].kind = SCO_WIDGET_COMBO; nodes[N_STAT - 1].label = "Stat";
    nodes[N_STAT - 1].items = stat_names; nodes[N_STAT - 1].item_count = STAT_COUNT; nodes[N_STAT - 1].index = selected;
    nodes[N_AMOUNT - 1].kind = SCO_WIDGET_SLIDER; nodes[N_AMOUNT - 1].label = "Amount (step or target)";
    nodes[N_AMOUNT - 1].value = amount; nodes[N_AMOUNT - 1].minimum = -1000.0; nodes[N_AMOUNT - 1].maximum = 1000.0;
    nodes[N_MINUS - 1].kind = SCO_WIDGET_BUTTON; nodes[N_MINUS - 1].label = "Adjust -amount";
    nodes[N_PLUS - 1].kind = SCO_WIDGET_BUTTON; nodes[N_PLUS - 1].label = "Adjust +amount";
    nodes[N_SET - 1].kind = SCO_WIDGET_BUTTON; nodes[N_SET - 1].label = "Set to amount";
    nodes[N_DUMP - 1].kind = SCO_WIDGET_BUTTON; nodes[N_DUMP - 1].label = "Log stats now";
    nodes[N_MODES - 1].kind = SCO_WIDGET_BUTTON; nodes[N_MODES - 1].label = "Log write modes";
    if (widgets->set_panel(me, PANEL_ID, "Actor stats probe", nodes, N_COUNT, sizeof(nodes[0]), on_widget, NULL) != SCO_OK) {
        logs(SCO_LOG_WARN, "set_panel refused: commands only");
        widgets = NULL;
    }
}

/* ---- load / unload ----------------------------------------------------------------------------- */

SCO_EXPORT const sco_plugin_info* sco_plugin_query(void) { return &info; }

SCO_EXPORT sco_result sco_plugin_load(const sco_api* a, sco_plugin* self) {
    sco_result r;
    api = a;
    me = self;
    if ((r = add_command("actor_stats_probe.dump", "Log stats now", "Logs every stat of your player", NULL, 0, cmd_dump, NULL)) != SCO_OK) return r;
    if ((r = add_command("actor_stats_probe.arm", "Arm writes", "Arms or disarms this plugin's writes", arm_args, 1, cmd_arm, NULL)) != SCO_OK) return r;
    if ((r = add_command("actor_stats_probe.adjust", "Adjust a stat", "Adds delta to one stat of your player", adjust_args, 2, cmd_write, NULL)) != SCO_OK) return r;
    if ((r = add_command("actor_stats_probe.set", "Set a stat", "Moves one stat of your player to value", set_args, 2, cmd_write, &set_marker)) != SCO_OK) return r;
    if ((r = add_command("actor_stats_probe.modes", "Log write modes", "Logs what the game would do with a write, per stat", NULL, 0, cmd_modes, NULL)) != SCO_OK) return r;
    if ((r = add_command("actor_stats_probe.interval", "Log interval", "Logging interval in ms", interval_args, 1, cmd_interval, NULL)) != SCO_OK) return r;
    if ((r = api->subscribe(me, "game.ready", on_ready, NULL)) != SCO_OK) return r;
    if ((r = api->subscribe(me, "tick", on_tick, NULL)) != SCO_OK) return r;
    show_panel();
    logs(SCO_LOG_INFO, "loaded (writes disarmed)");
    return SCO_OK;
}

SCO_EXPORT void sco_plugin_unload(void) {
    if (widgets) (void)widgets->remove_panel(me, PANEL_ID);
    widgets = NULL;
}
