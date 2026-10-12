/* GPL-3.0-only. game.relocation 1.0: guarded local-player relocation.
 * Game thread only; self is the calling plugin's live handle. Native C and raw
 * C++ supported; C# and Lua unsupported. Table lifetime ends after plugin unload.
 * Check FULL member extent and non-NULL before calling. No arbitrary entity IDs,
 * pointers, ownership transfer, cross-system movement or networking.
 * Request requires host-certified offline admission, validated transport, a live
 * player, and KNOWN equal nonzero systems. Unknown safety/system refuses.
 * A streamed destination is required at admission. Later stream loss waits until
 * timeout; session/player/system change fails. Admission is NOT completion.
 * Tick drives bounded work; status only reads. Completed means a fresh observed
 * player pose matches target within 0.05m and quaternion |dot| >= 0.999999 in
 * the target frame. Cancellation cannot undo an already initiated movement.
 * One active request globally; 32 retained terminal tickets; oldest terminal may
 * be evicted on admission. Tickets are opaque, owner-scoped, never persist them.
 * Owner unload cancels and forgets tickets; stop fails pending and forgets all.
 * Outputs unchanged on refusal; output structs preserve size/unknown tail.
 * Production mutation currently unavailable: no certified host admission,
 * system classifier or validated relocation transport exists at this revision.
 * Synthetic provider tests prove shared state machinery, not engine behavior. */
#ifndef SC_RELOCATION_H
#define SC_RELOCATION_H
#include "sco_api.h"
#define SC_RELOCATION_NAME "game.relocation"
#define SC_RELOCATION_VERSION 0x00010000u
#define SC_RELOCATION_PENDING 1u
#define SC_RELOCATION_COMPLETE 2u
#define SC_RELOCATION_CANCELLED 3u
#define SC_RELOCATION_FAILED 4u
#define SC_RELOCATION_REASON_NONE 0u
#define SC_RELOCATION_REASON_TIMEOUT 1u
#define SC_RELOCATION_REASON_SESSION 2u
#define SC_RELOCATION_REASON_SAFETY 3u
#define SC_RELOCATION_REASON_SYSTEM 4u
#define SC_RELOCATION_REASON_TRANSPORT 5u
#define SC_RELOCATION_REASON_FAULT 6u

typedef struct sc_relocation_request {
    uint32_t size;
    uint32_t timeout_ms; /* 1..60000; provider monotonic clock, no caller deadline */
    uint64_t zone_id;    /* nonzero, streamed at admission */
    double pos[3];      /* finite, abs(component) <= 1e12 metres */
    double rot_xyzw[4]; /* finite norm >= 1e-12; normalized on acceptance */
} sc_relocation_request;
typedef struct sc_relocation_status {
    uint32_t size;
    uint32_t state;
    uint32_t reason;
    uint32_t applied; /* movement initiation occurred; cancellation is not undo */
    uint64_t ticket;
} sc_relocation_status;
typedef struct sc_relocation_v1 {
    uint32_t size;
    sco_result (*request)(sco_plugin* self, const sc_relocation_request* request, uint64_t* out_ticket);
    sco_result (*cancel)(sco_plugin* self, uint64_t ticket);
    sco_result (*status)(sco_plugin* self, uint64_t ticket, sc_relocation_status* out);
} sc_relocation_v1;
#endif
