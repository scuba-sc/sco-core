/* sc_contracts.h: the game.contracts service: a read-only value behind the game's contracts and
 * missions that the repository documents, the mission stream radius. This header ships with the
 * sco SDK; the Star Citizen game pack publishes the service under the owner "game"
 * (sco::host::ProvideGameService, when the product sets Platform::gameServices;
 * docs/game-services.md). To use it:
 *
 *   const sc_contracts_v1* ct = NULL;
 *   if (api->size > offsetof(sco_api, query_service) &&
 *       api->query_service(SC_CONTRACTS_SERVICE_NAME, SC_CONTRACTS_SERVICE_VERSION,
 *                          (const void**)&ct) == SCO_OK && api->has("game.contracts.stream_radius")) { ... }
 *
 * Capabilities:
 *   game.contracts.stream_radius  stream_radius
 * It is OFF by default: the game pack holds it off until an in-game run has confirmed it
 * (docs/game/status.md). A function whose capability isn't ready answers SCO_UNAVAILABLE without
 * touching the game.
 *
 * Every function except last_error: game thread only (a command, a tick or a run_on_game_thread
 * task); from another thread it answers SCO_WRONG_THREAD without touching the game. Every failure
 * leaves a reason for last_error. Every function takes your plugin's handle (self, as
 * sco_plugin_load received it) first. Nothing here writes to the game and nothing is owned.
 *
 * The game.missions service (sc_missions.h) is the offline mission backend; this service only
 * reads a value the game holds. A product that runs offline contracts may write the stream radius;
 * stream_radius reports the value the game holds at the time of the call. */
#ifndef SC_CONTRACTS_SERVICE_H
#define SC_CONTRACTS_SERVICE_H

#include "sco_api.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SC_CONTRACTS_SERVICE_NAME    "game.contracts"
#define SC_CONTRACTS_SERVICE_VERSION 0x00010000u /* 1.0 */

typedef struct sc_contracts_v1 {
    uint32_t size; /* sizeof(sc_contracts_v1) as the game pack built it */
    uint32_t _pad;
    /* *out_radius = the mission stream radius the game's mission settings hold (a 32-bit float,
     * in the game's own units). SCO_BAD_ARG: out_radius NULL. SCO_NOT_FOUND: the mission settings
     * aren't loaded yet. SCO_FAILED: the game faulted. game.contracts.stream_radius. */
    sco_result (*stream_radius)(sco_plugin* self, float* out_radius);
    /* The reason for the last failed call ("" if none), NUL-terminated, with sco.storage's size
     * handshake: *inout_size is out's capacity on entry and the bytes written (NUL included) on
     * SCO_OK, or the bytes needed on SCO_TOO_MANY. out may be NULL when *inout_size is 0.
     * On the game thread: your own last failed call; on another thread: the last refusal on that
     * thread. Any thread. */
    sco_result (*last_error)(sco_plugin* self, char* out, uint32_t* inout_size);
} sc_contracts_v1;

#ifdef __cplusplus
}
#endif

#endif
