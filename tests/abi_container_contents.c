/* SPDX-License-Identifier: GPL-3.0-only */
#include <sc_container_contents.h>
#include <stddef.h>
#ifdef __cplusplus
#define PIN static_assert
#else
#define PIN _Static_assert
#endif
PIN(sizeof(sc_container_item) == 32, "item size");
PIN(offsetof(sc_container_item, item_id) == 8, "item id");
PIN(offsetof(sc_container_item, unit_volume) == 24, "volume");
PIN(sizeof(sc_container_snapshot) == 2088, "snapshot");
PIN(offsetof(sc_container_snapshot, items) == 40, "items offset");
PIN(sizeof(sc_container_contents_v1) == 16, "table");
PIN(offsetof(sc_container_contents_v1, snapshot) == 8, "member extent");
static sco_result read_ok(sco_plugin* self, uint64_t id, sc_container_snapshot* out) {
    (void)self; (void)id; (void)out; return SCO_OK;
}
int main(void) {
    sc_container_contents_v1 table = {sizeof(table), 0, read_ok};
    if (sc_container_contents_read(&table, NULL, 1, NULL) != SCO_OK) return 1;
    table.size = offsetof(sc_container_contents_v1, snapshot);
    if (sc_container_contents_read(&table, NULL, 1, NULL) != SCO_UNAVAILABLE) return 2;
    table.size = sizeof(table); table.snapshot = NULL;
    if (sc_container_contents_read(&table, NULL, 1, NULL) != SCO_UNAVAILABLE) return 3;
    if (sc_container_contents_read(NULL, NULL, 1, NULL) != SCO_UNAVAILABLE) return 4;
    return 0;
}
