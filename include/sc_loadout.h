/* SPDX-License-Identifier: GPL-3.0-only
 * Typed local-player offline equip only. No XML, files, pointers to game state,
 * entitlement/account inventory, network or persistence API. See docs/game/loadout.md. */
#ifndef SC_LOADOUT_H
#define SC_LOADOUT_H
#include "sco_api.h"
#ifdef __cplusplus
extern "C" {
#endif
#define SC_LOADOUT_SERVICE_NAME "game.loadout"
#define SC_LOADOUT_SERVICE_VERSION 0x00010000u
#define SC_LOADOUT_VERSION_1_0 0x00010000u
#define SC_LOADOUT_VERSION_1_1 0x00010001u /* + weapon and multitool slots 8-21 */
#define SC_LOADOUT_VERSION_1_2 0x00010002u /* + bounded asynchronous jobs */
#define SC_LOADOUT_MAX_JOBS 8u
#define SC_LOADOUT_TIMEOUT_MS 120000u
/* Respawn-completed notification (additive; sc_loadout_v1 is unchanged, no table member). Bus event
 * name only: this header is deliberately usable on its own, so a plugin that includes just
 * sc_loadout.h can subscribe with sco_api's subscribe. The payload struct and the same name live in
 * sc_game_events.h (sc_game_loadout_respawn_completed, size-prefixed, occurrence only; both headers
 * guard the macro, so include order does not matter). Gated by capability
 * "game.events.loadout_respawn_completed", which is OFF in production: the native event's
 * provenance and calling convention are unverified (F-INTAKE-004, T-3), not run in game. An event
 * whose capability is off never fires, so subscribing is harmless. See docs/game/loadout.md. */
#ifndef SC_GAME_EVENT_LOADOUT_RESPAWN_COMPLETED
#define SC_GAME_EVENT_LOADOUT_RESPAWN_COMPLETED "game.loadout.respawn_completed"
#endif
#define SC_LOADOUT_MAX_ITEMS 16u
#define SC_LOADOUT_ITEM_NAME_BYTES 96u
typedef enum sc_loadout_slot {
    SC_LOADOUT_SLOT_HEAD = 1, SC_LOADOUT_SLOT_TORSO = 2,
    SC_LOADOUT_SLOT_ARMS = 3, SC_LOADOUT_SLOT_LEGS = 4,
    SC_LOADOUT_SLOT_UNDERSUIT = 5, SC_LOADOUT_SLOT_BACKPACK = 6,
    SC_LOADOUT_SLOT_VISOR = 7,
    /* 1.1: weapon and multitool slots. Logical slots, not engine port names.
     * 8/9 are the weapons themselves; 10-21 target the item in the parent port
     * when processed, so put 8/9 before 10-21 in a batch. Values 22+ unassigned. */
    SC_LOADOUT_SLOT_PRIMARY = 8, SC_LOADOUT_SLOT_SIDEARM = 9,
    SC_LOADOUT_SLOT_PRIMARY_OPTIC = 10, SC_LOADOUT_SLOT_PRIMARY_BARREL = 11,
    SC_LOADOUT_SLOT_PRIMARY_UNDERBARREL = 12,
    SC_LOADOUT_SLOT_SIDEARM_OPTIC = 13, SC_LOADOUT_SLOT_SIDEARM_BARREL = 14,
    SC_LOADOUT_SLOT_SIDEARM_UNDERBARREL = 15,
    SC_LOADOUT_SLOT_PRIMARY_TOOL_MODULE = 16, SC_LOADOUT_SLOT_PRIMARY_TOOL_CANISTER = 17,
    SC_LOADOUT_SLOT_PRIMARY_TOOL_BATTERY = 18,
    SC_LOADOUT_SLOT_SIDEARM_TOOL_MODULE = 19, SC_LOADOUT_SLOT_SIDEARM_TOOL_CANISTER = 20,
    SC_LOADOUT_SLOT_SIDEARM_TOOL_BATTERY = 21,
    SC_LOADOUT_SLOT_FORCE32 = 0x7fffffff
} sc_loadout_slot;
typedef enum sc_loadout_status {
    SC_LOADOUT_APPLIED = 0, SC_LOADOUT_UNSUPPORTED = 1,
    SC_LOADOUT_ITEM_MISSING = 2, SC_LOADOUT_FAILED = 3,
    SC_LOADOUT_UNAVAILABLE = 4, SC_LOADOUT_STATUS_FORCE32 = 0x7fffffff
} sc_loadout_status;
/* Frozen 1.x records, unchanged in 1.1. item_class: NUL-terminated ASCII [A-Za-z0-9_], 1..95 bytes.
 * Unknown slot numbers are supported input: report UNSUPPORTED, do not call engine. */
typedef struct sc_loadout_item {
    uint32_t slot; uint32_t _pad; char item_class[SC_LOADOUT_ITEM_NAME_BYTES];
} sc_loadout_item;
typedef struct sc_loadout_slot_result {
    uint32_t slot; sc_loadout_status status;
} sc_loadout_slot_result;
typedef struct sc_loadout_request {
    uint32_t size; uint32_t count; uint32_t item_stride; uint32_t _pad;
    const sc_loadout_item* items;
} sc_loadout_request;
typedef struct sc_loadout_report {
    uint32_t size; uint32_t count; uint32_t applied; uint32_t _pad;
    sc_loadout_slot_result slots[SC_LOADOUT_MAX_ITEMS];
} sc_loadout_report;
typedef struct sc_loadout_v1 {
    uint32_t size; uint32_t _pad;
    /* Game thread only; self must be live. request/report require full v1 extent.
     * 1..16 items, exact item_stride; duplicate slots or malformed names -> BAD_ARG,
     * >16 -> TOO_MANY. Structural rejection leaves report untouched; no engine calls.
     * Safety/provider refusal -> UNAVAILABLE and one UNAVAILABLE result per item.
     * Valid batch processed in order: OK iff all APPLIED, FAILED for partial/all slot
     * refusals. No rollback: previously applied slots remain equipped, not owned by
     * the plugin. Caller pointers borrowed only for the synchronous call. Recursive
     * apply returns UNAVAILABLE; a backend fault disables apply until process restart. */
    sco_result (*apply)(sco_plugin* self, const sc_loadout_request* request, sc_loadout_report* report);
} sc_loadout_v1;
typedef enum sc_loadout_job_state {
    SC_LOADOUT_JOB_PENDING = 0, SC_LOADOUT_JOB_COMPLETE = 1,
    SC_LOADOUT_JOB_FAILED = 2, SC_LOADOUT_JOB_STATE_FORCE32 = 0x7fffffff
} sc_loadout_job_state;
typedef struct sc_loadout_job_report {
    uint32_t size; sc_loadout_job_state state;
    uint32_t completed; uint32_t _pad;
    sc_loadout_report report;
} sc_loadout_job_report;
/* Unchanged v1 prefix. Query 1.2 and check full tail extent before use.
 * submit copies the validated batch into one owner-scoped retained job (8 total).
 * Game-thread ticks process slots in order; APPLIED requires backend readback.
 * COMPLETE means all slots processed, not necessarily successful. FAILED denotes
 * admission/session loss, timeout or fault. cancel/unload forget pending work,
 * never roll back confirmed equipment. Caller pointers are never retained. */
typedef struct sc_loadout_v1_2 {
    uint32_t size; uint32_t _pad;
    sco_result (*apply)(sco_plugin*, const sc_loadout_request*, sc_loadout_report*);
    sco_result (*submit)(sco_plugin*, const sc_loadout_request*, uint64_t* out_job);
    sco_result (*status)(sco_plugin*, uint64_t job, sc_loadout_job_report*);
    sco_result (*cancel)(sco_plugin*, uint64_t job);
} sc_loadout_v1_2;
#ifdef __cplusplus
}
#endif
#endif
