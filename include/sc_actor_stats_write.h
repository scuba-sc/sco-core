/* sc_actor_stats_write.h: the game.actor_stats_write service: change your own player's status
 * stats (hunger, thirst, temperature, oxygen, stun, distortion, radiation) through the game's own
 * stat-adjust function. A separate table from game.actor_stats (sc_actor_stats.h) so a plugin
 * that only reads never sees a write route. To use it:
 *
 *   const sc_actor_stats_write_v1* w = NULL;
 *   if (api->query_service(SC_ACTOR_STATS_WRITE_NAME, SC_ACTOR_STATS_WRITE_VERSION_1_0, (const void**)&w) == SCO_OK) { ... }
 *
 * Rules (docs/game/actor-stats.md, "Writes"):
 *  - OFF by default. The capability "game.actor_stats.write" is ready only when the product (or
 *    the maintainer's test opt-in) has switched stat writes on, its signature rows are OK and
 *    game.actor_stats.read is ready. While it isn't, adjust and set answer SCO_UNAVAILABLE.
 *  - Offline only: every write first asks the host for fresh offline admission; a session that
 *    isn't certified offline answers SCO_UNAVAILABLE and nothing is written.
 *  - Your own player only in 1.0: id must be game.actors local_player's entity id.
 *  - Game thread only; self is your plugin's handle as sco_plugin_load received it.
 *  - The write goes through the game's own adjust function, so the game clamps the value and runs
 *    its own consequences (a stat change can incapacitate). The game ignores hunger, thirst and
 *    blood-drug changes while their pl_status.enable* switch is off: read the stat back to see
 *    what took effect.
 *  - Nothing is restored when your plugin unloads: these stats are gameplay state the game itself
 *    changes every tick (eating, breathing, weather), not settings.
 *  - Nothing here has been run in game; whether a write persists in an offline session is what the
 *    in-game run must confirm. */
#ifndef SC_ACTOR_STATS_WRITE_H
#define SC_ACTOR_STATS_WRITE_H

#include "sc_actor_stats.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SC_ACTOR_STATS_WRITE_NAME        "game.actor_stats_write"
#define SC_ACTOR_STATS_WRITE_VERSION_1_0 0x00010000u

/* The largest |delta| adjust accepts, and the largest |value - current| set applies. */
#define SC_ACTOR_STATS_WRITE_MAX_STEP 1000000.0f

typedef struct sc_actor_stats_write_v1 {
    uint32_t size; /* sizeof(sc_actor_stats_write_v1) as the game pack built it */
    uint32_t _pad;
    /* Adds delta to stat (an sc_actor_stat) of your player through the game's adjust function.
     * SCO_BAD_ARG: a null or released self, id 0 or not your player, stat not a 1.0 stat, delta not
     * finite or past SC_ACTOR_STATS_WRITE_MAX_STEP. SCO_NOT_FOUND: you haven't spawned, or your
     * actor has no record of that stat. SCO_UNAVAILABLE: game.actor_stats.write isn't ready, or the
     * session isn't certified offline. SCO_FAILED: the game faulted. */
    sco_result (*adjust)(sco_plugin* self, uint64_t id, uint32_t stat, float delta);
    /* Moves stat to value: reads the current value and adjusts by the difference, so the game's
     * clamping and consequences still apply. The same answers as adjust; SCO_BAD_ARG also when
     * value isn't finite or the step is past SC_ACTOR_STATS_WRITE_MAX_STEP. */
    sco_result (*set)(sco_plugin* self, uint64_t id, uint32_t stat, float value);
} sc_actor_stats_write_v1;

static inline sco_result sc_actor_stats_adjust(const sc_actor_stats_write_v1* t, sco_plugin* self, uint64_t id, uint32_t stat, float delta) {
    return (t && t->size >= offsetof(sc_actor_stats_write_v1, adjust) + sizeof(t->adjust) && t->adjust)
               ? t->adjust(self, id, stat, delta) : SCO_UNAVAILABLE;
}

static inline sco_result sc_actor_stats_set(const sc_actor_stats_write_v1* t, sco_plugin* self, uint64_t id, uint32_t stat, float value) {
    return (t && t->size >= offsetof(sc_actor_stats_write_v1, set) + sizeof(t->set) && t->set)
               ? t->set(self, id, stat, value) : SCO_UNAVAILABLE;
}

#ifdef __cplusplus
}
#endif

#endif
