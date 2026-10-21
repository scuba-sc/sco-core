/*
 * counter_panel: the consumer half of the sco SDK's modular example pair.
 *
 * It owns no counter. It uses counter_service's table, "counter_service.counter" 1.0, and
 * shows how a consumer stays correct whatever it finds:
 *   - plugin.ini says `requires = counter_service.counter`, so the host loads counter_service
 *     first (this folder sorts before it) and refuses this plugin if nothing can provide it,
 *   - the table is looked up with query_service only when a command runs, never at load and
 *     never kept: the first lookup pins counter_service for the rest of the session,
 *   - every member is checked (size covers it, not NULL) before the call,
 *   - SCO_NOT_FOUND and SCO_UNAVAILABLE are ordinary answers: the commands reply with the
 *     reason (no provider, another version, or a host without query_service) and return
 *     SCO_UNAVAILABLE instead of failing or crashing.
 * Build: see README.md. GPL-3.0, like sco-core.
 */
#include <stdio.h>
#include <string.h>

/* The provider's contract header. In your own project, copy it next to your sources. */
#include "../counter_service/counter_service.h"

static const sco_api* api;
static sco_plugin*    me;

static const sco_plugin_info info = {
    sizeof(sco_plugin_info), SCO_API_MAJOR, SCO_API_MINOR,
    "counter_panel", /* must equal `id` in plugin.ini; also the command prefix */
    "1.0.0",
    "sco SDK example",
};

/* 1 when this host's sco_api has query_service (1.1 and later). */
static int host_has_query(void) {
    return api->size >= offsetof(sco_api, query_service) + sizeof(api->query_service) && api->query_service;
}

/* Looks the service up now. SCO_OK: *out is the table. Anything else: *out is NULL and the
 * result says why (SCO_NOT_FOUND: nobody publishes it; SCO_UNAVAILABLE: another major version,
 * an older minor, or a host without query_service; not_there tells those apart). */
static sco_result find_counter(const counter_service_v1** out) {
    const void* found = NULL;
    sco_result r;
    *out = NULL;
    if (!host_has_query()) return SCO_UNAVAILABLE;
    r = api->query_service(COUNTER_SERVICE_NAME, COUNTER_SERVICE_VERSION_1_0, &found);
    if (r == SCO_OK) *out = (const counter_service_v1*)found;
    return r;
}

/* The reply for a lookup that did not succeed, naming the real cause. Always SCO_UNAVAILABLE.
 * plugin.ini requires the provider, but it can still be absent when a command runs (a stand-in
 * host such as sco-plugin-check, a provider that faulted or published nothing), so the commands
 * degrade instead of failing. */
static sco_result not_there(sco_result why, char* reply, uint32_t reply_size) {
    if (!host_has_query())
        snprintf(reply, reply_size, "this host has no query_service (sco_api 1.0)");
    else if (why == SCO_NOT_FOUND)
        snprintf(reply, reply_size, "counter_service.counter is not installed");
    else
        snprintf(reply, reply_size, "counter_service.counter 1.x is not available");
    return SCO_UNAVAILABLE;
}

/* counter_panel.bump: adds 1 through the service and replies "counter is <n>". Game thread. */
static sco_result bump(const sco_arg* args, uint32_t nargs, void* ctx, char* reply, uint32_t reply_size) {
    const counter_service_v1* c;
    int64_t now = 0;
    sco_result r;
    (void)args; (void)ctx;
    if (nargs != 0) return SCO_BAD_ARG;
    if ((r = find_counter(&c)) != SCO_OK) return not_there(r, reply, reply_size);
    if (!COUNTER_SERVICE_HAS(c, add)) {
        snprintf(reply, reply_size, "counter_service.counter has no add");
        return SCO_UNAVAILABLE;
    }
    if ((r = c->add(1, &now)) != SCO_OK) {
        snprintf(reply, reply_size, "add failed (result %d)", (int)r);
        return SCO_FAILED;
    }
    snprintf(reply, reply_size, "counter is %lld", (long long)now);
    return SCO_OK;
}

/* counter_panel.show: replies "counter is <n>" without changing it. Game thread. */
static sco_result show(const sco_arg* args, uint32_t nargs, void* ctx, char* reply, uint32_t reply_size) {
    const counter_service_v1* c;
    int64_t now = 0;
    sco_result r;
    (void)args; (void)ctx;
    if (nargs != 0) return SCO_BAD_ARG;
    if ((r = find_counter(&c)) != SCO_OK) return not_there(r, reply, reply_size);
    if (!COUNTER_SERVICE_HAS(c, get)) {
        snprintf(reply, reply_size, "counter_service.counter has no get");
        return SCO_UNAVAILABLE;
    }
    if ((r = c->get(&now)) != SCO_OK) {
        snprintf(reply, reply_size, "get failed (result %d)", (int)r);
        return SCO_FAILED;
    }
    snprintf(reply, reply_size, "counter is %lld", (long long)now);
    return SCO_OK;
}

static sco_result add_command(const char* name, const char* title, const char* help, sco_command_fn fn) {
    sco_command cmd;
    memset(&cmd, 0, sizeof cmd);
    cmd.size         = sizeof cmd;
    cmd.name         = name;
    cmd.title        = title;
    cmd.help         = help;
    cmd.capability   = NULL; /* the service check happens inside the command, with a reason */
    cmd.args         = NULL;
    cmd.nargs        = 0;
    cmd.arg_def_size = sizeof(sco_arg_def);
    cmd.fn           = fn;
    cmd.ctx          = NULL;
    return api->register_command(me, &cmd);
}

SCO_EXPORT const sco_plugin_info* sco_plugin_query(void) { return &info; }

SCO_EXPORT sco_result sco_plugin_load(const sco_api* a, sco_plugin* self) {
    sco_result r;

    api = a;
    me = self;

    if ((r = add_command("counter_panel.bump", "Bump counter", "Adds 1 to the shared counter", bump)) != SCO_OK) return r;
    if ((r = add_command("counter_panel.show", "Show counter", "Shows the shared counter", show)) != SCO_OK) return r;

    /* No lookup here. requires = counter_service.counter already loaded the provider first, and
     * the first query_service hands its table out, after which the host keeps counter_service
     * loaded for the rest of the session (sco.reload refuses it). Looking it up only when a
     * command runs leaves the provider reloadable until someone uses the feature. */
    if (!host_has_query())
        api->log(me, SCO_LOG_WARN, "this host has no query_service (sco_api 1.0): bump and show answer unavailable");
    api->status(me, "Counter panel ready");
    return SCO_OK;
}

/* The host removes the commands itself. Nothing to release: this plugin keeps no table pointer,
 * and it never calls the provider from here. */
SCO_EXPORT void sco_plugin_unload(void) {}
