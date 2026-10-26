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
PIN(sizeof(sc_item_type) == 4, "item_type size");
PIN(SC_ITEM_TYPE_AI_MODULE == 0, "AIModule = 0");
PIN(SC_ITEM_TYPE_CHAR_ARMOR_HELMET == 22, "Helmet = 22");
PIN(SC_ITEM_TYPE_CHAR_CLOTHING_HAT == 30, "Hat = 30");
PIN(SC_ITEM_TYPE_FPS_WEAPON_SIDEARM == 95, "FPS_WeaponSidearm = 95");
PIN(SC_ITEM_TYPE_INVENTORY_CONTAINER == 110, "InventoryContainer = 110");
PIN(SC_ITEM_TYPE_SHIP == 167, "Ship = 167");
PIN(SC_ITEM_TYPE_WHEELED_CONTROLLER == 191, "WheeledController = 191");
PIN(SC_ITEM_TYPE_COUNT_197 == 192, "192 entries on 197");

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
PIN(SC_INVENTORY_SERVICE_VERSION == SC_INVENTORY_SERVICE_VERSION_1_1, "legacy read alias");
PIN(SC_INVENTORY_SERVICE_VERSION_1_2 == 0x00010002u, "explicit layout version");
PIN(sizeof(sc_inventory_v1_2) == 24, "layout table size");
PIN(offsetof(sc_inventory_v1_2, base) == 0, "legacy base prefix");
PIN(offsetof(sc_inventory_v1_2, snapshot_layout) == 16, "layout method offset");
PIN(sizeof(sc_inventory_layout_entry) == 96, "layout entry size");
PIN(offsetof(sc_inventory_layout_entry, raw_item_type) == 16, "raw category offset");
PIN(offsetof(sc_inventory_layout_entry, entity_id) == 24, "ephemeral entity offset");
PIN(offsetof(sc_inventory_layout_entry, port_key) == 32, "logical key offset");
PIN(sizeof(sc_inventory_layout_snapshot) == 160, "layout snapshot size");
PIN(offsetof(sc_inventory_layout_snapshot, source_epoch) == 16, "source epoch offset");
PIN(offsetof(sc_inventory_layout_snapshot, observation_generation) == 24, "observation revision offset");
PIN(offsetof(sc_inventory_layout_snapshot, identity_domain) == 32, "identity domain offset");
PIN(offsetof(sc_inventory_layout_snapshot, layout_key) == 96, "layout key offset");
static sco_result snap_ok(sco_plugin* p, sc_inventory_snapshot* o) { (void)p; (void)o; return SCO_OK; }
int main(void) {
    sc_inventory_v1 full = {sizeof(full), 0, snap_ok};
    sc_inventory_snapshot s;
    memset(&s, 0, sizeof(s));
    s.size = sizeof(s); s.count = SC_INVENTORY_MAX_PORTS;
    assert(sc_inventory_snapshot_ports(&full, NULL, &s) == SCO_OK);
    assert(strcmp(sc_inventory_item_type_name(22), "Char_Armor_Helmet") == 0);
    assert(strcmp(sc_inventory_item_type_name(95), "FPS_WeaponSidearm") == 0);
    assert(strcmp(sc_inventory_item_type_name(191), "WheeledController") == 0);
    assert(strcmp(sc_inventory_item_type_name(999), "UNDEFINED") == 0);
    sc_inventory_port port22;
    memset(&port22, 0, sizeof(port22));
    port22.size = sizeof(port22);
    port22.valid = 1;
    port22.item_class_id = 22;
    assert(sc_inventory_port_item_type(&port22) == SC_ITEM_TYPE_CHAR_ARMOR_HELMET);
    sc_inventory_port portInv;
    memset(&portInv, 0, sizeof(portInv));
    portInv.valid = 0;
    portInv.item_class_id = 22;
    assert(sc_inventory_port_item_type(&portInv) == SC_ITEM_TYPE_UNKNOWN);

    sc_inventory_v1 truncated = {offsetof(sc_inventory_v1, snapshot), 0, snap_ok};
    assert(sc_inventory_snapshot_ports(&truncated, NULL, &s) == SCO_UNAVAILABLE);
    assert(sc_inventory_snapshot_ports(NULL, NULL, &s) == SCO_UNAVAILABLE);
    assert(sc_inventory_snapshot_ports(&full, NULL, NULL) == SCO_UNAVAILABLE);
    sc_inventory_v1 nullfn = {sizeof(nullfn), 0, NULL};
    assert(sc_inventory_snapshot_ports(&nullfn, NULL, &s) == SCO_UNAVAILABLE);
    return 0;
}
