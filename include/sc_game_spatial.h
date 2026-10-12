/* GPL-3.0-only. game.spatial 1.0: copied spatial observations and full poses.
 * Game pack only; teleport.spatial 1.0 and the frozen sco_api ABI are unchanged.
 * All calls are game-thread-only, synchronous, non-reentrant. Return 1 on success,
 * 0 on invalid input, absent readiness, wrong thread, unreadable/unstreamed frames,
 * expired epoch, ambiguous name or quota refusal. ALL outputs unchanged on 0.
 * Tables live until plugins unload; do not call after game shutdown admission.
 * Native C and raw C++ supported; C# and Lua unsupported (no FFI expansion).
 * Check size >= offsetof(table, method)+sizeof(table->method), then non-NULL.
 *
 * Observation scope is at most 256 explicitly observed zones (player ancestry or
 * an ID queried through zone_record). It is NOT global discovery, memory scanning
 * or an exhaustive list of streamed zones. Only complete, readable rigid ancestry
 * is admitted. root_id is the observed outermost zone, NOT a certified star system;
 * system_id stays 0 with SYSTEM_UNKNOWN until a grounded classifier exists.
 * Names are copied into 96 bytes; a full 95-byte slot conservatively sets
 * NAME_TRUNCATED and disables exact name lookup (original length is unknown).
 *
 * Epochs are opaque nonzero tokens: tick refresh, additions/changes, invalidation
 * and stop expire them. Records remain caller-owned historical copies; never
 * persist epoch validity across ticks/sessions. Re-query before using an ID.
 * list_zones: epoch=0 starts from current observations; a nonzero epoch requires
 * an exact match. Sorted ascending by ID, offset is an element index. cap <= 64,
 * stride in [sizeof(sc_game_zone),4096]; each output slot must advertise at least
 * sizeof(sc_game_zone). cap=0/out=NULL counts only. out_count is copied elements,
 * out_more is remaining elements AFTER this page; out_epoch pins later pages.
 * Invalid offset (> total), expired epoch or any invalid slot writes nothing.
 * Known fields only are written, preserving caller size and unknown tail bytes.
 * No pointers, assets, coordinates or player identifiers in metadata records. */
#ifndef SC_GAME_SPATIAL_H
#define SC_GAME_SPATIAL_H
#include <stdint.h>

#define SC_GAME_SPATIAL_NAME "game.spatial"
#define SC_GAME_SPATIAL_VERSION 0x00010000u
#define SC_GAME_SPATIAL_MAX_ZONES 256u
#define SC_GAME_SPATIAL_MAX_PAGE 64u
#define SC_GAME_ZONE_SYSTEM_UNKNOWN 1u
#define SC_GAME_ZONE_NAME_TRUNCATED 2u

typedef struct sc_game_zone {
    uint32_t size;
    uint32_t flags;
    uint64_t epoch;
    uint64_t zone_id;
    uint64_t parent_id; /* direct parent; 0 is world */
    uint64_t root_id;   /* confirmed complete observed ancestry */
    uint64_t system_id; /* unknown = 0, never guessed from a name */
    char name[96];
} sc_game_zone;

typedef struct sc_game_spatial_v1 {
    uint32_t size;
    /* Metres and quaternion x/y/z/w; either frame may be 0 (world).
     * Nonfinite position/rotation or rotation norm < 1e-12 refuses. Normalize
     * accepted rotations. Conversion stops positions at the common ancestor.
     * Outputs may alias corresponding input arrays; output arrays must not overlap. */
    int (*pose_to_zone)(uint64_t from, uint64_t to, const double pos[3],
                        const double rot_xyzw[4], double out_pos[3], double out_rot_xyzw[4]);
    /* Observe a streamed nonzero ID and its complete ancestors, then copy it.
     * Caller out->size >= sizeof(sc_game_zone); unknown tail is untouched. */
    int (*zone_record)(uint64_t zone_id, sc_game_zone* out);
    /* Exact case-sensitive nonempty name, <=95 bytes; unique observed match only.
     * No speculative/unloaded-name lookup and no observation of unseen IDs. */
    int (*find_zone)(const char* name, sc_game_zone* out);
    /* Bounded copied page of current observations; see the epoch/stride rules above. */
    int (*list_zones)(uint64_t epoch, uint32_t offset, sc_game_zone* out,
                      uint32_t stride, uint32_t cap, uint32_t* out_count,
                      uint32_t* out_more, uint64_t* out_epoch);
} sc_game_spatial_v1;
#endif
