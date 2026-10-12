/* Guarded offline mission service. GPL-3.0-only. See docs/design/offline-missions.md.
 * Size-prefixed copied records; no game pointers, accounts, file paths or script execution.
 * All methods require the game thread and a live plugin handle. Output tail bytes are untouched.
 * Unknown/online/EAC-present/shutdown admission or a missing backend returns UNAVAILABLE.
 * Native callers must check the complete table member extent and a non-null pointer.
 * Tables are host-session lifetime; no provider reload or mutation Lua/C# route is implied. */
#ifndef SC_MISSIONS_H
#define SC_MISSIONS_H
#include "sco_api.h"
#ifdef __cplusplus
extern "C" {
#endif
#define SC_MISSIONS_SERVICE_NAME "game.missions"
#define SC_MISSIONS_SERVICE_VERSION 0x00010000u
#define SC_MISSIONS_MAX_OFFERS 256u
#define SC_MISSIONS_MAX_STEPS 64u
#define SC_MISSIONS_MAX_ACTIVE 64u
/* State values are frozen for 1.x. */
typedef enum sc_mission_state {
    SC_MISSION_ACTIVE = 1, SC_MISSION_COMPLETED = 2, SC_MISSION_ABANDONED = 3,
    SC_MISSION_STATE_FORCE32 = 0x7fffffff
} sc_mission_state;
typedef struct sc_mission_offer {
    uint32_t size;
    uint32_t flags; /* reserved: zero */
    uint64_t offer_id;
    uint64_t broker_id;
    uint32_t phase_count;
    uint32_t objectives_in_phase; /* this narrow contract supports uniform phase widths */
    int64_t local_reward; /* advisory only; never grants payment authority */
    uint64_t generation; /* changes invalidate old offers */
} sc_mission_offer;
typedef struct sc_mission_status {
    uint32_t size;
    uint32_t state;
    uint64_t mission_id;
    uint64_t offer_id;
    uint32_t phase;
    uint32_t objective; /* completed objectives in current phase */
    uint32_t phase_count;
    uint32_t objectives_in_phase;
    uint64_t session;
} sc_mission_status;
typedef struct sc_missions_v1 {
    uint32_t size;
    uint32_t _pad;
    /* index < MAX_OFFERS; NOT_FOUND at end; output.size >= sizeof(record). */
    sco_result (*offer)(sco_plugin* self, uint32_t index, sc_mission_offer* out);
    /* Copied, sanitized status for an opaque mission ID. NOT_FOUND for unknown ID. */
    sco_result (*status)(sco_plugin* self, uint64_t mission_id, sc_mission_status* out);
    /* Current offer/broker/generation only. key is owner/session-scoped; output unchanged on failure.
     * Identical key+offer replay returns original ID; altered replay BAD_ARG; 64 receipts, no eviction. */
    sco_result (*accept)(sco_plugin* self, const sc_mission_offer* offer, uint64_t key, uint64_t* out_mission);
    sco_result (*objective)(sco_plugin* self, uint64_t mission_id, uint32_t phase, uint32_t objective);
    sco_result (*advance)(sco_plugin* self, uint64_t mission_id, uint32_t expected_phase);
} sc_missions_v1;
#ifdef __cplusplus
}
#endif
#endif
