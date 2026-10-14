/* sc_reputation.h: the game.reputation service: a read-only look at the game's reputation-service
 * checks (how many the game pack located on this build, how many still hold the game's own
 * instructions, and whether the services object they load is set). This header ships with the sco
 * SDK; the Star Citizen game pack publishes the service under the owner "game"
 * (sco::host::ProvideGameService, when the product sets Platform::gameServices;
 * docs/game-services.md). To use it:
 *
 *   const sc_reputation_v1* rp = NULL;
 *   if (api->size > offsetof(sco_api, query_service) &&
 *       api->query_service(SC_REPUTATION_SERVICE_NAME, SC_REPUTATION_SERVICE_VERSION,
 *                          (const void**)&rp) == SCO_OK && api->has("game.reputation.checks")) { ... }
 *
 * Capability:
 *   game.reputation.checks  checks
 * It is OFF by default: the game pack holds it off until an in-game run has confirmed it
 * (docs/game/status.md). While it is off, checks answers SCO_UNAVAILABLE without touching the game.
 *
 * Read-only: nothing here reads or changes a player's standing, and nothing here writes game
 * memory. The repository documents no standing value and no standing change (docs/game/reputation.md).
 *
 * Every function except last_error: game thread only (a command, a tick or a run_on_game_thread
 * task); from another thread it answers SCO_WRONG_THREAD without touching the game. Every failure
 * leaves a reason for last_error. Every function takes your plugin's handle (self, as
 * sco_plugin_load received it) first. Nothing here is owned. */
#ifndef SC_REPUTATION_SERVICE_H
#define SC_REPUTATION_SERVICE_H

#include "sco_api.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SC_REPUTATION_SERVICE_NAME    "game.reputation"
#define SC_REPUTATION_SERVICE_VERSION 0x00010000u /* 1.0 */

/* checks' answer. Set size to sizeof(sc_reputation_checks) before the call. */
typedef struct sc_reputation_checks {
    uint32_t size;     /* in: sizeof(sc_reputation_checks) as you built it */
    uint32_t sites;    /* the reputation-service check sites the game pack located on this build */
    uint32_t intact;   /* of those, the sites whose check still holds the game's own instructions;
                        * a site that isn't intact has been rewritten since the game loaded (the
                        * offline contracts feature of a product writes its patch there) */
    uint32_t services; /* 1: the services object the checks load is set; 0: it is null */
} sc_reputation_checks;

typedef struct sc_reputation_v1 {
    uint32_t size; /* sizeof(sc_reputation_v1) as the game pack built it */
    uint32_t _pad;
    /* Fills *out (sc_reputation_checks) from the game's reputation-service check sites and the
     * services object they load. Reads only; nothing is called or written in the game.
     * SCO_BAD_ARG: out NULL or out->size smaller than sizeof(sc_reputation_checks).
     * SCO_FAILED: the game faulted. game.reputation.checks. */
    sco_result (*checks)(sco_plugin* self, sc_reputation_checks* out);
    /* The reason for the last failed call ("" if none), NUL-terminated, with sco.storage's size
     * handshake: *inout_size is out's capacity on entry and the bytes written (NUL included) on
     * SCO_OK, or the bytes needed on SCO_TOO_MANY. out may be NULL when *inout_size is 0.
     * On the game thread: your own last failed call; on another thread: the last refusal on that
     * thread. Any thread. */
    sco_result (*last_error)(sco_plugin* self, char* out, uint32_t* inout_size);
} sc_reputation_v1;

#ifdef __cplusplus
}
#endif

#endif
