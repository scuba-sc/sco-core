/* Independently compiled C11/C++20 inventory layout and tail refusal fixture. 1.1 only appends
 * flags and total_seen after ports[]: every 1.0 offset below is unchanged (rule 2, ABI only grows). */
#include <sc_inventory.h>
#include <stddef.h>
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#ifdef __cplusplus
#define PIN static_assert
#else
#define PIN _Static_assert
#endif
PIN(sizeof(sc_inventory_port) == 24, "port size");
PIN(offsetof(sc_inventory_port, item_class_id) == 8, "class id offset");
PIN(offsetof(sc_inventory_port, entity_id) == 16, "entity id offset");
PIN(sizeof(sc_inventory_snapshot) == 3096, "snapshot size (1.1)");
PIN(SC_INVENTORY_SNAPSHOT_SIZE_1_0 == 3088, "1.0 snapshot size");
PIN(offsetof(sc_inventory_snapshot, flags) == SC_INVENTORY_SNAPSHOT_SIZE_1_0, "tail starts at the 1.0 size");
PIN(offsetof(sc_inventory_snapshot, total_seen) == 3092, "total_seen offset");
PIN(SC_INVENTORY_FLAG_TRUNCATED == 1 && SC_INVENTORY_FLAG_LIMITED == 2, "flag bits");
PIN(SC_INVENTORY_SERVICE_VERSION_1_0 == 0x00010000u && SC_INVENTORY_SERVICE_VERSION_1_1 == 0x00010001u, "versions");
PIN(offsetof(sc_inventory_snapshot, count) == 4, "count offset");
PIN(offsetof(sc_inventory_snapshot, generation) == 8, "generation offset");
PIN(offsetof(sc_inventory_snapshot, ports) == 16, "ports offset");
PIN(SC_INVENTORY_MAX_PORTS == 128, "max ports");
PIN(sizeof(sc_inventory_v1) == 16, "table size");
PIN(offsetof(sc_inventory_v1, snapshot) == 8, "snapshot offset");
static sco_result snap_ok(sco_plugin* p, sc_inventory_snapshot* o) { (void)p; (void)o; return SCO_OK; }
int main(void) {
    sc_inventory_v1 full = {sizeof(full), 0, snap_ok};
    sc_inventory_snapshot s;
    memset(&s, 0, sizeof(s));
    s.size = sizeof(s); s.count = SC_INVENTORY_MAX_PORTS;
    assert(sc_inventory_snapshot_ports(&full, NULL, &s) == SCO_OK);
    sc_inventory_v1 truncated = {offsetof(sc_inventory_v1, snapshot), 0, snap_ok};
    assert(sc_inventory_snapshot_ports(&truncated, NULL, &s) == SCO_UNAVAILABLE);
    assert(sc_inventory_snapshot_ports(NULL, NULL, &s) == SCO_UNAVAILABLE);
    assert(sc_inventory_snapshot_ports(&full, NULL, NULL) == SCO_UNAVAILABLE);
    sc_inventory_v1 nullfn = {sizeof(nullfn), 0, NULL};
    assert(sc_inventory_snapshot_ports(&nullfn, NULL, &s) == SCO_UNAVAILABLE);
    return 0;
}
