/* Read-only view of server GameRules parameters. No writes, no actions, no pointers cross this
 * ABI. The server owns these values; a plugin reads what the client received for the current
 * session. Table lifetime: game session; all methods game thread only.
 * See docs/game/gamerules.md. */
#ifndef SC_GAMERULES_H
#define SC_GAMERULES_H
#include "sco_api.h"
#include <stddef.h>
#include <stdint.h>
#define SC_GAMERULES_SERVICE_NAME "game.gamerules"
#define SC_GAMERULES_SERVICE_VERSION 0x00010000u
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
typedef struct sc_gamerules_v1 {
    uint32_t size;
    uint32_t _pad;
    sco_result (*read)(sco_plugin* self, sc_gamerules_state* out);
} sc_gamerules_v1;
static inline sco_result sc_gamerules_read(const sc_gamerules_v1* t, sco_plugin* self,
                                           sc_gamerules_state* out) {
    return (t && t->size >= offsetof(sc_gamerules_v1, read) + sizeof(t->read) && t->read && out)
               ? t->read(self, out) : SCO_UNAVAILABLE;
}
#endif
