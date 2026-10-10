/* sc_creative.h: the game.creative service: god mode, fly (noclip) and its speed, and infinite
 * ammo for your own weapons and the ship you're aboard, for the local player only. This header
 * ships with the sco SDK; the Star Citizen game pack publishes the service under the owner "game"
 * (sco::host::ProvideGameService, when the product sets Platform::gameServices;
 * docs/game-services.md). To use it:
 *
 *   const sc_creative_v1* cr = NULL;
 *   if (api->size > offsetof(sco_api, query_service) &&
 *       api->query_service(SC_CREATIVE_SERVICE_NAME, SC_CREATIVE_SERVICE_VERSION,
 *                          (const void**)&cr) == SCO_OK && api->has("game.creative.god_mode")) { ... }
 *
 * Capabilities, one per feature, so a game patch that breaks one leaves the others working:
 *   game.creative.god_mode  set_god_mode
 *   game.creative.fly       set_fly_mode (and set_fly_speed, which also needs the game's fly
 *                           speed field; without it set_fly_speed alone answers SCO_UNAVAILABLE)
 *   game.creative.ammo      set_infinite_ammo
 *   game.creative.ship_ammo set_infinite_ship_ammo
 * A function whose capability isn't ready answers SCO_UNAVAILABLE.
 *
 * Every function: game thread only (a command, a tick or a run_on_game_thread task); from another
 * thread it answers SCO_WRONG_THREAD without touching the game. Every failure leaves a reason for
 * last_error. Every function takes your plugin's handle (self, as sco_plugin_load received it)
 * first; the game pack logs each change with your plugin id in mod.log.
 *
 * Whose toggle: a toggle belongs to the plugin that last switched it on. When that plugin
 * unloads or crashes, the host switches the toggle off (god mode off, fly off, the fly speed
 * back to the game's, ammo as the game left it). Another plugin may switch a toggle off or on
 * again; it then owns it. Nothing here touches anyone but the local player. */
#ifndef SC_CREATIVE_SERVICE_H
#define SC_CREATIVE_SERVICE_H

#include "sco_api.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SC_CREATIVE_SERVICE_NAME    "game.creative"
#define SC_CREATIVE_SERVICE_VERSION 0x00010000u /* 1.0 */
#define SC_CREATIVE_FLY_SPEED_MAX   10000       /* set_fly_speed's limit, metres per second */

/* state()'s flags, or-ed: the toggles that are on, whoever switched them on. */
typedef enum sc_creative_flag {
    SC_CREATIVE_GOD_MODE = 0x1,          /* wanted on: held on every half second, so it also
                                          * applies once you spawn and again after a respawn */
    SC_CREATIVE_FLY = 0x2,               /* fly mode was requested on and not off since */
    SC_CREATIVE_INFINITE_AMMO = 0x4,     /* your hand-held weapons' magazines don't drain */
    SC_CREATIVE_INFINITE_SHIP_AMMO = 0x8, /* the same for the ship you're aboard */
    SC_CREATIVE_FLAG_FORCE32 = 0x7fffffff
} sc_creative_flag;

typedef struct sc_creative_v1 {
    uint32_t size; /* sizeof(sc_creative_v1) as the game pack built it */
    uint32_t _pad;
    /* God mode on (non-zero) or off (0). SCO_OK also when you haven't spawned yet: it is applied
     * once you have. SCO_FAILED: the game faulted, or the host can't turn the toggle off when
     * your plugin unloads (no release hook left). game.creative.god_mode. */
    sco_result (*set_god_mode)(sco_plugin* self, int on);
    /* Fly mode (noclip) on or off, for your player. SCO_FAILED: you're not spawned yet, your actor
     * has no fly handler, the game faulted, or no release hook is left. Switching it off when it
     * isn't on is SCO_OK and does nothing. game.creative.fly. */
    sco_result (*set_fly_mode)(sco_plugin* self, int on);
    /* The fly speed in metres per second: finite, above 0 and at most SC_CREATIVE_FLY_SPEED_MAX
     * (SCO_BAD_ARG otherwise). The game's own speed comes back when your plugin unloads.
     * SCO_UNAVAILABLE: no fly capability, or the game's fly speed field wasn't found on this
     * build. SCO_FAILED: the game's settings aren't up yet. game.creative.fly. */
    sco_result (*set_fly_speed)(sco_plugin* self, float mps);
    /* Infinite ammo for the weapons you carry: a magazine you fire from is topped up to its
     * maximum. SCO_FAILED when the magazine setter can't be hooked or no release hook is left.
     * game.creative.ammo. */
    sco_result (*set_infinite_ammo)(sco_plugin* self, int on);
    /* The same for every weapon on the ship you're aboard (including ones that drain their
     * magazine without the setter: topped up twice a second). game.creative.ship_ammo. */
    sco_result (*set_infinite_ship_ammo)(sco_plugin* self, int on);
    /* *out_flags = the sc_creative_flag bits that are on. SCO_UNAVAILABLE only when none of the
     * four capabilities is ready. */
    sco_result (*state)(sco_plugin* self, uint32_t* out_flags);
    /* The reason for the last failed call ("" if none), NUL-terminated, with sco.storage's size
     * handshake: *inout_size is out's capacity on entry and the bytes written (NUL included) on
     * SCO_OK, or the bytes needed on SCO_TOO_MANY. out may be NULL when *inout_size is 0.
     * On the game thread: your own last failed call; on another thread: the last refusal on that
     * thread. Any thread. */
    sco_result (*last_error)(sco_plugin* self, char* out, uint32_t* inout_size);
} sc_creative_v1;

#ifdef __cplusplus
}
#endif

#endif
