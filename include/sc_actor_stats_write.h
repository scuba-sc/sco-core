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
 *    blood-drug changes while their pl_status.enable* switch is off: *out_after is the value read
 *    back right after the write, so you see what took effect.
 *  - The game applies a change only where your entity has authority, or where that stat's
 *    definition allows a local change without it. mode() tells you which before you write; a write
 *    the game would discard answers SCO_UNAVAILABLE instead of a silent SCO_OK.
 *  - Nothing is restored when your plugin unloads: these stats are gameplay state the game itself
 *    changes every tick (eating, breathing, weather), not settings.
 *  - Nothing here has been run in game; whether a write persists in an offline session (in
 *    particular a SC_ACTOR_STAT_WRITE_LOCAL_ONLY one) is what the in-game run must confirm. */
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

/* mode's answers. 4 bytes. */
typedef enum sc_actor_stat_write_mode {
    SC_ACTOR_STAT_WRITE_DISCARDED = 0,     /* the game would ignore a change here: adjust/set refuse */
    SC_ACTOR_STAT_WRITE_AUTHORITATIVE = 1, /* your entity has authority: the game applies the change */
    SC_ACTOR_STAT_WRITE_LOCAL_ONLY = 2,    /* no authority, but the stat allows a local change: applied
                                            * here, and an authority (if any) may overwrite it */
    SC_ACTOR_STAT_WRITE_MODE_FORCE32 = 0x7fffffff
} sc_actor_stat_write_mode;

typedef struct sc_actor_stats_write_v1 {
    uint32_t size; /* sizeof(sc_actor_stats_write_v1) as the game pack built it */
    uint32_t _pad;
    /* Adds delta to stat (an sc_actor_stat) of your player through the game's adjust function.
     * *out_after (may be NULL) = the stat read back right after the write, 0 on failure.
     * SCO_BAD_ARG: a null or released self, id 0 or not your player, stat not a 1.0 stat, delta not
     * finite or past SC_ACTOR_STATS_WRITE_MAX_STEP. SCO_NOT_FOUND: you haven't spawned, or your
     * actor has no record of that stat. SCO_UNAVAILABLE: game.actor_stats.write isn't ready, the
     * session isn't certified offline, or mode() is SC_ACTOR_STAT_WRITE_DISCARDED. SCO_FAILED: the
     * game faulted. */
    sco_result (*adjust)(sco_plugin* self, uint64_t id, uint32_t stat, float delta, float* out_after);
    /* Moves stat to value: reads the current value and adjusts by the difference, so the game's
     * clamping and consequences still apply. The same answers as adjust; SCO_BAD_ARG also when
     * value isn't finite or the step is past SC_ACTOR_STATS_WRITE_MAX_STEP. */
    sco_result (*set)(sco_plugin* self, uint64_t id, uint32_t stat, float value, float* out_after);
    /* *out_mode = an sc_actor_stat_write_mode: whether the game would apply a change to stat of your
     * player right now. Writes nothing; needs no offline admission. The same SCO_BAD_ARG /
     * SCO_NOT_FOUND / SCO_UNAVAILABLE (capability) answers as adjust; out_mode NULL is SCO_BAD_ARG. */
    sco_result (*mode)(sco_plugin* self, uint64_t id, uint32_t stat, uint32_t* out_mode);
} sc_actor_stats_write_v1;

static inline sco_result sc_actor_stats_adjust(const sc_actor_stats_write_v1* t, sco_plugin* self, uint64_t id, uint32_t stat,
                                               float delta, float* out_after) {
    return (t && t->size >= offsetof(sc_actor_stats_write_v1, adjust) + sizeof(t->adjust) && t->adjust)
               ? t->adjust(self, id, stat, delta, out_after) : SCO_UNAVAILABLE;
}

static inline sco_result sc_actor_stats_set(const sc_actor_stats_write_v1* t, sco_plugin* self, uint64_t id, uint32_t stat,
                                            float value, float* out_after) {
    return (t && t->size >= offsetof(sc_actor_stats_write_v1, set) + sizeof(t->set) && t->set)
               ? t->set(self, id, stat, value, out_after) : SCO_UNAVAILABLE;
}

static inline sco_result sc_actor_stats_write_mode(const sc_actor_stats_write_v1* t, sco_plugin* self, uint64_t id, uint32_t stat,
                                                   uint32_t* out_mode) {
    return (t && t->size >= offsetof(sc_actor_stats_write_v1, mode) + sizeof(t->mode) && t->mode)
               ? t->mode(self, id, stat, out_mode) : SCO_UNAVAILABLE;
}

#ifdef __cplusplus
}
#endif

#endif
