// SPDX-License-Identifier: GPL-3.0-only
#include "world_ray_engine.h"
#include <cstring>
namespace sco::game::services {
namespace {
WorldRayNativeRoute g_route;
const char* g_refusal = "";
bool g_faulted = false;
bool g_installed = false;

// Zero every field the caller's struct has room for, apart from its own size: a failed or
// invalidated call publishes no hit, and the caller's tail stays untouched.
void NoHit(sc_world_hit* out) {
    if (!out || out->size < sizeof(sc_world_hit)) return;
    const uint32_t size = out->size;
    std::memset(out, 0, sizeof(sc_world_hit));
    out->size = size;
}

sco_result Filtered(sco_plugin*, uint64_t zone_id, const double from[3], const double dir[3],
                    double max_dist, const uint64_t* skip_ids, uint32_t skip_count,
                    sc_world_hit* out_hit) {
    if (!g_installed || g_faulted) return SCO_UNAVAILABLE;
    if (!out_hit || out_hit->size < sizeof(sc_world_hit)) return SCO_BAD_ARG;
    if (skip_count && !skip_ids) return SCO_BAD_ARG;
    const uint64_t generation = g_route.skip_generation(g_route.ctx);
    if (!generation) return SCO_UNAVAILABLE;
    const sco_result r = g_route.single_hit(g_route.ctx, zone_id, from, dir, max_dist, skip_ids,
                                            skip_count, out_hit);
    // The skip array was borrowed for the call. If its generation moved, the provider may have read
    // it after it was invalidated, so nothing from this call is published and the seam latches.
    if (g_route.skip_generation(g_route.ctx) != generation) {
        NoHit(out_hit);
        g_faulted = true;
        return SCO_UNAVAILABLE;
    }
    if (r == SCO_FAILED) {
        NoHit(out_hit);
        g_faulted = true;
        return SCO_FAILED;
    }
    if (r != SCO_OK && r != SCO_NOT_FOUND) NoHit(out_hit);
    return r;
}
}  // namespace

const char* NativeWorldRayRefusal() { return g_refusal; }

Result InstallNativeWorldRay(const WorldRayNativeRoute& route) {
    if (g_installed) {
        g_refusal = "a native filtered ray backend is already installed";
        return Result::Failed;
    }
    if (!route.zone || !route.world || !route.grid) {
        g_refusal = "the zone, world or grid reference was not resolved";
        return Result::Unavailable;
    }
    if (!route.single_hit) {
        g_refusal = "no single-hit query: the successful-call contract is not grounded";
        return Result::Unavailable;
    }
    if (!route.skip_generation) {
        g_refusal = "skip-request lifetime is not grounded";
        return Result::Unavailable;
    }
    g_route = route;
    g_faulted = false;
    g_installed = true;
    g_refusal = "";
    return Result::Ok;
}

sco_result (*NativeWorldRayFiltered())(sco_plugin*, uint64_t, const double[3], const double[3],
                                       double, const uint64_t*, uint32_t, sc_world_hit*) {
    return g_installed ? &Filtered : nullptr;
}

}  // namespace sco::game::services
