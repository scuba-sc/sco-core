/* Game build identity: what exe is running, and whether the game pack verified it.
 * Pure metadata: no engine pointers, no actions. See docs/game/build.md. */
#ifndef SC_GAME_BUILD_H
#define SC_GAME_BUILD_H
#include "sco_api.h"
#include <stddef.h>
#include <stdint.h>
#define SC_GAME_BUILD_SERVICE_NAME "game.build"
#define SC_GAME_BUILD_SERVICE_VERSION 0x00010000u
typedef struct sc_game_build_info {
    uint32_t size;
    uint32_t pe_timestamp;         /* StarCitizen.exe TimeDateStamp (the file identity) */
    uint32_t size_of_image;        /* PE SizeOfImage, bytes */
    uint32_t pack_major;           /* game pack version (SCO_GAME_PACK_*), dotted triple */
    uint32_t pack_minor;
    uint32_t pack_patch;
    uint32_t verified_build[4];    /* the build this game pack verified, four-part, 0 first */
    uint32_t verified_pe_timestamp;/* that build's PE timestamp */
    uint32_t pin_matches;          /* 1 = pe_timestamp == verified_pe_timestamp */
} sc_game_build_info;
typedef struct sc_game_build_v1 {
    uint32_t size;
    uint32_t _pad;
    sco_result (*read)(sco_plugin* self, sc_game_build_info* out);
} sc_game_build_v1;
static inline sco_result sc_game_build_read(const sc_game_build_v1* t, sco_plugin* self,
                                            sc_game_build_info* out) {
    return (t && t->size >= offsetof(sc_game_build_v1, read) + sizeof(t->read) && t->read && out)
               ? t->read(self, out) : SCO_UNAVAILABLE;
}
#endif
