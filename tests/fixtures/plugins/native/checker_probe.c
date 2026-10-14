/* Test plugin for sco-plugin-check's stand-in host (tests/plugin_check.cmake). Built once per case
 * with -DPROBE_MODE=<n> and -DPROBE_ID="<id>". Includes only public headers, like a real plugin. */
#include "sco_api.h"
#include "sco_widgets.h"
#include <stddef.h>

#define PROBE_RELEASE_OWN      0  /* provides PROBE_ID ".svc", then withdraws it: passes */
#define PROBE_RELEASE_BORROWED 1  /* queries sco.widgets, then calls release_service on it: fails */
#define PROBE_TAIL_UNCHECKED   2  /* calls query_service without checking api->size: fails with --short-tables */
#define PROBE_TAIL_CHECKED     3  /* checks api->size first: passes with and without --short-tables */

#ifndef PROBE_MODE
#error "build with -DPROBE_MODE=<n>"
#endif
#ifndef PROBE_ID
#define PROBE_ID "probe"
#endif

/* docs/contracts.md: the table covers the whole pointer before it is read. */
#define COVERS(api, member) ((api)->size >= offsetof(sco_api, member) + sizeof((api)->member))

static const sco_plugin_info g_info = {
    sizeof(sco_plugin_info), SCO_API_MAJOR, SCO_API_MINOR, PROBE_ID, "1.0.0", "sco-core tests"
};

SCO_EXPORT const sco_plugin_info* sco_plugin_query(void) { return &g_info; }

#if PROBE_MODE == PROBE_RELEASE_OWN
typedef struct probe_service { uint32_t size; int (*answer)(void); } probe_service;
static int Answer(void) { return 42; }
static const probe_service g_svc = { sizeof(probe_service), Answer };
#endif

SCO_EXPORT sco_result sco_plugin_load(const sco_api* api, sco_plugin* self) {
#if PROBE_MODE == PROBE_RELEASE_OWN
    sco_result r;
    if (!COVERS(api, release_service)) return SCO_OK;
    r = api->provide_service(self, PROBE_ID ".svc", 1u << 16, &g_svc);
    if (r != SCO_OK) return r;
    return api->release_service(self, PROBE_ID ".svc");
#elif PROBE_MODE == PROBE_RELEASE_BORROWED
    const void* table = NULL;
    if (!COVERS(api, release_service)) return SCO_OK;
    if (api->query_service(SCO_WIDGETS_NAME, SCO_WIDGETS_VERSION_1_0, &table) != SCO_OK) return SCO_FAILED;
    (void)api->release_service(self, SCO_WIDGETS_NAME);   /* the misuse: the table is borrowed */
    return SCO_OK;
#elif PROBE_MODE == PROBE_TAIL_UNCHECKED
    const void* table = NULL;
    const sco_result r = api->query_service(SCO_WIDGETS_NAME, SCO_WIDGETS_VERSION_1_0, &table);   /* no size check */
    api->log(self, SCO_LOG_INFO, r == SCO_OK ? "widgets found" : "widgets missing");
    return SCO_OK;
#elif PROBE_MODE == PROBE_TAIL_CHECKED
    const void* table = NULL;
    if (!COVERS(api, query_service)) {
        api->log(self, SCO_LOG_INFO, "1.0 host: no services");
        return SCO_OK;
    }
    api->log(self, SCO_LOG_INFO,
             api->query_service(SCO_WIDGETS_NAME, SCO_WIDGETS_VERSION_1_0, &table) == SCO_OK ? "widgets found"
                                                                                           : "widgets missing");
    return SCO_OK;
#else
#error "unknown PROBE_MODE"
#endif
}

SCO_EXPORT void sco_plugin_unload(void) {}
