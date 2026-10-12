/* SPDX-License-Identifier: GPL-3.0-only */
#include <sc_cvars.h>
#ifdef CVARS_PROBE_WRITE
#include <sc_cvars_write.h>
#endif
#include <stddef.h>
static const sco_plugin_info info = {sizeof(sco_plugin_info), 1, 1, "cvars_probe", "1.0.0", "scubamount"};
SCO_EXPORT const sco_plugin_info* sco_plugin_query(void) { return &info; }
SCO_EXPORT sco_result sco_plugin_load(const sco_api* api, sco_plugin* self) {
    const void* raw = NULL;
    if (!api || api->size < offsetof(sco_api, query_service) + sizeof(api->query_service) || !api->query_service)
        return SCO_UNAVAILABLE;
    sco_result r = api->query_service(SC_CVARS_NAME, SC_CVARS_VERSION, &raw);
    if (r != SCO_OK) return r;
    const sc_cvars_v1* t = (const sc_cvars_v1*)raw;
    if (!t || t->size < offsetof(sc_cvars_v1, get_bool) + sizeof(t->get_bool) || !t->get_bool)
        return SCO_UNAVAILABLE;
    uint32_t value = 9;
    r = t->get_bool(self, "i_target_selector.targeting2_enabled", SC_CVAR_BOOL, &value);
    if (r != SCO_OK || value != 1) return r == SCO_OK ? SCO_FAILED : r;
#ifdef CVARS_PROBE_WRITE
    raw = NULL;
    r = api->query_service(SC_CVARS_WRITE_NAME, SC_CVARS_WRITE_VERSION, &raw);
    if (r != SCO_OK) return r;
    const sc_cvars_write_v1* w = (const sc_cvars_write_v1*)raw;
    if (!w || w->size < offsetof(sc_cvars_write_v1, set_bool) + sizeof(w->set_bool) || !w->set_bool)
        return SCO_UNAVAILABLE;
    return w->set_bool(self, "i_target_selector.targeting2_enabled", SC_CVAR_BOOL, 0);
#else
    return SCO_OK;
#endif
}
SCO_EXPORT void sco_plugin_unload(void) {}
