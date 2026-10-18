/*
 * sc_places.h: game service "game.places", version 1.0 (SDK 2.4.0, issue #124 [G3]).
 *
 * Exposes read-only starmap and place directory navigation from the game's
 * CPlaceDirectoryComponent and CZoneSystem (anchored in F-4.10.197.2878-025..030).
 *
 * Game-neutral wire types in C. Additive ABI only (Rule 2).
 */
#ifndef SC_PLACES_H
#define SC_PLACES_H

#include "sco_api.h"
#include <stdint.h>
#include <stddef.h>

#define SC_PLACES_SERVICE_NAME "game.places"
#define SC_PLACES_SERVICE_VERSION_1_0 0x00010000u
#define SC_PLACES_SERVICE_VERSION SC_PLACES_SERVICE_VERSION_1_0

#define SC_PLACES_MAX_NAME_BYTES 64u

typedef struct sc_place_info {
    uint32_t size;                              /* sizeof(sc_place_info) */
    uint32_t flags;                             /* reserved, 0 */
    uint64_t place_id;                          /* unique numeric place identifier */
    uint64_t parent_place_id;                   /* parent place or 0 if top-level */
    uint64_t zone_id;                           /* bound runtime zone entity id or 0 */
    char     name[SC_PLACES_MAX_NAME_BYTES];    /* display/internal identifier name */
} sc_place_info;

typedef struct sc_places_v1 {
    uint32_t size;
    uint32_t _pad;

    /* Get total count of registered places in the directory */
    sco_result (*count)(sco_plugin* self, uint32_t* out_count);

    /* Get place info by index [0..count-1] */
    sco_result (*by_index)(sco_plugin* self, uint32_t index, sc_place_info* out_place);

    /* Find place info by exact name */
    sco_result (*by_name)(sco_plugin* self, const char* name, sc_place_info* out_place);

    /* Find place info by place_id */
    sco_result (*by_id)(sco_plugin* self, uint64_t place_id, sc_place_info* out_place);
} sc_places_v1;

static inline sco_result sc_places_count(const sc_places_v1* t, sco_plugin* self, uint32_t* out_count) {
    return (t && t->size >= offsetof(sc_places_v1, count) + sizeof(t->count) && t->count)
        ? t->count(self, out_count) : SCO_UNAVAILABLE;
}

#endif /* SC_PLACES_H */
