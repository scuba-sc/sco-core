/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef SC_CONTAINER_CONTENTS_H
#define SC_CONTAINER_CONTENTS_H
#include "sco_api.h"
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define SC_CONTAINER_CONTENTS_NAME "game.inventory.contents"
#define SC_CONTAINER_CONTENTS_VERSION_1_0 0x00010000u
#define SC_CONTAINER_CONTENTS_MAX_ITEMS 64u
#define SC_CONTAINER_CONTENTS_TRUNCATED 1u
#define SC_CONTAINER_CONTENTS_VOLUME_KNOWN 2u
/* IDs are opaque session-local keys, not engine pointers or persistent record GUIDs.
 * unit_volume/used_volume are millionths of one standard cargo unit (micro-SCU);
 * providers convert their own units before publication. Absent VOLUME_KNOWN,
 * all volume fields are zero. Quantity is positive for every returned item. */
typedef struct sc_container_item {
    uint32_t size;
    uint32_t quantity;
    uint64_t item_id;
    uint64_t class_id;
    uint64_t unit_volume;
} sc_container_item;
/* Caller sets size and count (capacity <= MAX_ITEMS). Provider copies records,
 * generation and total_seen; count never exceeds capacity. No pointer retained.
 * A successful zero count is empty only when total_seen is zero. */
typedef struct sc_container_snapshot {
    uint32_t size;
    uint32_t count;
    uint64_t generation;
    uint64_t container_id;
    uint64_t used_volume;
    uint32_t flags;
    uint32_t total_seen;
    sc_container_item items[SC_CONTAINER_CONTENTS_MAX_ITEMS];
} sc_container_snapshot;
typedef struct sc_container_contents_v1 {
    uint32_t size;
    uint32_t _pad;
    /* Game thread; live plugin handle; id nonzero. Failure zeros the known-sized
     * output, preserves caller size/tail. Unbound backend: UNAVAILABLE;
     * unknown container: NOT_FOUND; malformed backend or exception: FAILED. */
    sco_result (*snapshot)(sco_plugin* self, uint64_t container_id, sc_container_snapshot* out);
} sc_container_contents_v1;
static inline sco_result sc_container_contents_read(const sc_container_contents_v1* table,
    sco_plugin* self, uint64_t id, sc_container_snapshot* out) {
    return table && table->size >= offsetof(sc_container_contents_v1, snapshot) + sizeof(table->snapshot)
        && table->snapshot ? table->snapshot(self, id, out) : SCO_UNAVAILABLE;
}
#ifdef __cplusplus
}
#endif
#endif
