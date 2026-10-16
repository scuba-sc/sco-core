/* Read-only view over the local player's item ports: what is attached where. No writes,
 * no pointers, no actions cross this ABI. See docs/game/inventory.md. */
#ifndef SC_INVENTORY_H
#define SC_INVENTORY_H
#include "sco_api.h"
#include <stddef.h>
#include <stdint.h>
#define SC_INVENTORY_SERVICE_NAME "game.inventory"
#define SC_INVENTORY_SERVICE_VERSION_1_0 0x00010000u
#define SC_INVENTORY_SERVICE_VERSION_1_1 0x00010001u /* + flags and total_seen after ports[] */
#define SC_INVENTORY_SERVICE_VERSION SC_INVENTORY_SERVICE_VERSION_1_1
#define SC_INVENTORY_MAX_PORTS 128u
/* snapshot.flags bits (1.1; see sc_inventory_snapshot). */
#define SC_INVENTORY_FLAG_TRUNCATED 0x1u /* ports exist that were not written: total_seen > count */
#define SC_INVENTORY_FLAG_LIMITED   0x2u /* the walk's own limits (depth, container budget, a cycle or an
                                            unreadable nested container) cut the tree: total_seen is a lower bound */
/* One item attached to one port of the queried entity, as of the snapshot. Names are not
 * exposed in 1.0: item_class_id holds the item type id, an ordinal of the game's item-type
 * enumeration (a coarse category, build-scoped). Per-class item names are not yet established;
 * an item-type-id name map exists but is not exposed in 1.0 (docs/game/inventory.md). */
typedef struct sc_inventory_port {
    uint32_t size;
    uint32_t valid;            /* 1 = port exists and holds an item; else fields below are 0 */
    uint64_t item_class_id;    /* item type id: ordinal of the game's item-type enumeration */
    uint64_t entity_id;        /* the item's entity id, session-scoped (game.entities keys) */
} sc_inventory_port;
typedef struct sc_inventory_snapshot {
    uint32_t size;
    uint32_t count;                            /* ports filled, <= SC_INVENTORY_MAX_PORTS */
    uint64_t generation;                       /* changes when the port set changes */
    sc_inventory_port ports[SC_INVENTORY_MAX_PORTS];
    /* 1.1 tail: written only when out->size covers it (a 1.0 caller passes size 3088 and never
     * sees it). Ports come from the whole attached tree, depth first, each port followed by the
     * ports of the container attached to it, empty ports included (docs/game/inventory.md). */
    uint32_t flags;                            /* SC_INVENTORY_FLAG_* */
    uint32_t total_seen;                       /* ports the walk found, written or not; >= count */
} sc_inventory_snapshot;
/* sizeof the 1.0 snapshot: the least out->size a snapshot call accepts. */
#define SC_INVENTORY_SNAPSHOT_SIZE_1_0 3088u
typedef struct sc_inventory_v1 {
    uint32_t size;
    uint32_t _pad;
    /* Copies a snapshot of the LOCAL player's item ports into out (caller sets out->size,
     * out->count = capacity of ports[]). TRUNCATED-style contract: fills min(count, ports),
     * sets count to the number written; generation lets a caller detect growth. With
     * out->size >= sizeof(sc_inventory_snapshot) it also sets flags and total_seen. UNAVAILABLE
     * with no bound backend, off the game thread, no local player, no item-port container on the
     * player, or out->size below SC_INVENTORY_SNAPSHOT_SIZE_1_0.
     * Values are the client's view of server state at snapshot time; read-only by contract. */
    sco_result (*snapshot)(sco_plugin* self, sc_inventory_snapshot* out);
} sc_inventory_v1;
static inline sco_result sc_inventory_snapshot_ports(const sc_inventory_v1* t, sco_plugin* self,
                                                     sc_inventory_snapshot* out) {
    return (t && t->size >= offsetof(sc_inventory_v1, snapshot) + sizeof(t->snapshot) && t->snapshot && out)
               ? t->snapshot(self, out) : SCO_UNAVAILABLE;
}
#endif
