/* sc_hangar.h: the game.hangar service: a hangar's ship lift (find it from its landing area,
 * ask it to open or close) and withdrawing the local player's pending ATC request. This header
 * ships with the sco SDK; the Star Citizen game pack publishes the service under the owner "game"
 * (sco::host::ProvideGameService, when the product sets Platform::gameServices;
 * docs/game-services.md). To use it:
 *
 *   const sc_hangar_v1* hg = NULL;
 *   if (api->size > offsetof(sco_api, query_service) &&
 *       api->query_service(SC_HANGAR_SERVICE_NAME, SC_HANGAR_SERVICE_VERSION,
 *                          (const void**)&hg) == SCO_OK && api->has("game.hangar.lift")) { ... }
 *
 * Capabilities, one per feature, so a game patch that breaks one leaves the other working:
 *   game.hangar.lift        lift_of_landing_area, lift_request
 *   game.hangar.atc_cancel  atc_cancel_request
 * Both are OFF by default: the game pack holds them off until an in-game run has confirmed
 * them (docs/game/status.md). A function whose capability isn't ready answers SCO_UNAVAILABLE
 * without touching the game.
 *
 * Every function except last_error: game thread only (a command, a tick or a run_on_game_thread
 * task); from another thread it answers SCO_WRONG_THREAD without touching the game. Every failure
 * leaves a reason for last_error. Every function takes your plugin's handle (self, as
 * sco_plugin_load received it) first. Nothing here is owned: a lift request or an ATC cancel is
 * one call into the game, and nothing is undone when your plugin unloads.
 *
 * The game pack's own ship terminal, hangar and ATC handling (when the product runs it) drives
 * the same lifts for Store and Retrieve; a lift request from a plugin is not coordinated with it. */
#ifndef SC_HANGAR_SERVICE_H
#define SC_HANGAR_SERVICE_H

#include "sco_api.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SC_HANGAR_SERVICE_NAME    "game.hangar"
#define SC_HANGAR_SERVICE_VERSION 0x00010000u /* 1.0 */

/* lift_request's op. */
typedef enum sc_hangar_lift_op {
    SC_HANGAR_LIFT_OPEN = 1,  /* the game's lift open request: the platform comes up */
    SC_HANGAR_LIFT_CLOSE = 2, /* the game's lift close request: the platform goes down */
    SC_HANGAR_LIFT_OP_FORCE32 = 0x7fffffff
} sc_hangar_lift_op;

typedef struct sc_hangar_v1 {
    uint32_t size; /* sizeof(sc_hangar_v1) as the game pack built it */
    uint32_t _pad;
    /* *out_lift_id = the entity id of the ship lift that serves the landing area
     * landing_area_id: the lift whose name matches the landing area's and that is in the
     * landing area's zone. SCO_BAD_ARG: an id of 0, out_lift_id NULL, or the entity isn't a
     * landing area. SCO_NOT_FOUND: the landing area isn't streamed in, or no lift goes with it.
     * SCO_FAILED: the game faulted. game.hangar.lift. */
    sco_result (*lift_of_landing_area)(sco_plugin* self, uint64_t landing_area_id, uint64_t* out_lift_id);
    /* Asks the ship lift lift_id (an id from lift_of_landing_area) to open or close
     * (sc_hangar_lift_op). SCO_OK means the request was handed to the game, not that the lift has
     * moved. SCO_BAD_ARG: an id of 0, an unknown op, or the entity isn't a ship lift.
     * SCO_NOT_FOUND: the lift isn't streamed in. SCO_FAILED: the game faulted.
     * game.hangar.lift. */
    sco_result (*lift_request)(sco_plugin* self, uint64_t lift_id, uint32_t op);
    /* Withdraws the local player's pending request at the air traffic controller atc_id (the
     * game's own request cancel). SCO_BAD_ARG: an id of 0. SCO_NOT_FOUND: that entity has no
     * ATC data. SCO_FAILED: you're not spawned, or the game faulted. game.hangar.atc_cancel. */
    sco_result (*atc_cancel_request)(sco_plugin* self, uint64_t atc_id);
    /* The reason for the last failed call ("" if none), NUL-terminated, with sco.storage's size
     * handshake: *inout_size is out's capacity on entry and the bytes written (NUL included) on
     * SCO_OK, or the bytes needed on SCO_TOO_MANY. out may be NULL when *inout_size is 0.
     * On the game thread: your own last failed call; on another thread: the last refusal on that
     * thread. Any thread. */
    sco_result (*last_error)(sco_plugin* self, char* out, uint32_t* inout_size);
} sc_hangar_v1;

#ifdef __cplusplus
}
#endif

#endif
