// SPDX-License-Identifier: GPL-3.0-only
// game.asop 1.0 on Windows: the AsopProvider built from rows, and the published table. The reads
// are the ones docs/game/asop.md and include/sco/game/asop.h document: insurance.config is the
// cached SInsuranceComponentConfig* (RIP of insurance.update_pending_requests+0x58) whose int32 at
// kInsuranceTimeout (+0x48, pinned by the +0x9F check) is the request timeout in seconds (rc5), and
// asop.shard_persisted is the IsShardPersisted byte asop.on_request_open compares at +0x28. Reads
// only: no hooks, no patches, no writes. Both capabilities stay OFF (AsopProvider::probed is
// false) until an in-game run confirms them.
#include "asop_service.h"
#include "../../api/internal.h"
#include "sco/game/asop.h"
#include "sco/host.h"
#include "sco/log.h"
#include "sco/signatures.h"
#include "sco/signature_registry.h"
#include <iterator>
#include <windows.h>

namespace sco::game::services {

namespace {

constexpr const char* kClaimRows[] = { "insurance.update_pending_requests", "insurance.config" };
constexpr const char* kShardRows[] = { "asop.on_request_open", "asop.shard_persisted" };

struct Engine {
    const uintptr_t* insuranceConfig = nullptr;
    const uint8_t*   shardPersisted = nullptr;
};
Engine g_e;
Asop   g_asop;
bool   g_published = false;

bool RowsOk(const char* const* ids, size_t n) {
    const auto signatures = AcquireHostSignatureSnapshot();
    if (!signatures) return false;
    for (size_t i = 0; i < n; ++i) {
        const SigResult* s = (signatures ? signatures->Lookup(ids[i]) : nullptr);
        if (!s || s->state != SigState::Ok) return false;
    }
    return true;
}

// ---- game reads, under SEH ---------------------------------------------------------------------
//
// 1 read, 0 not there, -1 the game faulted. No C++ object with a destructor lives in these frames.

int ReadClaimTimeout(int32_t* seconds) {
    *seconds = 0;
    __try {
        const uintptr_t config = *g_e.insuranceConfig;
        if (!config) return 0;
        *seconds = *reinterpret_cast<const int32_t*>(config + sco::game::asop::kInsuranceTimeout);
        return 1;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        *seconds = 0;
        return -1;
    }
}

int ReadShardPersisted(uint32_t* persisted) {
    *persisted = 0;
    __try {
        *persisted = *g_e.shardPersisted ? 1u : 0u;
        return 1;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        *persisted = 0;
        return -1;
    }
}

// ---- the provider ------------------------------------------------------------------------------

sco_result ClaimTimeout(int32_t* seconds, const char** why) {
    const int got = ReadClaimTimeout(seconds);
    if (got < 0) { *why = "the game faulted reading the insurance configuration"; return SCO_FAILED; }
    if (!got) { *why = "the insurance configuration isn't loaded yet"; return SCO_NOT_FOUND; }
    return SCO_OK;
}

sco_result ShardPersisted(uint32_t* persisted, const char** why) {
    if (ReadShardPersisted(persisted) < 0) { *why = "the game faulted reading IsShardPersisted"; return SCO_FAILED; }
    return SCO_OK;
}

bool OnThread() { return OnGameThread(); }
bool Live(const sco_plugin* p) { return host::PluginId(p) && !detail::Released(p); }

void LogCall(const sco_plugin* self, const char* fn, sco_result r, const char* why) {
    const char* id = self ? host::PluginId(self) : nullptr;
    Log("[game] asop: %s: %s -> %s: %s", id ? id : "?", fn, ResultName(static_cast<Result>(r)), why);
}

sco_result SvcClaimTimeout(sco_plugin* self, int32_t* outSeconds) { return g_asop.ClaimTimeout(self, outSeconds); }
sco_result SvcShardPersisted(sco_plugin* self, uint32_t* outPersisted) { return g_asop.ShardPersisted(self, outPersisted); }
sco_result SvcLastError(sco_plugin* self, char* out, uint32_t* inoutSize) { return g_asop.LastError(self, out, inoutSize); }

const sc_asop_v1 kService = {
    sizeof(sc_asop_v1), 0, SvcClaimTimeout, SvcShardPersisted, SvcLastError,
};

}  // namespace

Result StartAsop() {
    if (g_published) return Result::Ok;
    if (!OnGameThread()) return Result::WrongThread;
    AsopProvider p;
    p.on_thread = OnThread;
    p.live = Live;
    p.claim_rows = RowsOk(kClaimRows, std::size(kClaimRows));
    p.shard_rows = RowsOk(kShardRows, std::size(kShardRows));
    p.probed = false;
    g_e = {};
    if (p.claim_rows) g_e.insuranceConfig = reinterpret_cast<const uintptr_t*>(Sig("insurance.config"));
    if (p.shard_rows) g_e.shardPersisted = Sig("asop.shard_persisted");
    p.claim_timeout = ClaimTimeout;
    p.shard_persisted = ShardPersisted;
    p.log = LogCall;
    if (g_asop.Start(p) != SCO_OK) {
        g_e = {};
        return Result::WrongThread;
    }
    const Result r = host::ProvideGameService(SC_ASOP_SERVICE_NAME, SC_ASOP_SERVICE_VERSION, &kService);
    if (r != Result::Ok) {
        g_asop.Stop();
        g_e = {};
        return r;
    }
    g_published = true;
    Log("[game] game.asop 1.0 published: game.asop.claim_timeout and game.asop.shard_persisted held OFF pending an in-game run");
    return Result::Ok;
}

void StopAsop() {
    if (!g_published) return;
    host::WithdrawGameService(SC_ASOP_SERVICE_NAME);
    g_asop.Stop();
    g_e = {};
    g_published = false;
}

void ReleaseAsopOwner(const void* owner) { g_asop.ReleaseOwner(owner); }

}  // namespace sco::game::services
