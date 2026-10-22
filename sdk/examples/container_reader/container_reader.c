/* SPDX-License-Identifier: GPL-3.0-only */
#include "sc_container_contents.h"
#include <stdio.h>
#include <string.h>
static const sco_api* host;
static sco_plugin* owner;
static sco_result Read(const sco_arg* args, uint32_t count, void* ctx, char* out, uint32_t capacity) {
    const void* raw = NULL;
    sc_container_snapshot snapshot;
    sco_result result;
    (void)ctx;
    if (!out && capacity) return SCO_BAD_ARG;
    if (out && capacity) out[0] = 0;
    if (!args || count != 1 || args[0].type != SCO_ARG_INT || args[0].v.i <= 0) return SCO_BAD_ARG;
    result = host->query_service(SC_CONTAINER_CONTENTS_NAME, SC_CONTAINER_CONTENTS_VERSION_1_0, &raw);
    if (result != SCO_OK) return result;
    memset(&snapshot, 0, sizeof(snapshot));
    snapshot.size = sizeof(snapshot); snapshot.count = SC_CONTAINER_CONTENTS_MAX_ITEMS;
    result = sc_container_contents_read((const sc_container_contents_v1*)raw, owner,
        (uint64_t)args[0].v.i, &snapshot);
    if (result == SCO_OK && out && capacity)
        snprintf(out, capacity, "items=%u total=%u generation=%llu volume_known=%u truncated=%u",
            snapshot.count, snapshot.total_seen, (unsigned long long)snapshot.generation,
            !!(snapshot.flags & SC_CONTAINER_CONTENTS_VOLUME_KNOWN),
            !!(snapshot.flags & SC_CONTAINER_CONTENTS_TRUNCATED));
    return result;
}
SCO_EXPORT const sco_plugin_info* sco_plugin_query(void) {
    static const sco_plugin_info info = {sizeof(sco_plugin_info), SCO_API_MAJOR,
        SCO_API_MINOR, "container_reader", "1.0.0", "scubamount"};
    return &info;
}
SCO_EXPORT sco_result sco_plugin_load(const sco_api* api, sco_plugin* self) {
    static const sco_arg_def arg = {"container", SCO_ARG_INT, 0, "Session-local container id"};
    static const sco_command command = {sizeof(sco_command), 0, "container_reader.read",
        "Read container contents", "Copied SDK snapshot; no private pointers", NULL,
        &arg, 1, sizeof(sco_arg_def), Read, NULL};
    sco_result result;
    if (!api || !self || api->size < offsetof(sco_api, query_service) + sizeof(api->query_service)
        || !api->query_service || !api->register_command) return SCO_UNAVAILABLE;
    host = api; owner = self;
    result = api->register_command(self, &command);
    if (result != SCO_OK) { host = NULL; owner = NULL; }
    return result;
}
SCO_EXPORT void sco_plugin_unload(void) { host = NULL; owner = NULL; }
