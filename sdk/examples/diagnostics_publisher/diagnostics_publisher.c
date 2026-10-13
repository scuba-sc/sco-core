/* GPL-3.0-only. Public SDK only; no reads, transport, game values or user fields. */
#include "sco_diagnostics_publisher.h"
#include <stddef.h>
SCO_EXPORT const sco_plugin_info* sco_plugin_query(void) {
    static const sco_plugin_info info = { sizeof(sco_plugin_info), SCO_API_MAJOR,
        SCO_API_MINOR, "diagnostics_publisher", "1.0.0", "scubamount" };
    return &info;
}
SCO_EXPORT sco_result sco_plugin_load(const sco_api* api, sco_plugin* self) {
    const void* out = NULL;
    const sco_diagnostics_publisher_v1* table;
    const sco_diagnostics_observation_v1 observation = {sizeof(observation), SCO_DIAGNOSTICS_HEALTH_READY};
    sco_result r;
    if (!api || api->size < offsetof(sco_api, query_service) + sizeof(api->query_service) || !api->query_service)
        return SCO_UNAVAILABLE;
    r = api->query_service(SCO_DIAGNOSTICS_PUBLISHER_NAME, SCO_DIAGNOSTICS_PUBLISHER_VERSION_1_0, &out);
    if (r != SCO_OK) return r;
    table = (const sco_diagnostics_publisher_v1*)out;
    if (!table || table->size < offsetof(sco_diagnostics_publisher_v1, submit) + sizeof(table->submit) || !table->submit)
        return SCO_UNAVAILABLE;
    /* This sample reports its own successful initialization, not game readiness.
       A real diagnostic plugin submits observations during normal game-thread
       callbacks, never during snapshot construction or an endpoint request. */
    return table->submit(self, &observation);
}
SCO_EXPORT void sco_plugin_unload(void) {}
