/* sc_actor_stats.h: the game.actor_stats service: read an actor's status stats (hunger, thirst,
 * temperature, oxygen, stun, distortion, radiation) by name. This header ships with the sco SDK;
 * the Star Citizen game pack publishes the service under the owner "game". To use it:
 *
 *   const sc_actor_stats_v1* stats = NULL;
 *   if (api->size > offsetof(sco_api, query_service) &&
 *       api->query_service(SC_ACTOR_STATS_NAME, SC_ACTOR_STATS_VERSION_1_0, (const void**)&stats) == SCO_OK) { ... }
 *
 * A later 1.x minor only appends functions; check size before calling one.
 *
 * Rules (docs/game/actor-stats.md):
 *  - Game thread only (a command, a tick or a run_on_game_thread task). From another thread read
 *    answers SCO_WRONG_THREAD without touching the game.
 *  - Read-only. Nothing is written, nothing is allocated across the boundary.
 *  - A stat is named by sc_actor_stat, never by the game's own index. The game pack maps the name
 *    to the game's stat table and checks, on every game build, that the game still calls that
 *    entry by the same name; if it doesn't, the capability is off instead of reading the wrong
 *    stat.
 *  - Values are the game's own floats in the game's own units. Their range, units and direction
 *    (for example whether hunger rises toward starving or falls from full) are not pinned yet:
 *    nothing here has been run in game (docs/game/actor-stats.md lists what an in-game run must
 *    confirm).
 *  - Capability (sco_api has()): "game.actor_stats.read", ready when its signature rows are OK
 *    on this game build. While it isn't, read answers SCO_UNAVAILABLE. */
#ifndef SC_ACTOR_STATS_H
#define SC_ACTOR_STATS_H

#include "sco_api.h"
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SC_ACTOR_STATS_NAME        "game.actor_stats"
#define SC_ACTOR_STATS_VERSION_1_0 0x00010000u

/* The stats 1.0 names. 4 bytes. 0 is never a stat; values past SC_ACTOR_STAT_SUIT_RADIATION are
 * reserved for later minors (read answers SCO_BAD_ARG for them in 1.0). */
typedef enum sc_actor_stat {
    SC_ACTOR_STAT_NONE = 0,
    SC_ACTOR_STAT_HUNGER = 1,
    SC_ACTOR_STAT_THIRST = 2,
    SC_ACTOR_STAT_BODY_TEMPERATURE = 3,
    SC_ACTOR_STAT_SUIT_TEMPERATURE = 4,
    SC_ACTOR_STAT_STUN = 5,
    SC_ACTOR_STAT_DISTORTION = 6,
    SC_ACTOR_STAT_OXYGEN = 7,           /* the actor's oxygen saturation */
    SC_ACTOR_STAT_BODY_RADIATION = 8,
    SC_ACTOR_STAT_SUIT_RADIATION = 9,
    SC_ACTOR_STAT_FORCE32 = 0x7fffffff
} sc_actor_stat;

#define SC_ACTOR_STAT_LAST_1_0 SC_ACTOR_STAT_SUIT_RADIATION

typedef struct sc_actor_stats_v1 {
    uint32_t size; /* sizeof(sc_actor_stats_v1) as the game pack built it */
    uint32_t _pad;
    /* *out_value = stat (an sc_actor_stat) of the actor whose entity id is id: your player's
     * (game.actors local_player's entity id) or an NPC's, once it has streamed in. 0 on failure.
     * SCO_BAD_ARG: id 0, out_value NULL, or stat not a 1.0 stat. SCO_NOT_FOUND: id isn't streamed
     * in, isn't an actor, or the actor has no record of that stat. SCO_UNAVAILABLE:
     * game.actor_stats.read isn't ready. SCO_FAILED: the game faulted or the value isn't finite. */
    sco_result (*read)(uint64_t id, uint32_t stat, float* out_value);
} sc_actor_stats_v1;

/* Calls read when the table has it; SCO_UNAVAILABLE for a NULL or short table. */
static inline sco_result sc_actor_stats_read(const sc_actor_stats_v1* t, uint64_t id, uint32_t stat, float* out_value) {
    return (t && t->size >= offsetof(sc_actor_stats_v1, read) + sizeof(t->read) && t->read)
               ? t->read(id, stat, out_value) : SCO_UNAVAILABLE;
}

#ifdef __cplusplus
}
#endif

#endif
