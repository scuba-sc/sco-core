// SPDX-License-Identifier: GPL-3.0-only
#pragma once
// Native filtered single-hit ray seam (K-42). The successful-call contract needs the zone, world
// and grid references the host resolved plus a skip-request lifetime: the skip array is borrowed
// only for the call and must be copied before any provider access. None of that is grounded, so
// production installs nothing and refuses; the synthetic provider keeps answering
// game.world.raycast_filtered and no native call is made.
//
// One synchronous scoped query, never a repeated-single-ray approximation of a multi-hit result.
// Not run in game.
#include "sco/runtime.h"
#include <sc_world_control.h>
#include <cstdint>

namespace sco::game::services {

struct WorldRayNativeRoute {
    // The zone, world and grid references the host resolved for this build.
    void* zone = nullptr;
    void* world = nullptr;
    void* grid = nullptr;
    // Fills exactly one hit: SCO_OK on a hit, SCO_NOT_FOUND when nothing was within max_dist,
    // SCO_FAILED on a fault. The provider acquires and releases the grid itself.
    sco_result (*single_hit)(void* ctx, uint64_t zone_id, const double from[3], const double dir[3],
                             double max_dist, const uint64_t* skip_ids, uint32_t skip_count,
                             sc_world_hit* out_hit) = nullptr;
    // Skip-request lifetime: nonzero while a skip array handed to the provider is still valid. The
    // seam reads it before and after the call; a change means the array was invalidated mid-call.
    uint64_t (*skip_generation)(void* ctx) = nullptr;
    void* ctx = nullptr;
    bool Complete() const { return zone && world && grid && single_hit && skip_generation; }
};

// Why the last install refused; empty after a successful one. Static storage, never caller-owned.
const char* NativeWorldRayRefusal();

// Installs the single-hit backend. Refuses, naming the first missing piece, unless the route is
// complete; a refusal leaves no backend and every capability exactly as it was.
Result InstallNativeWorldRay(const WorldRayNativeRoute& route = {});

// The installed query, or null. A fault or a mid-call skip invalidation latches the seam
// unavailable for the rest of the process, as the multi-hit contract requires.
sco_result (*NativeWorldRayFiltered())(sco_plugin*, uint64_t, const double[3], const double[3],
                                       double, const uint64_t*, uint32_t, sc_world_hit*);

}  // namespace sco::game::services
