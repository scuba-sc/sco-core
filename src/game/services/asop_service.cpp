// SPDX-License-Identifier: GPL-3.0-only
// game.asop 1.0: the portable policy (asop_service.h). No game memory here; the engine reads are
// the provider's.
#include "asop_service.h"
#include "sco/caps.h"
#include <cstdio>
#include <cstring>

namespace sco::game::services {

namespace {

constexpr char kOffPending[] = "OFF pending in-game confirmation (docs/game/asop-service.md)";
constexpr char kRowsNotOk[]  = "rows not OK on this build (docs/game/asop-service.md)";
constexpr char kStopped[]    = "game.asop stopped";

thread_local char t_threadErr[Asop::kErrMax];

void Copy(char* dst, size_t n, const char* src) { std::snprintf(dst, n, "%s", src ? src : ""); }

}  // namespace

sco_result Asop::Start(const AsopProvider& provider) {
    if (!provider.on_thread || !provider.live || !provider.on_thread()) return SCO_WRONG_THREAD;
    p_ = provider;
    claim_ = p_.probed && p_.claim_rows && p_.claim_timeout;
    shard_ = p_.probed && p_.shard_rows && p_.shard_persisted;
    Copy(claimWhy_, sizeof(claimWhy_), claim_ ? "" : p_.claim_rows ? kOffPending : kRowsNotOk);
    Copy(shardWhy_, sizeof(shardWhy_), shard_ ? "" : p_.shard_rows ? kOffPending : kRowsNotOk);
    caps::Set(kAsopCapClaimTimeout, claim_, claimWhy_);
    caps::Set(kAsopCapShardPersisted, shard_, shardWhy_);
    started_ = true;
    return SCO_OK;
}

void Asop::Stop() {
    if (!started_) return;
    started_ = claim_ = shard_ = false;
    Copy(claimWhy_, sizeof(claimWhy_), kStopped);
    Copy(shardWhy_, sizeof(shardWhy_), kStopped);
    caps::Set(kAsopCapClaimTimeout, false, kStopped);
    caps::Set(kAsopCapShardPersisted, false, kStopped);
    for (ErrSlot& e : errs_) e = {};
}

const Asop::ErrSlot* Asop::FindSlot(const void* owner) const {
    for (const ErrSlot& e : errs_)
        if (e.owner == owner) return &e;
    return nullptr;
}

Asop::ErrSlot& Asop::SlotFor(const void* owner) {
    ErrSlot* oldest = &errs_[0];
    for (ErrSlot& e : errs_) {
        if (e.owner == owner) return e;
        if (e.seq < oldest->seq) oldest = &e;
    }
    *oldest = {};
    oldest->owner = owner;
    return *oldest;
}

sco_result Asop::Fail(sco_plugin* self, const char* fn, sco_result r, const char* why) {
    ErrSlot& e = SlotFor(self);
    Copy(e.msg, sizeof(e.msg), why);
    e.seq = ++seq_;
    if (p_.log) p_.log(self, fn, r, e.msg);
    return r;
}

// Thread and self first: a refusal here never reaches a plugin's slot.
sco_result Asop::Admit(sco_plugin* self, const char* fn) {
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

sco_result Asop::Gate(sco_plugin* self, const char* fn, bool on, const char* why) {
    if (!started_) return Fail(self, fn, SCO_UNAVAILABLE, kStopped);
    if (!on) return Fail(self, fn, SCO_UNAVAILABLE, why);
    return SCO_OK;
}

sco_result Asop::ClaimTimeout(sco_plugin* self, int32_t* outSeconds) {
    const char* fn = "claim_timeout";
    sco_result r = Admit(self, fn);
    if (r != SCO_OK) return r;
    if (!outSeconds) return Fail(self, fn, SCO_BAD_ARG, "out_seconds is NULL");
    *outSeconds = 0;
    r = Gate(self, fn, claim_, claimWhy_);
    if (r != SCO_OK) return r;
    const char* why = "the game didn't say why";
    int32_t seconds = 0;
    r = p_.claim_timeout(&seconds, &why);
    if (r != SCO_OK) return Fail(self, fn, r, why);
    *outSeconds = seconds;
    return SCO_OK;
}

sco_result Asop::ShardPersisted(sco_plugin* self, uint32_t* outPersisted) {
    const char* fn = "shard_persisted";
    sco_result r = Admit(self, fn);
    if (r != SCO_OK) return r;
    if (!outPersisted) return Fail(self, fn, SCO_BAD_ARG, "out_persisted is NULL");
    *outPersisted = 0;
    r = Gate(self, fn, shard_, shardWhy_);
    if (r != SCO_OK) return r;
    const char* why = "the game didn't say why";
    uint32_t persisted = 0;
    r = p_.shard_persisted(&persisted, &why);
    if (r != SCO_OK) return Fail(self, fn, r, why);
    *outPersisted = persisted ? 1u : 0u;
    return SCO_OK;
}

sco_result Asop::LastError(sco_plugin* self, char* out, uint32_t* inoutSize) {
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

void Asop::ReleaseOwner(const void* owner) {
    for (ErrSlot& e : errs_)
        if (owner && e.owner == owner) e = {};
}

}  // namespace sco::game::services
