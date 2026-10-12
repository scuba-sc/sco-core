/* GPL-3.0-only. Optional game.world.control 1.0: bounded filtered rays and a
 * reversible free-camera lease. Independent of game.world 1.0 and frozen sco_api.
 * All methods are game-thread-only and require a live plugin handle. Tables are
 * borrowed for the host session: check complete member extent and non-nullness.
 * No engine pointers, absolute camera teleport, FOV, arbitrary input or hooks.
 * Production capabilities remain OFF pending a verified offline engine adapter.
 */
#ifndef SC_WORLD_CONTROL_H
#define SC_WORLD_CONTROL_H
#include "sc_world.h"
#ifdef __cplusplus
extern "C" {
#endif
#define SC_WORLD_CONTROL_NAME "game.world.control"
#define SC_WORLD_CONTROL_VERSION_1_0 0x00010000u
#define SC_WORLD_CONTROL_MAX_SKIPS 32u
#define SC_WORLD_CONTROL_STEP_METRES 10.0
#define SC_WORLD_CONTROL_STEP_RADIANS 0.25

/* Translation delta in world metres, rotation delta unit quaternion (xyzw).
 * Translation norm <=10m, shortest rotation angle <=0.25 rad per call; finite.
 * No absolute pose/zone setter. Provider composes rotation in world frame.
 * The caller sets size; extra tail is ignored, _pad must be zero. */
typedef struct sc_world_camera_step {
    uint32_t size;
    uint32_t _pad;
    double translation[3];
    double rotation_xyzw[4];
} sc_world_camera_step;

typedef struct sc_world_control_v1 {
    uint32_t size;
    uint32_t _pad;
    /* Same ray frames, distance clamp and output semantics as game.world.
     * skip_ids: at most MAX_SKIPS unique nonzero IDs currently owned by self
     * through game.entities/spawn.entities; foreign/relinquished IDs: FAILED.
     * Count 0 permits NULL. Array borrowed only for call and copied/validated
     * before provider access. No persisted filter; unload cannot leak skips.
     * Optional hit entity/normal remain zero and flags=0 until implemented.
     * capability game.world.raycast_filtered; absent => UNAVAILABLE.
     * BAD_ARG invalid input/handle; WRONG_THREAD; NOT_FOUND no hit; FAILED fault.
     * Adequately-sized hit zeroed on all failures, caller tail untouched. */
    sco_result (*raycast_filtered)(sco_plugin* self, uint64_t zone_id,
        const double from[3], const double dir[3], double max_dist,
        const uint64_t* skip_ids, uint32_t skip_count, sc_world_hit* out_hit);
    /* Exclusive shared-state lease. *out_lease=0 on failure. Provider captures
     * ALL overridden state before mutation, atomically; failed acquire changes
     * nothing. Foreign concurrent owner: FAILED; same owner reacquire returns
     * same token. No steal. UNAVAILABLE without verified offline adapter and
     * guaranteed unload restore hook. Token is nonzero, session-volatile.
     * capability game.world.free_camera; output NULL/invalid self: BAD_ARG. */
    sco_result (*camera_acquire)(sco_plugin* self, uint64_t* out_lease);
    /* Only the lease owner may submit bounded deltas. BAD_ARG malformed input;
     * NOT_FOUND stale/zero lease; FAILED foreign owner or provider failure.
     * Provider failure attempts restore, then latches restart-required; future
     * acquire/step is UNAVAILABLE even if restoration succeeded. */
    sco_result (*camera_step)(sco_plugin* self, uint64_t lease,
        const sc_world_camera_step* step);
    /* Restore exact pre-acquire camera/input/mode state before dropping lease.
     * Stale lease: NOT_FOUND; foreign owner: FAILED. No arbitrary reset/off.
     * Ordinary unload and service stop perform the same restoration. Restore
     * failure retains backing state, refuses new mutations and requires restart.
     * Fault-retained native providers are NOT safely unloadable (host contract).
     * No async work/cancellation, no plugin callbacks; recursive calls FAILED. */
    sco_result (*camera_release)(sco_plugin* self, uint64_t lease);
} sc_world_control_v1;
#ifdef __cplusplus
}
#endif
#endif
