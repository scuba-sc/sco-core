// SPDX-License-Identifier: GPL-3.0-only
// game.contracts 1.0: the portable policy (contracts_service.h). No game memory here; the engine
// read is the provider's.
#include "contracts_service.h"
#include "sco/caps.h"
#include <cstdio>
#include <cstring>

namespace sco::game::services {

namespace {

constexpr char kOffPending[] = "OFF pending in-game confirmation (docs/game/contracts-service.md)";
constexpr char kRowsNotOk[]  = "rows not OK on this build (docs/game/contracts-service.md)";
constexpr char kStopped[]    = "game.contracts stopped";

thread_local char t_threadErr[Contracts::kErrMax];

void Copy(char* dst, size_t n, const char* src) { std::snprintf(dst, n, "%s", src ? src : ""); }

}  // namespace

sco_result Contracts::Start(const ContractsProvider& provider) {
    if (!provider.on_thread || !provider.live || !provider.on_thread()) return SCO_WRONG_THREAD;
    p_ = provider;
    radius_ = p_.probed && p_.radius_rows && p_.stream_radius;
    Copy(radiusWhy_, sizeof(radiusWhy_), radius_ ? "" : p_.radius_rows ? kOffPending : kRowsNotOk);
    caps::Set(kContractsCapStreamRadius, radius_, radiusWhy_);
    started_ = true;
    return SCO_OK;
}

void Contracts::Stop() {
    if (!started_) return;
    started_ = radius_ = false;
    Copy(radiusWhy_, sizeof(radiusWhy_), kStopped);
    caps::Set(kContractsCapStreamRadius, false, kStopped);
    for (ErrSlot& e : errs_) e = {};
}

const Contracts::ErrSlot* Contracts::FindSlot(const void* owner) const {
    for (const ErrSlot& e : errs_)
        if (e.owner == owner) return &e;
    return nullptr;
}

Contracts::ErrSlot& Contracts::SlotFor(const void* owner) {
    ErrSlot* oldest = &errs_[0];
    for (ErrSlot& e : errs_) {
        if (e.owner == owner) return e;
        if (e.seq < oldest->seq) oldest = &e;
    }
    *oldest = {};
    oldest->owner = owner;
    return *oldest;
}

sco_result Contracts::Fail(sco_plugin* self, const char* fn, sco_result r, const char* why) {
    ErrSlot& e = SlotFor(self);
    Copy(e.msg, sizeof(e.msg), why);
    e.seq = ++seq_;
    if (p_.log) p_.log(self, fn, r, e.msg);
    return r;
}

// Thread and self first: a refusal here never reaches a plugin's slot.
sco_result Contracts::Admit(sco_plugin* self, const char* fn) {
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

sco_result Contracts::Gate(sco_plugin* self, const char* fn, bool on, const char* why) {
    if (!started_) return Fail(self, fn, SCO_UNAVAILABLE, kStopped);
    if (!on) return Fail(self, fn, SCO_UNAVAILABLE, why);
    return SCO_OK;
}

sco_result Contracts::StreamRadius(sco_plugin* self, float* outRadius) {
    const char* fn = "stream_radius";
    sco_result r = Admit(self, fn);
    if (r != SCO_OK) return r;
    if (!outRadius) return Fail(self, fn, SCO_BAD_ARG, "out_radius is NULL");
    *outRadius = 0.0f;
    r = Gate(self, fn, radius_, radiusWhy_);
    if (r != SCO_OK) return r;
    const char* why = "the game didn't say why";
    float radius = 0.0f;
    r = p_.stream_radius(&radius, &why);
    if (r != SCO_OK) return Fail(self, fn, r, why);
    *outRadius = radius;
    return SCO_OK;
}

sco_result Contracts::LastError(sco_plugin* self, char* out, uint32_t* inoutSize) {
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

void Contracts::ReleaseOwner(const void* owner) {
    for (ErrSlot& e : errs_)
        if (owner && e.owner == owner) e = {};
}

}  // namespace sco::game::services
