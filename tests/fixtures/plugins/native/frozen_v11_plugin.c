/* frozen_v11_plugin.c: a plugin built against sco_api.h exactly as released in sdk-v1.1.0.
 *
 * The ABI freeze (tools/abi_snapshot.py, tests/abi/sco_api-v1.1.golden) pins the header's
 * surface; this is the other half: the released header is extracted from the tag at test time
 * (`abi_snapshot.py released-header`) and this plugin is compiled against *that* copy, never
 * against the working tree's include/sco_api.h. tests/test_plugins.cpp then loads the resulting
 * module into the current host, so a change to sco_api.h that breaks a 1.1 plugin fails here
 * even where the pins in tests/abi_v1.c and the golden still match.
 *
 * No binary is committed: tools/test.sh and CMakeLists.txt build it into tests/out.
 * Keep it to the 1.1 surface only: nothing that a later minor added.
 */
#include "sco_api.h"
#include <stddef.h>

/* Exported so the host test can see what the plugin did (tests/test_plugins.cpp reads these). */
SCO_EXPORT int frozen_ticks = 0;    /* tick callbacks the host ran */
SCO_EXPORT int frozen_cmds = 0;     /* frozen.ping calls the host made */
SCO_EXPORT int frozen_unloads = 0;  /* sco_plugin_unload calls */
SCO_EXPORT int frozen_loads = 0;    /* sco_plugin_load calls */
SCO_EXPORT int frozen_expected_size = 0;  /* sizeof(sco_api) as the released header had it */
SCO_EXPORT int frozen_host_size = 0;      /* api->size, what this host handed us */

static const sco_api* g_api;
static sco_plugin*    g_self;

static void OnTick(const char* event, const void* data, void* ctx) {
    (void)event; (void)data; (void)ctx;
    ++frozen_ticks;
}

static sco_result Ping(const sco_arg* args, uint32_t nargs, void* ctx, char* reply,
                       uint32_t reply_size) {
    (void)args; (void)nargs; (void)ctx;
    ++frozen_cmds;
    static const char msg[] = "frozen 1.1 ok";
    uint32_t i = 0;
    if (reply && reply_size) {
        for (; msg[i] && i + 1 < reply_size; ++i) reply[i] = msg[i];
        reply[i] = '\0';
    }
    return SCO_OK;
}

SCO_EXPORT const sco_plugin_info* sco_plugin_query(void) {
    static sco_plugin_info info;
    info.size = sizeof(info);
    info.api_major = SCO_API_MAJOR;   /* 1, from the released header */
    info.api_minor = SCO_API_MINOR;   /* 1, from the released header */
    info.name = "v11";
    info.version = "1.1.0";
    info.author = "abi freeze";
    return &info;
}

SCO_EXPORT sco_result sco_plugin_load(const sco_api* api, sco_plugin* self) {
    ++frozen_loads;
    frozen_expected_size = (int)sizeof(sco_api);   /* from the released header */
    frozen_host_size = (int)api->size;             /* what this host handed us */
    g_api = api;
    g_self = self;
    sco_result r = api->subscribe(self, "tick", OnTick, NULL);
    if (r != SCO_OK) return r;
    api->log(self, SCO_LOG_INFO, "v11 loaded (built against sdk-v1.1.0)");
    sco_command cmd;
    cmd.size = sizeof(cmd);
    cmd.name = "v11.ping";
    cmd.title = "Ping";
    cmd.help = "Answers from a plugin built against sco_api.h 1.1";
    cmd.capability = NULL;
    cmd.args = NULL;
    cmd.nargs = 0;
    cmd.arg_def_size = 0;
    cmd.fn = Ping;
    cmd.ctx = NULL;
    return api->register_command(self, &cmd);
}

SCO_EXPORT void sco_plugin_unload(void) {
    ++frozen_unloads;
    (void)g_api; (void)g_self;
}
