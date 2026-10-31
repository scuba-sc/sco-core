/* SPDX-License-Identifier: GPL-3.0-only
 * Candidate fixed-document ASOP selection service. This table observes host-owned copied
 * configuration; its presence does not enable or certify a native terminal producer.
 * All calls require a live owner and the game thread. Output remains untouched on error.
 */
#ifndef SC_ASOP_SELECTION_H
#define SC_ASOP_SELECTION_H
#include "sco_api.h"
#ifdef __cplusplus
extern "C" {
#endif
#define SC_ASOP_SELECTION_NAME "game.asop.selection"
#define SC_ASOP_SELECTION_VERSION 0x00010000u
#define SC_ASOP_SELECTION_FILE_KNOWN 0x01u
#define SC_ASOP_SELECTION_FILE_PRESENT 0x02u
#define SC_ASOP_SELECTION_PUBLISHED_KNOWN 0x04u
#define SC_ASOP_SELECTION_OBSERVED_KNOWN 0x08u
#define SC_ASOP_SELECTION_FILE_ERROR 0x10u
#define SC_ASOP_SELECTION_REASON_NONE 0u
#define SC_ASOP_SELECTION_REASON_IO 1u
#define SC_ASOP_SELECTION_REASON_INVALID 2u
#define SC_ASOP_SELECTION_REASON_CONFLICT 3u
#define SC_ASOP_SELECTION_REASON_OUTCOME_UNKNOWN 4u
#define SC_ASOP_SELECTION_REASON_BUSY 5u
#define SC_ASOP_SELECTION_EDIT_ACCEPTED 1u
#define SC_ASOP_SELECTION_FILE_PREPARING 2u
#define SC_ASOP_SELECTION_FILE_COMMITTED 3u
#define SC_ASOP_SELECTION_EDIT_CONFLICT 4u
#define SC_ASOP_SELECTION_EDIT_REVOKED 5u
#define SC_ASOP_SELECTION_EDIT_UNKNOWN 6u
#define SC_ASOP_SELECTION_FILE_COMMITTING 7u
#define SC_ASOP_SELECTION_MAX_CLASSES 2048u
#define SC_ASOP_SELECTION_MAX_CLASS_BYTES 63u
#define SC_ASOP_SELECTION_MAX_FILE_BYTES 262144u
#define SC_ASOP_SELECTION_ROW_BUSY 0x01u
#define SC_ASOP_SELECTION_ROWS_FILE 1u
#define SC_ASOP_SELECTION_ROWS_CATALOGUE 2u
#define SC_ASOP_SELECTION_MAX_PAGE 64u
#define SC_ASOP_SELECTION_MAX_STRIDE 4096u

typedef struct sc_asop_selection_state {
    uint32_t size;
    uint32_t flags;
    uint64_t session;
    uint64_t file_revision;
    uint64_t catalogue_generation;
    uint64_t semantic_revision;
    uint32_t file_count;
    uint32_t reason;
    uint8_t file_digest[32]; /* SHA-256 of exact known bytes; check FILE_KNOWN */
} sc_asop_selection_state;

typedef struct sc_asop_selection_read {
    uint32_t size, kind;
    uint64_t session, revision;
    uint32_t offset, reserved;
} sc_asop_selection_read;
typedef struct sc_asop_selection_row {
    uint32_t size, flags;
    char class_name[64];
} sc_asop_selection_row;

typedef struct sc_asop_selection_class {
    uint32_t size, flags;
    char class_name[64];
} sc_asop_selection_class;
typedef struct sc_asop_selection_request {
    uint32_t size, flags;
    uint64_t session, file_revision, client_key;
    uint8_t file_digest[32];
    const sc_asop_selection_class* classes;
    uint32_t count, class_stride;
} sc_asop_selection_request;
typedef struct sc_asop_selection_receipt {
    uint32_t size, phase;
    uint64_t session, request_id, client_key, file_revision;
    uint32_t reason, reserved;
} sc_asop_selection_receipt;

typedef struct sc_asop_selection_v1 {
    uint32_t size;
    uint32_t _pad;
    sco_result (*state)(sco_plugin* self, sc_asop_selection_state* out);
    /* Re-read the fixed document. Errors do not turn unavailable bytes into an empty list. */
    sco_result (*refresh)(sco_plugin* self);
    /* Copied generation-checked rows; all output is untouched on failure. */
    sco_result (*rows)(sco_plugin* self, const sc_asop_selection_read* read,
                       sc_asop_selection_row* out, uint32_t stride, uint32_t capacity,
                       uint32_t* out_count, uint32_t* out_more);
    /* One editor lease per host session. A repeated acquire by its owner is idempotent. */
    sco_result (*acquire)(sco_plugin* self, uint64_t session, uint64_t* out_lease);
    sco_result (*release)(sco_plugin* self, uint64_t lease);
    /* Accepted is not committed: the host processes the document job separately. */
    sco_result (*replace)(sco_plugin* self, uint64_t lease, const sc_asop_selection_request* request, uint64_t* out_request);
    sco_result (*status)(sco_plugin* self, uint64_t lease, uint64_t request_id, sc_asop_selection_receipt* out);
} sc_asop_selection_v1;
#ifdef __cplusplus
}
#endif
#endif
