/*
 * sc_datacore_live.h: game service "game.datacore_live", version 1.0 (SDK 2.4.0, issue #193 [G22]).
 *
 * Host-backed in-memory pool access. The optional native read adapter defaults
 * OFF and requires host lifecycle preparation and fresh offline admission.
 * Native writes remain unavailable.
 *
 * Game-neutral wire types in C. Additive ABI only (Rule 2).
 */
#ifndef SC_DATACORE_LIVE_H
#define SC_DATACORE_LIVE_H

#include "sco_api.h"
#include <stdint.h>
#include <stddef.h>

#define SC_DATACORE_LIVE_SERVICE_NAME "game.datacore_live"
#define SC_DATACORE_LIVE_SERVICE_VERSION_1_0 0x00010000u
#define SC_DATACORE_LIVE_SERVICE_VERSION SC_DATACORE_LIVE_SERVICE_VERSION_1_0

#define SC_DATACORE_LIVE_CAPABILITY "game.datacore_live"

/* Supported scalar pool types for in-memory records */
typedef enum sc_datacore_pool_type {
    SC_DC_POOL_BOOL     = 0,
    SC_DC_POOL_INT8     = 1,
    SC_DC_POOL_INT16    = 2,
    SC_DC_POOL_INT32    = 3,
    SC_DC_POOL_INT64    = 4,
    SC_DC_POOL_UINT8    = 5,
    SC_DC_POOL_UINT16   = 6,
    SC_DC_POOL_UINT32   = 7,
    SC_DC_POOL_UINT64   = 8,
    SC_DC_POOL_FLOAT    = 9,
    SC_DC_POOL_DOUBLE   = 10,
    SC_DC_POOL_GUID     = 11,
    SC_DC_POOL_STRING   = 12, /* serialized text blob: byte elements, not string indices */
    SC_DC_POOL_COUNT    = 13
} sc_datacore_pool_type;

typedef struct sc_datacore_pool_stats {
    uint32_t size;            /* sizeof(sc_datacore_pool_stats) */
    uint32_t pool_type;       /* sc_datacore_pool_type */
    uint64_t element_count;   /* elements in pool */
    uint64_t capacity;        /* allocated capacity */
    uint64_t element_size;    /* size of single element in bytes */
    uint64_t total_bytes;     /* capacity * element_size */
} sc_datacore_pool_stats;

typedef struct sc_datacore_live_record_info {
    uint32_t size;            /* sizeof(sc_datacore_live_record_info) */
    uint8_t  guid[16];        /* record GUID */
    uint32_t struct_type;     /* struct definition index */
    uint32_t field_count;     /* number of fields, including inherited fields */
    uint64_t pool_offset;     /* byte offset within the serialized record table */
} sc_datacore_live_record_info;

typedef struct sc_datacore_live_v1 {
    uint32_t size;
    uint32_t _pad;

    /* Status: returns SCO_OK if live DataCore structures are mapped and ready */
    sco_result (*is_ready)(sco_plugin* self);

    /* Get statistics for a specific scalar pool */
    sco_result (*get_pool_stats)(sco_plugin* self, uint32_t pool_type, sc_datacore_pool_stats* out_stats);

    /* Find a live record by 16-byte GUID */
    sco_result (*find_record)(sco_plugin* self, const uint8_t guid[16], sc_datacore_live_record_info* out_record);

    /* Read raw bytes from a specified pool offset */
    sco_result (*read_pool)(sco_plugin* self, uint32_t pool_type, uint64_t offset, void* out_buffer, size_t buffer_size, size_t* out_read);

    /* Write bytes only with fresh admission and the host's separate write opt-in. */
    sco_result (*write_pool)(sco_plugin* self, uint32_t pool_type, uint64_t offset, const void* in_buffer, size_t buffer_size, size_t* out_written);
} sc_datacore_live_v1;

static inline sco_result sc_datacore_live_is_ready(const sc_datacore_live_v1* t, sco_plugin* self) {
    return (t && t->size >= offsetof(sc_datacore_live_v1, is_ready) + sizeof(t->is_ready) && t->is_ready)
        ? t->is_ready(self) : SCO_UNAVAILABLE;
}

#endif /* SC_DATACORE_LIVE_H */
