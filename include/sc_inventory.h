/* Read-only view over the local player's item ports: what is attached where. No writes,
 * no pointers, no actions cross this ABI. See docs/game/inventory.md. */
#ifndef SC_INVENTORY_H
#define SC_INVENTORY_H
#include "sco_api.h"
#include <stddef.h>
#include <stdint.h>
#define SC_INVENTORY_SERVICE_NAME "game.inventory"
#define SC_INVENTORY_SERVICE_VERSION 0x00010000u
#define SC_INVENTORY_MAX_PORTS 128u
/* One item attached to one port of the queried entity, as of the snapshot. Names are not
 * exposed in 1.0: class_id is the game's item class type id (opaque, build-scoped); a
 * type-id -> name map is a documented follow-up (docs/game/inventory.md). */
typedef struct sc_inventory_port {
    uint32_t size;
    uint32_t valid;            /* 1 = port exists and holds an item; else fields below are 0 */
    uint64_t item_class_id;    /* game item class type id, opaque per build */
    uint64_t entity_id;        /* the item's entity id, session-scoped (game.entities keys) */
} sc_inventory_port;
typedef struct sc_inventory_snapshot {
    uint32_t size;
    uint32_t count;                            /* ports filled, <= SC_INVENTORY_MAX_PORTS */
    uint64_t generation;                       /* changes when the port set changes */
    sc_inventory_port ports[SC_INVENTORY_MAX_PORTS];
} sc_inventory_snapshot;
typedef struct sc_inventory_v1 {
    uint32_t size;
    uint32_t _pad;
    /* Copies a snapshot of the LOCAL player's item ports into out (caller sets out->size,
     * out->count = capacity of ports[]). TRUNCATED-style contract: fills min(count, ports),
     * sets count to the number written; generation lets a caller detect growth. UNAVAILABLE
     * with no bound backend, off the game thread, no local player, or out->size too small.
     * Values are the client's view of server state at snapshot time; read-only by contract. */
    sco_result (*snapshot)(sco_plugin* self, sc_inventory_snapshot* out);
} sc_inventory_v1;
static inline sco_result sc_inventory_snapshot_ports(const sc_inventory_v1* t, sco_plugin* self,
                                                     sc_inventory_snapshot* out) {
    return (t && t->size >= offsetof(sc_inventory_v1, snapshot) + sizeof(t->snapshot) && t->snapshot && out)
               ? t->snapshot(self, out) : SCO_UNAVAILABLE;
}
#endif
