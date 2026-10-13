/* GPL-3.0-only. Public-header-only consumer; user-authored bytes, no execution. */
#include "sco_content.h"
#include <stddef.h>
#include <stdlib.h>

SCO_EXPORT const sco_plugin_info* sco_plugin_query(void) {
    static const sco_plugin_info info = { sizeof(sco_plugin_info), SCO_API_MAJOR,
        SCO_API_MINOR, "content_reader", "1.0.0", "scubamount" };
    return &info;
}
SCO_EXPORT sco_result sco_plugin_load(const sco_api* api, sco_plugin* self) {
    const sco_content_v1* table = NULL;
    const void* out = NULL;
    sco_content_item item = {0};
    uint32_t size = 0;
    uint32_t ordinal = 0;
    void* bytes;
    sco_result r;
    if (!api || api->size < offsetof(sco_api, query_service) + sizeof(api->query_service) || !api->query_service)
        return SCO_UNAVAILABLE;
    r = api->query_service(SCO_CONTENT_NAME, SCO_CONTENT_VERSION_1_0, &out);
    if (r != SCO_OK) return r;
    table = (const sco_content_v1*)out;
    if (!table || table->size < offsetof(sco_content_v1, read) + sizeof(table->read) || !table->item || !table->read)
        return SCO_UNAVAILABLE;
    /* Skip empty files so the synthetic module fixture exercises byte copying. */
    for (;;) {
        item.size = sizeof(item);
        r = table->item(self, SCO_CONTENT_LIST, ordinal++, &item);
        if (r == SCO_NOT_FOUND) return SCO_OK; /* empty/disabled packs are normal */
        if (r != SCO_OK) return r;
        size = 0;
        r = table->read(self, item.id, NULL, &size);
        if (r != SCO_OK || size != 0) break;
    }
    if (r != SCO_TOO_MANY || size > SCO_CONTENT_MAX_FILE_BYTES) return SCO_UNAVAILABLE;
    bytes = malloc(size);
    if (!bytes) return SCO_TOO_MANY;
    r = table->read(self, item.id, bytes, &size);
    free(bytes); /* Parse the exact bytes in a real feature. No proprietary data here. */
    if (r == SCO_OK && api->size >= offsetof(sco_api, status) + sizeof(api->status) && api->status)
        api->status(self, "content snapshot read");
    return r;
}
SCO_EXPORT void sco_plugin_unload(void) {}
