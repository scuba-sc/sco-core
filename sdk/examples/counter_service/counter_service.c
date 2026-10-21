/*
 * counter_service: the provider half of the sco SDK's modular example pair.
 *
 * One feature, one plugin: it owns a counter and shares it with other plugins only through a
 * versioned service table, "counter_service.counter" 1.0 (counter_service.h). It shows:
 *   - a size-first table published once with provide_service, under "<plugin id>.<name>",
 *   - a check that the host's sco_api is new enough (1.1) before calling provide_service,
 *   - argument checks in the provider (a fault in a provider is blamed on the provider),
 *   - one command for people, counter_service.value, beside the table for plugins.
 * counter_panel is the consumer. Build: see README.md. GPL-3.0, like sco-core.
 */
#include <stdio.h>
#include <string.h>

#include "counter_service.h"

static const sco_api* api;
static sco_plugin*    me;
static int64_t        counter; /* the state this plugin owns; nobody else writes it */

static const sco_plugin_info info = {
    sizeof(sco_plugin_info), SCO_API_MAJOR, SCO_API_MINOR,
    "counter_service", /* must equal `id` in plugin.ini; the prefix of its names */
    "1.0.0",
    "sco SDK example",
};

/* ---- the service table ------------------------------------------------- */

static sco_result counter_add(int64_t delta, int64_t* out_value) {
    if ((delta > 0 && counter > INT64_MAX - delta) || (delta < 0 && counter < INT64_MIN - delta))
        return SCO_BAD_ARG; /* would overflow: leave the counter as it is */
    counter += delta;
    if (out_value) *out_value = counter;
    return SCO_OK;
}

static sco_result counter_get(int64_t* out_value) {
    if (!out_value) return SCO_BAD_ARG;
    *out_value = counter;
    return SCO_OK;
}

/* static and const: it lives as long as the DLL. Once the table has been handed out, the host
 * refuses to unload or reload this plugin for the rest of the session (docs/api-v1.md, Services). */
static const counter_service_v1 table = {
    sizeof(counter_service_v1), 0, counter_add, counter_get,
};

/* ---- a command for people ---------------------------------------------- */

/* counter_service.value: replies "counter is <n>". Runs on the game thread. */
static sco_result value(const sco_arg* args, uint32_t nargs, void* ctx, char* reply, uint32_t reply_size) {
    (void)args; (void)ctx;
    if (nargs != 0) return SCO_BAD_ARG;
    snprintf(reply, reply_size, "counter is %lld", (long long)counter);
    return SCO_OK;
}

/* ---- exports ------------------------------------------------------------ */

SCO_EXPORT const sco_plugin_info* sco_plugin_query(void) { return &info; }

SCO_EXPORT sco_result sco_plugin_load(const sco_api* a, sco_plugin* self) {
    sco_command cmd;
    sco_result r;

    api = a;
    me = self;
    counter = 0;

    /* provide_service arrived in sco_api 1.1. On an older host this plugin can't do its one job,
     * so it says why and declines to load; its dependents are then refused with the reason. */
    if (api->size < offsetof(sco_api, provide_service) + sizeof(api->provide_service) || !api->provide_service) {
        api->log(me, SCO_LOG_ERROR, "this host has no provide_service (sco_api 1.0); counter_service needs 1.1");
        return SCO_UNAVAILABLE;
    }
    r = api->provide_service(me, COUNTER_SERVICE_NAME, COUNTER_SERVICE_VERSION_1_0, &table);
    if (r != SCO_OK) {
        api->log(me, SCO_LOG_ERROR, "could not publish counter_service.counter");
        return r;
    }

    memset(&cmd, 0, sizeof cmd);
    cmd.size         = sizeof cmd;
    cmd.name         = "counter_service.value";
    cmd.title        = "Counter value";
    cmd.help         = "Shows the shared counter";
    cmd.capability   = NULL; /* game-neutral: always available */
    cmd.args         = NULL;
    cmd.nargs        = 0;
    cmd.arg_def_size = sizeof(sco_arg_def);
    cmd.fn           = value;
    cmd.ctx          = NULL;
    if ((r = api->register_command(me, &cmd)) != SCO_OK) return r;

    api->log(me, SCO_LOG_INFO, "published counter_service.counter 1.0");
    return SCO_OK;
}

/* The host withdraws the service and the command itself, and unloads dependents first. The
 * counter is static, so there is nothing to free. */
SCO_EXPORT void sco_plugin_unload(void) {}
