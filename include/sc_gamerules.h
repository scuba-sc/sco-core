/* Read-only view of server GameRules parameters. No writes, no actions, no pointers cross this
 * ABI. The server owns these values; a plugin reads what the client received for the current
 * session. Table lifetime: game session; all methods game thread only.
 *
 * 1.1 adds list_params at the table tail: a static, copied registry of parameter definitions
 * (name, group, type). It carries no values, reads no memory and does not need the
 * game.gamerules.read capability or a backend; read is unchanged and stays unavailable without
 * a backend. See docs/game/gamerules.md for the exact enumeration rules. */
#ifndef SC_GAMERULES_H
#define SC_GAMERULES_H
#include "sco_api.h"
#include <stddef.h>
#include <stdint.h>
#define SC_GAMERULES_SERVICE_NAME "game.gamerules"
#define SC_GAMERULES_SERVICE_VERSION 0x00010000u  /* 1.0, frozen: what read-only callers query */
#define SC_GAMERULES_VERSION_1_0 0x00010000u
#define SC_GAMERULES_VERSION_1_1 0x00010001u /* + list_params */
#define SC_GAMERULES_PARAM_INT 1u
#define SC_GAMERULES_PARAM_FLOAT 2u
#define SC_GAMERULES_PARAM_MAX_PAGE 64u
#define SC_GAMERULES_PARAM_MAX_STRIDE 4096u
typedef struct sc_gamerules_state {
    uint32_t size;
    uint32_t valid;                          /* 1 = a parameter set has been received */
    uint64_t generation;                     /* opaque sync generation, not a session id */
    uint32_t enforce_group_qt;               /* server gate: group quantum travel enforced */
    uint32_t quantum_travel_allowed;         /* server gate: quantum travel allowed */
    uint32_t quantum_boost_allowed;          /* server gate: quantum boost allowed */
    float travel_target_threshold_angle;     /* degrees; 0 when unset */
    float target_lock_angular_player;
    float target_lock_angular_ai;
    float target_lock_linear_player;
    float target_lock_linear_ai;
    float boost_group_distance_threshold;
} sc_gamerules_state;
/* One parameter definition. The caller sets size >= sizeof(sc_gamerules_param_info) in every
 * slot it passes; list_params writes type, name and group only and leaves size and any bytes
 * past the known fields untouched. name and group are NUL-terminated copies; group is empty for
 * a top-level member. No engine pointer or offset is carried. */
typedef struct sc_gamerules_param_info {
    uint32_t size;
    uint32_t type;      /* SC_GAMERULES_PARAM_INT or SC_GAMERULES_PARAM_FLOAT */
    char name[48];
    char group[32];
} sc_gamerules_param_info;
typedef struct sc_gamerules_v1 {
    uint32_t size;
    uint32_t _pad;
    sco_result (*read)(sco_plugin* self, sc_gamerules_state* out);
    /* 1.1. Copies definitions [offset, offset+min(cap, total-offset)) in fixed registry order.
     * Slot i is at (char*)out + i*stride. cap 0 requires out NULL and counts only (stride
     * ignored); cap 1..SC_GAMERULES_PARAM_MAX_PAGE requires out non-NULL and stride in
     * [sizeof(sc_gamerules_param_info), SC_GAMERULES_PARAM_MAX_STRIDE]. out_count and out_total
     * must be distinct and non-NULL: *out_total is the registry size, *out_count the records
     * copied; the list is complete when offset + *out_count == *out_total. offset == total is
     * valid (count 0); offset > total, a bad cap, stride or out, a slot whose size is too small
     * or NULL out_count/out_total is SCO_BAD_ARG. Wrong thread is SCO_WRONG_THREAD, a NULL,
     * unknown or released self is SCO_BAD_ARG, a stopped service or runtime is SCO_UNAVAILABLE.
     * Every failure leaves all outputs unchanged. */
    sco_result (*list_params)(sco_plugin* self, uint32_t offset, sc_gamerules_param_info* out,
                              uint32_t stride, uint32_t cap, uint32_t* out_count,
                              uint32_t* out_total);
} sc_gamerules_v1;
static inline sco_result sc_gamerules_read(const sc_gamerules_v1* t, sco_plugin* self,
                                           sc_gamerules_state* out) {
    return (t && t->size >= offsetof(sc_gamerules_v1, read) + sizeof(t->read) && t->read && out)
               ? t->read(self, out) : SCO_UNAVAILABLE;
}
static inline sco_result sc_gamerules_list_params(const sc_gamerules_v1* t, sco_plugin* self,
                                                  uint32_t offset, sc_gamerules_param_info* out,
                                                  uint32_t stride, uint32_t cap,
                                                  uint32_t* out_count, uint32_t* out_total) {
    return (t && t->size >= offsetof(sc_gamerules_v1, list_params) + sizeof(t->list_params) &&
            t->list_params)
               ? t->list_params(self, offset, out, stride, cap, out_count, out_total)
               : SCO_UNAVAILABLE;
}
#endif
