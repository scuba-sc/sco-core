/* GPL-3.0-only. game.prefabs 1.0: owned object-container spawning.
 * Game-thread only. Host-owned session table; test complete pointer extent and
 * nullness. No game pointers, filesystem access, arbitrary class or attributes.
 * Owner-approved R1 default is enabled; Windows/SEH/gameplay NOT VERIFIED. */
#ifndef SC_PREFABS_H
#define SC_PREFABS_H
#include "sco_api.h"
#ifdef __cplusplus
extern "C" {
#endif
#define SC_PREFABS_NAME "game.prefabs"
#define SC_PREFABS_VERSION_1_0 0x00010000u
#define SC_PREFABS_MAX_PATH 511u
#define SC_PREFABS_MAX_PER_OWNER 128u
/* NUL-terminated canonical content path: ObjectContainers/.../name.socpak,
 * at most MAX_PATH bytes excluding NUL; ASCII alphanumeric, _ - . { } / only.
 * No empty/dot/dot-dot component, backslash, absolute path, colon, percent
 * encoding, query or fragment. Exact spelling preserved, never normalized.
 * pos in streamed zone's local metres; rot finite nonzero quaternion xyzw,
 * normalized by host. Path/pose borrowed for this synchronous call only.
 * self must be live. out_id zero on failure, final entity ID on success, even
 * before streaming. Object container is owned through game.entities: use its
 * set_transform/despawn and game.owned retained-build/remove contracts.
 * Pending spawns count against shared ENTITIES_MAX_OWNED and owner quota;
 * never recycle ownership merely because alive=0. Ordinary unload removes
 * pending/streamed owned containers through existing delayed despawn primitive.
 * Use retain_build for undo while loaded, NOT keep (irreversibly gives up rights).
 * SCO_OK is spawn initiation, NOT materialization or collision confirmation.
 * BAD_ARG invalid input/handle; WRONG_THREAD; TOO_MANY quota; NOT_FOUND missing
 * zone/content/class; UNAVAILABLE absent capability or unload cleanup; FAILED
 * provider fault/refusal. No async callback. Native retained-provider fault
 * follows the host restart-required contract, not safe unloading. */
typedef struct sc_prefabs_v1 {
    uint32_t size;
    uint32_t _pad;
    sco_result (*spawn)(sco_plugin* self, const char* content_path, uint64_t zone_id,
        const double pos[3], const double rot_xyzw[4], uint64_t* out_id);
} sc_prefabs_v1;
#ifdef __cplusplus
}
#endif
#endif
