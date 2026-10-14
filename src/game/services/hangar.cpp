// SPDX-License-Identifier: GPL-3.0-only
// game.hangar 1.0: the portable policy (hangar.h). No game memory here; the engine calls are the
// provider's.
#include "hangar.h"
#include "sco/caps.h"
#include <cstdio>
#include <cstring>

namespace sco::game::services {

namespace {

constexpr char kOffPending[] = "OFF pending in-game confirmation (docs/game/hangar.md)";
constexpr char kRowsNotOk[]  = "rows not OK on this build (docs/game/hangar.md)";
constexpr char kStopped[]    = "game.hangar stopped";

thread_local char t_threadErr[Hangar::kErrMax];

void Copy(char* dst, size_t n, const char* src) { std::snprintf(dst, n, "%s", src ? src : ""); }

}  // namespace

sco_result Hangar::Start(const HangarProvider& provider) {
    if (!provider.on_thread || !provider.live || !provider.on_thread()) return SCO_WRONG_THREAD;
    p_ = provider;
    lift_ = p_.probed && p_.lift_rows && p_.lift_of_area && p_.lift_request;
    atc_  = p_.probed && p_.atc_rows && p_.atc_cancel;
    Copy(liftWhy_, sizeof(liftWhy_), lift_ ? "" : p_.lift_rows ? kOffPending : kRowsNotOk);
    Copy(atcWhy_, sizeof(atcWhy_), atc_ ? "" : p_.atc_rows ? kOffPending : kRowsNotOk);
    caps::Set(kHangarCapLift, lift_, liftWhy_);
    caps::Set(kHangarCapAtcCancel, atc_, atcWhy_);
    started_ = true;
    return SCO_OK;
}

void Hangar::Stop() {
    if (!started_) return;
    started_ = lift_ = atc_ = false;
    Copy(liftWhy_, sizeof(liftWhy_), kStopped);
    Copy(atcWhy_, sizeof(atcWhy_), kStopped);
    caps::Set(kHangarCapLift, false, kStopped);
    caps::Set(kHangarCapAtcCancel, false, kStopped);
    for (ErrSlot& e : errs_) e = {};
}

const Hangar::ErrSlot* Hangar::FindSlot(const void* owner) const {
    for (const ErrSlot& e : errs_)
        if (e.owner == owner) return &e;
    return nullptr;
}

Hangar::ErrSlot& Hangar::SlotFor(const void* owner) {
    ErrSlot* oldest = &errs_[0];
    for (ErrSlot& e : errs_) {
        if (e.owner == owner) return e;
        if (e.seq < oldest->seq) oldest = &e;
    }
    *oldest = {};
    oldest->owner = owner;
    return *oldest;
}

sco_result Hangar::Fail(sco_plugin* self, const char* fn, sco_result r, const char* why) {
    ErrSlot& e = SlotFor(self);
    Copy(e.msg, sizeof(e.msg), why);
    e.seq = ++seq_;
    if (p_.log) p_.log(self, fn, r, e.msg);
    return r;
}

// Thread and self first: a refusal here never reaches a plugin's slot.
sco_result Hangar::Admit(sco_plugin* self, const char* fn) {
    if (!p_.on_thread || !p_.on_thread()) {
        std::snprintf(t_threadErr, sizeof(t_threadErr), "%s: game thread only", fn);
        return SCO_WRONG_THREAD;
    }
    if (!self || !p_.live || !p_.live(self)) {
        std::snprintf(t_threadErr, sizeof(t_threadErr), "%s: self isn't a loaded plugin", fn);
        return SCO_BAD_ARG;
    }
    return SCO_OK;
}

sco_result Hangar::Gate(sco_plugin* self, const char* fn, bool on, const char* why) {
    if (!started_) return Fail(self, fn, SCO_UNAVAILABLE, kStopped);
    if (!on) return Fail(self, fn, SCO_UNAVAILABLE, why);
    return SCO_OK;
}

sco_result Hangar::LiftOfLandingArea(sco_plugin* self, uint64_t area, uint64_t* outLift) {
    const char* fn = "lift_of_landing_area";
    sco_result r = Admit(self, fn);
    if (r != SCO_OK) return r;
    if (!area || !outLift) return Fail(self, fn, SCO_BAD_ARG, "landing_area_id is 0 or out_lift_id is NULL");
    *outLift = 0;
    r = Gate(self, fn, lift_, liftWhy_);
    if (r != SCO_OK) return r;
    const char* why = "the game didn't say why";
    uint64_t lift = 0;
    r = p_.lift_of_area(area, &lift, &why);
    if (r != SCO_OK) return Fail(self, fn, r, why);
    *outLift = lift;
    return SCO_OK;
}

sco_result Hangar::LiftRequest(sco_plugin* self, uint64_t lift, uint32_t op) {
    const char* fn = "lift_request";
    sco_result r = Admit(self, fn);
    if (r != SCO_OK) return r;
    if (!lift) return Fail(self, fn, SCO_BAD_ARG, "lift_id is 0");
    if (op != SC_HANGAR_LIFT_OPEN && op != SC_HANGAR_LIFT_CLOSE) return Fail(self, fn, SCO_BAD_ARG, "op isn't SC_HANGAR_LIFT_OPEN or _CLOSE");
    r = Gate(self, fn, lift_, liftWhy_);
    if (r != SCO_OK) return r;
    const char* why = "the game didn't say why";
    r = p_.lift_request(lift, op, &why);
    if (r != SCO_OK) return Fail(self, fn, r, why);
    if (p_.log) p_.log(self, fn, SCO_OK, op == SC_HANGAR_LIFT_OPEN ? "open requested" : "close requested");
    return SCO_OK;
}

sco_result Hangar::AtcCancelRequest(sco_plugin* self, uint64_t atc) {
    const char* fn = "atc_cancel_request";
    sco_result r = Admit(self, fn);
    if (r != SCO_OK) return r;
    if (!atc) return Fail(self, fn, SCO_BAD_ARG, "atc_id is 0");
    r = Gate(self, fn, atc_, atcWhy_);
    if (r != SCO_OK) return r;
    const char* why = "the game didn't say why";
    r = p_.atc_cancel(atc, &why);
    if (r != SCO_OK) return Fail(self, fn, r, why);
    if (p_.log) p_.log(self, fn, SCO_OK, "request cancelled");
    return SCO_OK;
}

sco_result Hangar::LastError(sco_plugin* self, char* out, uint32_t* inoutSize) {
    if (!inoutSize || (!out && *inoutSize)) return SCO_BAD_ARG;
    const char* msg = t_threadErr;
    if (p_.on_thread && p_.on_thread() && self && p_.live && p_.live(self)) {
        const ErrSlot* e = FindSlot(self);
        msg = e ? e->msg : "";
    }
    const size_t need = std::strlen(msg) + 1;
    if (*inoutSize < need) {
        *inoutSize = static_cast<uint32_t>(need);
        return SCO_TOO_MANY;
    }
    std::memcpy(out, msg, need);
    *inoutSize = static_cast<uint32_t>(need);
    return SCO_OK;
}

void Hangar::ReleaseOwner(const void* owner) {
    for (ErrSlot& e : errs_)
        if (owner && e.owner == owner) e = {};
}

}  // namespace sco::game::services
