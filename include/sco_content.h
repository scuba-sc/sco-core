/* sco.content 1.0: copied, read-only list/script data from enabled data packs.
 * GPL-3.0-only. See docs/design/content-access.md and sdk/docs/data-packs.md.
 * Plain C, 64-bit hosts; discovered through unchanged sco_api 1.1 query_service.
 * All calls: game thread only, live self required. No file IO during a call.
 * Table storage lives for the process; Stop/terminal shutdown returns UNAVAILABLE.
 * Contents are a startup snapshot; change packs only while the host is stopped.
 * Script records include XML files below scripts/ and, on hosts after SDK 2.4 RC1,
 * missions/<id>.scom (id: 1..100 lowercase ASCII letters, digits or underscores).
 * File bytes are opaque; consumers validate their formats. Private .cwmission is not exposed.
 * No implicit base files, script execution, ownership transfer or reload support.
 * Check complete callback extents AND non-null pointers before using any table.
 */
#ifndef SCO_CONTENT_H
#define SCO_CONTENT_H
#include "sco_api.h"
#ifdef __cplusplus
extern "C" {
#endif
#define SCO_CONTENT_NAME "sco.content"
#define SCO_CONTENT_VERSION_1_0 0x00010000u
#define SCO_CONTENT_MAX_FILE_BYTES (1024u * 1024u)
#define SCO_CONTENT_MAX_TOTAL_BYTES (16u * 1024u * 1024u)
#define SCO_CONTENT_MAX_ITEMS 4096u
#define SCO_CONTENT_MAX_PACK_BYTES 63u
#define SCO_CONTENT_MAX_NAME_BYTES 1023u

typedef enum sco_content_kind {
    SCO_CONTENT_SCRIPT = 2,
    SCO_CONTENT_LIST = 3,
    SCO_CONTENT_KIND_FORCE32 = 0x7fffffff
} sco_content_kind;

/* Fixed 1.0 record. Caller sets size >= sizeof(sco_content_item); provider writes
 * only sizeof(record), including size. Strings are copied UTF-8, NUL-terminated.
 * name is pack-relative, never a host path. id is opaque, process-unique and
 * valid until host Stop; duplicate names in different packs have distinct IDs. */
typedef struct sco_content_item {
    uint32_t size;
    sco_content_kind kind;
    uint64_t id;
    char pack[SCO_CONTENT_MAX_PACK_BYTES + 1u];
    char name[SCO_CONTENT_MAX_NAME_BYTES + 1u];
} sco_content_item;

typedef struct sco_content_v1 {
    uint32_t size;
    uint32_t _pad;
    /* Zero-based ordinal within kind. Discovery plugin order then name byte order.
     * NOT_FOUND: end. BAD_ARG: unsupported kind, null/short output, bad self.
     * Every failed call leaves out untouched. */
    sco_result (*item)(sco_plugin* self, sco_content_kind kind, uint32_t ordinal, sco_content_item* out);
    /* Exact bytes, no added NUL. *inout_size is capacity on entry, bytes written
     * on OK, bytes needed on TOO_MANY. NULL out with zero capacity asks size;
     * empty file returns OK/0. BAD_ARG: null size, NULL out with nonzero capacity,
     * bad self. NOT_FOUND: unknown/stale id. Failed calls leave bytes untouched;
     * only TOO_MANY changes size. Contents may contain embedded NULs. */
    sco_result (*read)(sco_plugin* self, uint64_t id, void* out, uint32_t* inout_size);
} sco_content_v1;
#ifdef __cplusplus
}
#endif
#endif
