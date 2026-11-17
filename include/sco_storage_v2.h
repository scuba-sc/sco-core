/* SPDX-License-Identifier: GPL-3.0-only
 * Read-only, per-owner metadata. This is a separate service; sco.storage 1.x is unchanged.
 * No paths, row values or other owners' databases are exposed. See docs/storage-metadata.md.
 */
#ifndef SCO_STORAGE_V2_H
#define SCO_STORAGE_V2_H

#include "sco_api.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SCO_STORAGE_V2_NAME "sco.storage.metadata"
#define SCO_STORAGE_V2_VERSION_2_0 0x00020000u
#define SCO_STORAGE_OWNER_OPENED 0x01u

/* Set size to the caller's capacity. At least sizeof(record) is required. On success only
 * sizeof(record) bytes are written, size becomes sizeof(record), and any trailing bytes are
 * preserved. Flags not defined here are reserved and returned zero. All outputs remain
 * unchanged on failure, except the next_table required-size handshake described below. */
typedef struct sco_storage_owner_info {
    uint32_t size;
    uint32_t flags;
    uint64_t session_generation;  /* current successful storage Start epoch, never zero */
    uint64_t table_count;         /* ordinary own main-schema tables only */
    uint64_t cursor_count;
} sco_storage_owner_info;

typedef struct sco_storage_table_info {
    uint32_t size;
    uint32_t flags;
    uint64_t column_count;        /* includes declared generated columns */
} sco_storage_table_info;

typedef struct sco_storage_quota_info {
    uint32_t size;
    uint32_t flags;
    uint64_t configured_bytes;    /* host option, before SQLite page rounding */
    uint64_t effective_bytes;     /* current max_page_count * page_size */
    uint64_t allocated_bytes;     /* current page_count * page_size */
    uint64_t free_bytes;          /* effective_bytes - allocated_bytes */
} sco_storage_quota_info;

/* Any thread; calls serialize with this owner's storage operations and obey the host SQL
 * time budget. Metadata never opens a database or creates files. A valid but unopened owner
 * returns SCO_UNAVAILABLE. Unknown, foreign and released handles return SCO_BAD_ARG; stopped
 * storage returns SCO_UNAVAILABLE. Views, virtual tables, sco_* and sqlite_* names are
 * excluded. To avoid connecting virtual modules, shadow exclusion is conservative: any
 * ordinary name starting with a declared main-schema virtual table's name plus '_' is
 * excluded, case-insensitively, even if an application created that prefixed table itself.
 * Only the owner's existing main schema is inspected.
 * Names use the storage 1.1 input limit: 16,777,216 bytes excluding the terminating NUL.
 * Empty names are valid. Host queries validate the schema before quoting a table identifier.
 */
typedef struct sco_storage_v2 {
    uint32_t size;
    uint32_t _pad;
    sco_result (*owner_info)(sco_plugin* self, sco_storage_owner_info* out);
    /* First name in binary order after `after` (NULL: start). SCO_NOT_FOUND leaves all outputs
     * untouched. inout_size is mandatory; out may be NULL only with capacity zero. Names are
     * NUL-terminated, including the NUL in the required size. SCO_TOO_MANY due to an undersized
     * name buffer changes only *inout_size to the required size, leaving out and info untouched.
     */
    sco_result (*next_table)(sco_plugin* self, const char* after, char* out,
                             uint32_t* inout_size, sco_storage_table_info* info);
    /* COUNT(*) on an ordinary own table; SCO_NOT_FOUND for excluded or absent objects. */
    sco_result (*row_count)(sco_plugin* self, const char* table, uint64_t* out_count);
    /* SQLite page accounting only: excludes WAL, filesystem file sizes and freelist reuse. */
    sco_result (*quota_info)(sco_plugin* self, sco_storage_quota_info* out);
} sco_storage_v2;

#ifdef __cplusplus
}
#endif
#endif /* SCO_STORAGE_V2_H */
