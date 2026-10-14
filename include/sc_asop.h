/* sc_asop.h: the game.asop service: two read-only values behind the ship terminal (ASOP) that the
 * repository documents, the insurance claim request timeout and the IsShardPersisted flag. This
 * header ships with the sco SDK; the Star Citizen game pack publishes the service under the owner
 * "game" (sco::host::ProvideGameService, when the product sets Platform::gameServices;
 * docs/game-services.md). To use it:
 *
 *   const sc_asop_v1* as = NULL;
 *   if (api->size > offsetof(sco_api, query_service) &&
 *       api->query_service(SC_ASOP_SERVICE_NAME, SC_ASOP_SERVICE_VERSION,
 *                          (const void**)&as) == SCO_OK && api->has("game.asop.claim_timeout")) { ... }
 *
 * Capabilities, one per feature, so a game patch that breaks one leaves the other working:
 *   game.asop.claim_timeout    claim_timeout
 *   game.asop.shard_persisted  shard_persisted
 * Both are OFF by default: the game pack holds them off until an in-game run has confirmed
 * them (docs/game/status.md). A function whose capability isn't ready answers SCO_UNAVAILABLE
 * without touching the game.
 *
 * Every function except last_error: game thread only (a command, a tick or a run_on_game_thread
 * task); from another thread it answers SCO_WRONG_THREAD without touching the game. Every failure
 * leaves a reason for last_error. Every function takes your plugin's handle (self, as
 * sco_plugin_load received it) first. Nothing here writes to the game and nothing is owned.
 *
 * The game pack's own ship terminal handling (when the product runs it) shortens the claim
 * timeout; claim_timeout reports the value the game holds at the time of the call. */
#ifndef SC_ASOP_SERVICE_H
#define SC_ASOP_SERVICE_H

#include "sco_api.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SC_ASOP_SERVICE_NAME    "game.asop"
#define SC_ASOP_SERVICE_VERSION 0x00010000u /* 1.0 */

typedef struct sc_asop_v1 {
    uint32_t size; /* sizeof(sc_asop_v1) as the game pack built it */
    uint32_t _pad;
    /* *out_seconds = the request timeout, in seconds, of the game's insurance component
     * configuration: how long a ship terminal claim request waits for an answer. SCO_BAD_ARG:
     * out_seconds NULL. SCO_NOT_FOUND: the insurance configuration isn't loaded yet. SCO_FAILED:
     * the game faulted. game.asop.claim_timeout. */
    sco_result (*claim_timeout)(sco_plugin* self, int32_t* out_seconds);
    /* *out_persisted = 1 when the game's IsShardPersisted flag is set, 0 when it is clear: the
     * flag the ship terminal's open checks before it opens. SCO_BAD_ARG: out_persisted NULL.
     * SCO_FAILED: the game faulted. game.asop.shard_persisted. */
    sco_result (*shard_persisted)(sco_plugin* self, uint32_t* out_persisted);
    /* The reason for the last failed call ("" if none), NUL-terminated, with sco.storage's size
     * handshake: *inout_size is out's capacity on entry and the bytes written (NUL included) on
     * SCO_OK, or the bytes needed on SCO_TOO_MANY. out may be NULL when *inout_size is 0.
     * On the game thread: your own last failed call; on another thread: the last refusal on that
     * thread. Any thread. */
    sco_result (*last_error)(sco_plugin* self, char* out, uint32_t* inout_size);
} sc_asop_v1;

#ifdef __cplusplus
}
#endif

#endif
