// SPDX-License-Identifier: GPL-3.0-only
// game.world 1.1 free_camera_flag: the portable policy (world_free_cam.h). No game memory here;
// the read is the provider's.
#include "world_free_cam.h"
#include "sco/caps.h"
#include <cstdio>

namespace sco::game::services {

namespace {

constexpr char kOffPending[] = "OFF pending in-game confirmation (docs/game/world.md)";
constexpr char kRowsNotOk[]  = "build.free_cam rows not OK on this build (docs/game/world.md)";
constexpr char kStopped[]    = "game.world is stopped";

}  // namespace

void FreeCamFlag::Start(const FreeCamFlagProvider& provider) {
    p_ = provider;
    on_ = p_.on_thread && p_.rows && p_.probed && p_.read_flag;
    std::snprintf(why_, sizeof(why_), "%s", on_ ? "" : p_.rows ? kOffPending : kRowsNotOk);
    caps::Set(kWorldCapFreeCamFlag, on_, why_);
    started_ = true;
}

void FreeCamFlag::Stop() {
    p_ = { p_.on_thread, false, false, nullptr };
    started_ = on_ = false;
    std::snprintf(why_, sizeof(why_), "%s", kStopped);
    caps::Set(kWorldCapFreeCamFlag, false, kStopped);
}

sco_result FreeCamFlag::Query(uint32_t* outSet, const char** why) const {
    if (outSet) *outSet = 0;
    if (!p_.on_thread || !p_.on_thread()) {
        *why = "game thread only";
        return SCO_WRONG_THREAD;
    }
    if (!outSet) {
        *why = "out_set is NULL";
        return SCO_BAD_ARG;
    }
    if (!started_) {
        *why = kStopped;
        return SCO_UNAVAILABLE;
    }
    if (!on_) {
        *why = why_;
        return SCO_UNAVAILABLE;
    }
    uint32_t set = 0;
    const sco_result r = p_.read_flag(&set, why);
    if (r != SCO_OK) return r;
    *outSet = set ? 1u : 0u;
    return SCO_OK;
}

}  // namespace sco::game::services
