// SPDX-License-Identifier: GPL-3.0-only
// game.reputation 1.0: the portable policy (reputation.h). No game memory here; the engine reads
// are the provider's.
#include "reputation.h"
#include "sco/caps.h"
#include <cstdio>
#include <cstring>

namespace sco::game::services {

namespace {

constexpr char kOffPending[] = "OFF pending in-game confirmation (docs/game/reputation.md)";
constexpr char kRowsNotOk[]  = "rows not OK on this build (docs/game/reputation.md)";
constexpr char kStopped[]    = "game.reputation stopped";

thread_local char t_threadErr[Reputation::kErrMax];

void Copy(char* dst, size_t n, const char* src) { std::snprintf(dst, n, "%s", src ? src : ""); }

}  // namespace

sco_result Reputation::Start(const ReputationProvider& provider) {
    if (!provider.on_thread || !provider.live || !provider.on_thread()) return SCO_WRONG_THREAD;
    p_ = provider;
    checks_ = p_.probed && p_.rows && p_.checks;
    Copy(checksWhy_, sizeof(checksWhy_), checks_ ? "" : p_.rows ? kOffPending : kRowsNotOk);
    caps::Set(kReputationCapChecks, checks_, checksWhy_);
    started_ = true;
    return SCO_OK;
}

void Reputation::Stop() {
    if (!started_) return;
    started_ = checks_ = false;
    Copy(checksWhy_, sizeof(checksWhy_), kStopped);
    caps::Set(kReputationCapChecks, false, kStopped);
    for (ErrSlot& e : errs_) e = {};
}

const Reputation::ErrSlot* Reputation::FindSlot(const void* owner) const {
    for (const ErrSlot& e : errs_)
        if (e.owner == owner) return &e;
    return nullptr;
}

Reputation::ErrSlot& Reputation::SlotFor(const void* owner) {
    ErrSlot* oldest = &errs_[0];
    for (ErrSlot& e : errs_) {
        if (e.owner == owner) return e;
        if (e.seq < oldest->seq) oldest = &e;
    }
    *oldest = {};
    oldest->owner = owner;
    return *oldest;
}

sco_result Reputation::Fail(sco_plugin* self, const char* fn, sco_result r, const char* why) {
    ErrSlot& e = SlotFor(self);
    Copy(e.msg, sizeof(e.msg), why);
    e.seq = ++seq_;
    if (p_.log) p_.log(self, fn, r, e.msg);
    return r;
}

// Thread and self first: a refusal here never reaches a plugin's slot.
sco_result Reputation::Admit(sco_plugin* self, const char* fn) {
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

sco_result Reputation::Checks(sco_plugin* self, sc_reputation_checks* out) {
    const char* fn = "checks";
    sco_result r = Admit(self, fn);
    if (r != SCO_OK) return r;
    if (!out || out->size < sizeof(*out)) return Fail(self, fn, SCO_BAD_ARG, "out is NULL or out->size is too small");
    out->sites = out->intact = out->services = 0;
    if (!started_) return Fail(self, fn, SCO_UNAVAILABLE, kStopped);
    if (!checks_) return Fail(self, fn, SCO_UNAVAILABLE, checksWhy_);
    sc_reputation_checks got{};
    const char* why = "the game didn't say why";
    r = p_.checks(&got, &why);
    if (r != SCO_OK) return Fail(self, fn, r, why);
    out->sites = got.sites;
    out->intact = got.intact;
    out->services = got.services;
    return SCO_OK;
}

sco_result Reputation::LastError(sco_plugin* self, char* out, uint32_t* inoutSize) {
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

void Reputation::ReleaseOwner(const void* owner) {
    for (ErrSlot& e : errs_)
        if (owner && e.owner == owner) e = {};
}

}  // namespace sco::game::services
