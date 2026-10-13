/* GPL-3.0-only. Offline local-economy boundary; no accounts/entitlements/online balance.
 * Game thread, live owner, current offline/EAC-absent session; all outputs copied on success.
 * Plugin owns persistence/import/save policy. No automatic reward or file access.
 * Complete member extent + non-null pointer required before each native call. */
#ifndef SC_ECONOMY_H
#define SC_ECONOMY_H
#include "sco_api.h"
#ifdef __cplusplus
extern "C" {
#endif
#define SC_ECONOMY_SERVICE_NAME "game.economy"
#define SC_ECONOMY_SERVICE_VERSION 0x00010000u
#define SC_ECONOMY_MAX_RECEIPTS 128u
#define SC_ECONOMY_BALANCE_LIMIT INT64_C(1000000000000)
#define SC_ECONOMY_TRANSACTION_EVENT "game.economy.transaction"
#define SC_ECONOMY_SIGNAL_DROPPED 1u /* receipt flags: payment committed, advisory notification unavailable */
typedef struct sc_economy_balance {
    uint32_t size;
    uint32_t _pad;
    int64_t local_balance;
} sc_economy_balance;
typedef struct sc_economy_request {
    uint32_t size;
    uint32_t flags; /* reserved: zero */
    uint64_t key; /* nonzero owner/session-scoped once-only key */
    int64_t expected_balance;
    int64_t delta; /* nonzero, strictly within +/- BALANCE_LIMIT */
} sc_economy_request;
typedef struct sc_economy_receipt {
    uint32_t size;
    uint32_t flags; /* SC_ECONOMY_SIGNAL_DROPPED or zero */
    uint64_t key;
    uint64_t session;
    int64_t before;
    int64_t after;
    int64_t delta;
    uint64_t sequence; /* monotonically increasing process-local signal correlation */
} sc_economy_receipt;
/* Synchronous copied event, callback-only lifetime. No accounts/paths/player IDs.
 * Public table calls during notification refuse reentrancy; copy and post later work.
 * New success attempts one advisory signal; replay never repeats it. Receipt is authoritative.
 * A suppressed notification sets SC_ECONOMY_SIGNAL_DROPPED; caller buffers live until return. */
typedef struct sc_economy_event {
    uint32_t size;
    uint32_t _pad;
    sc_economy_receipt receipt;
    char plugin_id[32];
} sc_economy_event;
typedef struct sc_economy_v1 {
    uint32_t size;
    uint32_t _pad;
    sco_result (*balance)(sco_plugin* self, sc_economy_balance* out);
    sco_result (*apply)(sco_plugin* self, const sc_economy_request* request, sc_economy_receipt* out);
    sco_result (*receipt)(sco_plugin* self, uint64_t key, sc_economy_receipt* out);
} sc_economy_v1;
#ifdef __cplusplus
}
#endif
#endif
