/* GPL-3.0-only. game.relocation 1.0: guarded local-player relocation.
 * Game thread only; self is the calling plugin's live handle. Native C and raw
 * C++ supported; C# and Lua unsupported. Table lifetime ends after plugin unload.
 * Check FULL member extent and non-NULL before calling. No arbitrary entity IDs,
 * pointers, ownership transfer, cross-system movement or networking.
 * Request requires host-certified offline admission, validated transport, a live
 * player, and KNOWN equal nonzero systems. Unknown safety/system refuses.
 * A streamed destination is required at admission. Later stream loss waits until
 * timeout; session/player/system change fails. Admission is NOT completion.
 * Tick drives bounded work; status only reads. For the frozen 1.0 request,
 * Completed means a fresh observed
 * player pose matches target within 0.05m and quaternion |dot| >= 0.999999 in
 * the target frame. Cancellation cannot undo an already initiated movement.
 * One active request globally; 32 retained terminal tickets; oldest terminal may
 * be evicted on admission. Tickets are opaque, owner-scoped, never persist them.
 * Owner unload cancels and forgets tickets; stop fails pending and forgets all.
 * Outputs unchanged on refusal; output structs preserve size/unknown tail.
 * FullPose native readiness is conditional on the installed backend and fresh
 * host admission. Resolved rows are not evidence of engine behavior; the native
 * adapter does not supply the separate 1.1 position operation below.
 * Synthetic provider tests prove shared state machinery, not engine behavior. */
#ifndef SC_RELOCATION_H
#define SC_RELOCATION_H
#include "sco_api.h"
#define SC_RELOCATION_NAME "game.relocation"
#define SC_RELOCATION_VERSION 0x00010000u
#define SC_RELOCATION_VERSION_1_0 0x00010000u
#define SC_RELOCATION_VERSION_1_1 0x00010001u
#define SC_RELOCATION_POSITION_CAPABILITY "game.relocation.position"
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
#define SC_RELOCATION_REASON_TARGET 7u /* 1.1 position target identity changed */

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

/* 1.1 position mutation and completion, without a requested orientation. This is
 * a separate operation: never emulate it with a full-pose request. Requires its
 * own capability and verified position backend; the legacy native adapter does
 * not supply one. Native C/raw C++ only; no C#/Lua mutation route.
 *
 * target_epoch is the current nonzero game.spatial observation epoch for this
 * zone. Refresh zone_record immediately before admission. It is checked only at
 * admission, not retained as a target lifetime across ticks. The backend must
 * separately attest the target instance/ancestry identity during pending work.
 * Known equal nonzero systems and all 1.0 offline/player/ownership guards apply.
 * Position is finite and bounded as above; timeout remains 1..60000 ms.
 *
 * After position-only initiation, a later tick must freshly observe position in
 * the admitted target frame within 0.05m. Orientation is neither requested nor
 * tested; this does not certify unrelated engine/physics orientation behavior.
 * A target identity change fails with REASON_TARGET. Stream loss waits only to
 * timeout; reappearance with a different identity fails. Status/cancel, global
 * one-pending/32-terminal quotas, no retry/rollback and owner cleanup are shared
 * with 1.0 tickets. Outputs remain unchanged on refusal. */
typedef struct sc_relocation_position_request {
    uint32_t size;
    uint32_t timeout_ms;
    uint64_t zone_id;
    double pos[3];
    uint64_t target_epoch;
} sc_relocation_position_request;
typedef struct sc_relocation_v1_1 {
    uint32_t size;
    sco_result (*request)(sco_plugin* self, const sc_relocation_request* request, uint64_t* out_ticket);
    sco_result (*cancel)(sco_plugin* self, uint64_t ticket);
    sco_result (*status)(sco_plugin* self, uint64_t ticket, sc_relocation_status* out);
    /* Full extent and non-NULL required. BAD_ARG for invalid size/epoch/zone,
     * timeout or position. UNAVAILABLE for stale epoch, missing safety/backend
     * or unreadable target authority; remaining results follow 1.0. */
    sco_result (*request_position)(sco_plugin* self, const sc_relocation_position_request* request,
                                   uint64_t* out_ticket);
} sc_relocation_v1_1;
#endif
