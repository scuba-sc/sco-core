/* SPDX-License-Identifier: GPL-3.0-only
 * Independent public-header-only native module, synthetic input only. */
#include "sc_loadout.h"
#include <stdio.h>
#include <stddef.h>
#define MEMBER(t, type, m) ((t) && (t)->size >= offsetof(type, m) + sizeof((t)->m) && (t)->m)
static const sco_api* api;
static sco_plugin* self;
static unsigned respawns;
/* Only sc_loadout.h is copied for this module: the event name comes from it, payload is unused. */
static void OnRespawn(const char* event, const void* data, void* ctx) { (void)event; (void)data; (void)ctx; ++respawns; }
static sco_result RespawnCount(const sco_arg* a, uint32_t n, void* ctx, char* reply, uint32_t size) {
    (void)a; (void)n; (void)ctx;
    snprintf(reply, size, "respawns=%u", respawns);
    return SCO_OK;
}
static sco_result Apply(const sco_arg* a, uint32_t n, void* ctx, char* reply, uint32_t size) {
    const void* out = NULL;
    const sc_loadout_v1* table;
    sco_result r;
    const sc_loadout_item items[] = {{SC_LOADOUT_SLOT_HEAD, 0, "synthetic_helmet"},
                                    {999, 0, "synthetic_unknown"},
                                    {SC_LOADOUT_SLOT_TORSO, 0, "synthetic_missing"}};
    const sc_loadout_request req = {sizeof(req), 3, sizeof(sc_loadout_item), 0, items};
    sc_loadout_report report = {0};
    (void)a; (void)n; (void)ctx;
    r = api->query_service(SC_LOADOUT_SERVICE_NAME, SC_LOADOUT_SERVICE_VERSION, &out);
    if (r != SCO_OK) return r;
    table = (const sc_loadout_v1*)out;
    if (!MEMBER(table, sc_loadout_v1, apply)) return SCO_UNAVAILABLE;
    report.size = sizeof(report);
    r = table->apply(self, &req, &report);
    snprintf(reply, size, "count=%u applied=%u slots=%u,%u,%u", report.count, report.applied,
             (unsigned)report.slots[0].status, (unsigned)report.slots[1].status, (unsigned)report.slots[2].status);
    return r;
}
SCO_EXPORT const sco_plugin_info* sco_plugin_query(void) {
    static const sco_plugin_info info = {sizeof(info), SCO_API_MAJOR, SCO_API_MINOR, "loadout_native", "1.0.0", "scubamount"};
    return &info;
}
SCO_EXPORT sco_result sco_plugin_load(const sco_api* host, sco_plugin* owner) {
    sco_command command = {0};
    if (!owner || !MEMBER(host, sco_api, query_service) || !MEMBER(host, sco_api, register_command)) return SCO_UNAVAILABLE;
    api = host; self = owner;
    command.size = sizeof(command); command.name = "loadout_native.apply";
    command.title = "Synthetic loadout"; command.fn = Apply;
    {
        const sco_result r = api->register_command(self, &command);
        if (r != SCO_OK) return r;
    }
    command.name = "loadout_native.respawn_count"; command.title = "Synthetic respawn count"; command.fn = RespawnCount;
    {
        const sco_result r = api->register_command(self, &command);
        if (r != SCO_OK) return r;
    }
    return MEMBER(host, sco_api, subscribe) ?
        api->subscribe(self, SC_GAME_EVENT_LOADOUT_RESPAWN_COMPLETED, OnRespawn, NULL) : SCO_UNAVAILABLE;
}
SCO_EXPORT void sco_plugin_unload(void) {
    if (api && self && MEMBER(api, sco_api, unsubscribe))
        api->unsubscribe(self, SC_GAME_EVENT_LOADOUT_RESPAWN_COMPLETED, OnRespawn);
    api = NULL; self = NULL;
}
